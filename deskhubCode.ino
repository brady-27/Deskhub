#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <SpotifyEsp32.h>

const char* WIFI_SSID     = "placeholder";
const char* WIFI_PASS     = "placeholder";
const char* SPOTIFY_CLIENT_ID     = "placeholder";
const char* SPOTIFY_CLIENT_SECRET = "placeholder";

static const float WEATHER_LAT = placeholder;
static const float WEATHER_LON = placeholder;
const char* TZ_INFO = "EST5EDT,M3.2.0/2,M11.1.0/2";

#define SCREEN_W 800
#define SCREEN_H 480

#define I2C_SDA    8
#define I2C_SCL    9
#define TOUCH_INT  4

#define CH422G_DIR_ADDR 0x22
#define CH422G_OUT_ADDR 0x23

#define PIR_PIN 13

#define C_BG     0x080C
#define C_PANEL  0x1082
#define C_PANEL2 0x18C3
#define C_TEXT   0xFFFF
#define C_MUTED  0x9CF3
#define C_DIM    0x39E7
#define C_BLUE   0x3B7F
#define C_BLUE2  0x6CBF
#define C_BLACK  0x0000

Arduino_ESP32RGBPanel *rgbBus = new Arduino_ESP32RGBPanel(
    5, 3, 46, 7,
    1, 2, 42, 41, 40,
    39, 0, 45, 48, 47, 21,
    14, 38, 18, 17, 10,
    0, 8, 4, 8,
    0, 8, 4, 8,
    1, 16000000L
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(SCREEN_W, SCREEN_H, rgbBus, 0, true);

TAMC_GT911 ts(I2C_SDA, I2C_SCL, TOUCH_INT, -1, SCREEN_W, SCREEN_H);

Spotify sp(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET);

enum Page {
    PAGE_CLOCK = 0,
    PAGE_WEATHER,
    PAGE_SPOTIFY,
    PAGE_STATUS,
    PAGE_TIMER,
    PAGE_STOPWATCH,
    PAGE_COUNT
};

Page currentPage = PAGE_CLOCK;
bool settingsOpen = false;

unsigned long lastCarouselMs    = 0;
unsigned long carouselIntervalMs = 10000;

bool motionWakeEnabled    = true;
unsigned long screenTimeoutMs = 30UL * 60UL * 1000UL;
unsigned long lastMotionMs    = 0;
bool screenAwake = true;

Preferences prefs;

float  weatherTempF  = NAN;
int    weatherHumidity = -1;
float  weatherHighF  = NAN;
float  weatherLowF   = NAN;
int    weatherCode   = -1;
String weatherCondition = "Loading";
String weatherStatus    = "Not loaded";
unsigned long lastWeatherFetchMs = 0;
const unsigned long weatherFetchIntervalMs = 20UL * 60UL * 1000UL;

String spotifyArtist = "";
String spotifyTrack  = "";
String spotifyStatus = "Not authenticated";
unsigned long lastSpotifyFetchMs = 0;
const unsigned long spotifyFetchIntervalMs = 3000;

int  timerHours   = 0;
int  timerMinutes = 5;
int  timerSeconds = 0;
long timerRemainingMs = 0;
bool timerRunning = false;
bool timerPaused  = false;
unsigned long timerLastTickMs = 0;

bool stopwatchRunning = false;
unsigned long stopwatchStartMs       = 0;
unsigned long stopwatchAccumulatedMs = 0;

bool needsRedraw = true;
unsigned long lastSerialPagePrintMs = 0;

struct TouchPoint {
    bool    pressed;
    int16_t x;
    int16_t y;
};

bool inBox(int16_t x, int16_t y, int16_t bx, int16_t by, int16_t bw, int16_t bh) {
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

String twoDigits(int n) {
    return (n < 10) ? "0" + String(n) : String(n);
}

String formatTimeMs(unsigned long ms, bool hundredths = false) {
    unsigned long total   = ms / 1000UL;
    unsigned long hours   = total / 3600UL;
    unsigned long minutes = (total % 3600UL) / 60UL;
    unsigned long seconds = total % 60UL;
    unsigned long cents   = (ms % 1000UL) / 10UL;

    String s;
    if (hours > 0)
        s = twoDigits(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(seconds);
    else
        s = twoDigits(minutes) + ":" + twoDigits(seconds);

    if (hundredths) s += "." + twoDigits(cents);
    return s;
}

String weatherCodeToText(int code) {
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Mostly Clear";
        case 2:  return "Partly Cloudy";
        case 3:  return "Cloudy";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing Drizzle";
        case 61: case 63: case 65: return "Rain";
        case 66: case 67: return "Freezing Rain";
        case 71: case 73: case 75: return "Snow";
        case 77: return "Snow Grains";
        case 80: case 81: case 82: return "Rain Showers";
        case 85: case 86: return "Snow Showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Storm + Hail";
        default: return "Unknown";
    }
}

void ch422g_init() {
    Wire.beginTransmission(CH422G_DIR_ADDR);
    Wire.write(0xFF);
    Wire.endTransmission();
    delay(5);
    Wire.beginTransmission(CH422G_OUT_ADDR);
    Wire.write(0b11111100);
    Wire.endTransmission();
    delay(20);
    Wire.beginTransmission(CH422G_OUT_ADDR);
    Wire.write(0xFF);
    Wire.endTransmission();
    delay(50);
}

void connectWiFi() {
    Serial.print("[WIFI] Connecting...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
        Serial.println("[WIFI] Connected: " + WiFi.localIP().toString());
    else
        Serial.println("[WIFI] Failed.");
}

void setupTime() {
    configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
}

String getClockString() {
    struct tm t;
    if (!getLocalTime(&t)) return "--:--";
    int h = t.tm_hour;
    const char* suf = h >= 12 ? "PM" : "AM";
    h %= 12;
    if (h == 0) h = 12;
    return String(h) + ":" + twoDigits(t.tm_min) + " " + suf;
}

String getDateString() {
    struct tm t;
    if (!getLocalTime(&t)) return "Loading...";
    char buf[40];
    strftime(buf, sizeof(buf), "%A, %B %d", &t);
    return String(buf);
}

void fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) { weatherStatus = "Wi-Fi offline"; return; }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = "https://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(WEATHER_LAT, 4);
    url += "&longitude=" + String(WEATHER_LON, 4);
    url += "&current=temperature_2m,relative_humidity_2m,weather_code";
    url += "&daily=temperature_2m_max,temperature_2m_min";
    url += "&temperature_unit=fahrenheit&timezone=auto";

    if (!http.begin(client, url)) { weatherStatus = "HTTP begin failed"; return; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        weatherStatus = "HTTP " + String(code);
        http.end();
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) { weatherStatus = "JSON error"; return; }

    weatherTempF    = doc["current"]["temperature_2m"] | NAN;
    weatherHumidity = doc["current"]["relative_humidity_2m"] | -1;
    weatherCode     = doc["current"]["weather_code"] | -1;
    weatherHighF    = doc["daily"]["temperature_2m_max"][0] | NAN;
    weatherLowF     = doc["daily"]["temperature_2m_min"][0] | NAN;
    weatherCondition = weatherCodeToText(weatherCode);
    weatherStatus    = "Online";
    lastWeatherFetchMs = millis();

    Serial.printf("[WEATHER] %.1f°F  %s  Hi:%.0f Lo:%.0f  Hum:%d%%\n",
        weatherTempF, weatherCondition.c_str(), weatherHighF, weatherLowF, weatherHumidity);
}

void setupSpotify() {
    sp.begin();
    spotifyStatus = "Auth needed";
}

void updateSpotify() {
    if (WiFi.status() != WL_CONNECTED) { spotifyStatus = "Wi-Fi offline"; return; }
    if (!sp.is_auth()) { sp.handle_client(); spotifyStatus = "Auth needed"; return; }
    if (millis() - lastSpotifyFetchMs < spotifyFetchIntervalMs) return;
    lastSpotifyFetchMs = millis();

    String a = sp.current_artist_names();
    String t = sp.current_track_name();
    if (a.length() && a != "Something went wrong" && a != "null") spotifyArtist = a;
    if (t.length() && t != "Something went wrong" && t != "null") spotifyTrack  = t;
    spotifyStatus = "Connected";
}

void loadSettings() {
    prefs.begin("deskhub", false);
    carouselIntervalMs = prefs.getULong("carousel", 10000);
    screenTimeoutMs    = prefs.getULong("timeout",  30UL * 60UL * 1000UL);
    motionWakeEnabled  = prefs.getBool("motion",    true);
    prefs.end();
}

void saveSettings() {
    prefs.begin("deskhub", false);
    prefs.putULong("carousel", carouselIntervalMs);
    prefs.putULong("timeout",  screenTimeoutMs);
    prefs.putBool("motion",    motionWakeEnabled);
    prefs.end();
    Serial.println("[SETTINGS] Saved.");
}

void startTimer() {
    if (timerRemainingMs <= 0)
        timerRemainingMs = ((long)timerHours * 3600L + timerMinutes * 60L + timerSeconds) * 1000L;
    timerRunning = true;
    timerPaused  = false;
    timerLastTickMs = millis();
    needsRedraw = true;
}

void pauseTimer() { timerRunning = false; timerPaused = true; needsRedraw = true; }

void resetTimer() {
    timerRunning = false;
    timerPaused  = false;
    timerRemainingMs = ((long)timerHours * 3600L + timerMinutes * 60L + timerSeconds) * 1000L;
    needsRedraw = true;
}

void updateTimer() {
    if (!timerRunning) return;
    unsigned long now = millis();
    timerRemainingMs -= (long)(now - timerLastTickMs);
    timerLastTickMs = now;
    if (timerRemainingMs <= 0) {
        timerRemainingMs = 0;
        timerRunning = false;
        timerPaused  = false;
        Serial.println("[TIMER] Done!");
    }
}

void startStopwatch() {
    if (!stopwatchRunning) {
        stopwatchStartMs = millis();
        stopwatchRunning = true;
        needsRedraw = true;
    }
}

void pauseStopwatch() {
    if (stopwatchRunning) {
        stopwatchAccumulatedMs += millis() - stopwatchStartMs;
        stopwatchRunning = false;
        needsRedraw = true;
    }
}

void resetStopwatch() {
    stopwatchRunning       = false;
    stopwatchStartMs       = 0;
    stopwatchAccumulatedMs = 0;
    needsRedraw = true;
}

unsigned long currentStopwatchMs() {
    if (stopwatchRunning)
        return stopwatchAccumulatedMs + (millis() - stopwatchStartMs);
    return stopwatchAccumulatedMs;
}

TouchPoint readTouch() {
    ts.read();
    if (ts.isTouched && ts.touches > 0)
        return { true, (int16_t)ts.points[0].x, (int16_t)ts.points[0].y };
    return { false, 0, 0 };
}

void gfxText(int16_t x, int16_t y, const String& s, uint16_t color = C_TEXT, uint8_t size = 2) {
    gfx->setTextColor(color);
    gfx->setTextSize(size);
    gfx->setCursor(x, y);
    gfx->print(s);
}

void drawPageDots(int active) {
    int spacing = 22;
    int startX  = SCREEN_W / 2 - ((PAGE_COUNT - 1) * spacing) / 2;
    int y = SCREEN_H - 45;
    for (int i = 0; i < PAGE_COUNT; i++)
        gfx->fillCircle(startX + i * spacing, y, 5, i == active ? C_BLUE : C_DIM);
}

void drawGearButton() {
    gfx->fillRect(SCREEN_W - 90, 15, 75, 40, C_PANEL);
    gfxText(SCREEN_W - 78, 23, "[gear]", C_TEXT, 1);
}

void drawTopLine() {
    gfx->drawLine(300, 45, 500, 45, C_DIM);
    gfx->fillCircle(400, 45, 4, C_BLUE);
}

void drawPill(int x, int y, int w, int h, const String& text) {
    gfx->fillRect(x, y, w, h, C_PANEL);
    gfx->drawRect(x, y, w, h, C_PANEL2);
    gfxText(x + 14, y + 13, text, C_TEXT, 1);
}

void drawSegmentedControl(bool timerSelected) {
    gfx->fillRect(280, 75, 240, 42, C_PANEL);
    if (timerSelected) {
        gfx->fillRect(280, 75, 120, 42, C_PANEL2);
        gfxText(310, 87, "Timer",     C_TEXT,  2);
        gfxText(420, 87, "Stopwatch", C_MUTED, 1);
    } else {
        gfx->fillRect(400, 75, 120, 42, C_PANEL2);
        gfxText(310, 87, "Timer",     C_MUTED, 1);
        gfxText(415, 87, "Stopwatch", C_TEXT,  2);
    }
}

void drawClockPage() {
    gfx->fillScreen(C_BG);
    drawGearButton();
    gfxText(220, 165, getClockString(), C_TEXT, 6);
    gfxText(250, 245, getDateString(),  C_TEXT, 3);
    drawPageDots(PAGE_CLOCK);
}

void drawWeatherPage() {
    gfx->fillScreen(C_BG);
    drawTopLine();
    drawGearButton();
    gfxText(325, 80, "WEATHER", C_TEXT, 3);

    String tempStr = isnan(weatherTempF) ? "--°F" : String((int)round(weatherTempF)) + "F";
    String hiStr   = isnan(weatherHighF)  ? "Hi --" : "Hi " + String((int)round(weatherHighF));
    String loStr   = isnan(weatherLowF)   ? "Lo --" : "Lo " + String((int)round(weatherLowF));
    String humStr  = weatherHumidity < 0  ? "Hum --%" : "Hum " + String(weatherHumidity) + "%";

    gfxText(340, 150, tempStr,          C_TEXT,  6);
    gfxText(320, 245, weatherCondition, C_TEXT,  3);
    gfxText(340, 285, "Cary, NC",       C_MUTED, 2);

    drawPill( 80, 360, 180, 48, hiStr);
    drawPill(300, 360, 180, 48, loStr);
    drawPill(520, 360, 210, 48, humStr);

    drawPageDots(PAGE_WEATHER);
}

void drawSpotifyPage() {
    gfx->fillScreen(C_BG);
    drawTopLine();
    drawGearButton();
    gfxText(330, 75, "SPOTIFY", C_TEXT, 3);

    String track  = spotifyTrack.length()  ? spotifyTrack  : "Nothing playing";
    String artist = spotifyArtist.length() ? spotifyArtist : "---";

    gfx->fillRect(80, 130, 210, 160, C_PANEL2);
    gfxText(112, 200, "Album Art", C_MUTED, 2);

    gfxText(355, 140, track,  C_TEXT,  3);
    gfxText(355, 190, artist, C_MUTED, 2);
    gfxText(355, 220, "Now Playing", C_MUTED, 1);

    gfxText(370, 295, "|<",  C_TEXT, 3);
    gfxText(460, 295, "||",  C_TEXT, 3);
    gfxText(560, 295, ">|",  C_TEXT, 3);

    gfx->drawLine(90, 385, 650, 385, C_DIM);
    gfx->drawLine(90, 385, 300, 385, C_BLUE);
    gfx->fillCircle(300, 385, 6, C_BLUE);

    gfxText(720, 120, "vol", C_MUTED, 1);
    gfx->drawLine(730, 150, 730, 330, C_DIM);
    gfx->drawLine(730, 240, 730, 330, C_BLUE);
    gfx->fillCircle(730, 240, 7, C_BLUE);

    drawPageDots(PAGE_SPOTIFY);
}

void drawStatusPage() {
    gfx->fillScreen(C_BG);
    drawTopLine();
    drawGearButton();
    gfxText(240, 75, "SYSTEM STATUS", C_TEXT, 3);

    drawPill(110, 140, 580, 44, "Motion:  " + String(motionWakeEnabled ? "Active" : "Off"));
    drawPill(110, 194, 580, 44, "Timeout: " + String(screenTimeoutMs / 60000UL) + " min");
    drawPill(110, 248, 580, 44, "Wi-Fi:   " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Offline"));
    drawPill(110, 302, 580, 44, "Weather: " + weatherStatus);
    drawPill(110, 356, 580, 44, "Spotify: " + spotifyStatus);

    drawPageDots(PAGE_STATUS);
}

void drawTimerPage() {
    gfx->fillScreen(C_BG);
    drawTopLine();
    drawGearButton();
    drawSegmentedControl(true);

    String setStr = twoDigits(timerHours) + " : " + twoDigits(timerMinutes) + " : " + twoDigits(timerSeconds);
    gfxText(275, 145, setStr, C_TEXT, 4);
    gfxText(235, 210, "HOURS      MINUTES    SECONDS", C_MUTED, 1);

    if (timerRunning || timerPaused) {
        gfxText(310, 275, formatTimeMs(timerRemainingMs), C_TEXT, 4);
    }

    String leftLabel  = (timerRunning || timerPaused) ? "Cancel" : "Reset";
    String rightLabel = timerRunning ? "Pause" : "Start";

    gfxText(220, 380, leftLabel,  C_TEXT, 3);
    gfxText(480, 380, rightLabel, C_TEXT, 3);

    drawPageDots(PAGE_TIMER);
}

void drawStopwatchPage() {
    gfx->fillScreen(C_BG);
    drawTopLine();
    drawGearButton();
    drawSegmentedControl(false);

    gfxText(215, 160, formatTimeMs(currentStopwatchMs(), true), C_TEXT, 5);

    gfxText(220, 360, "Reset", C_TEXT, 3);
    gfxText(490, 360, stopwatchRunning ? "Pause" : "Start", C_TEXT, 3);

    drawPageDots(PAGE_STOPWATCH);
}

void drawSettingsScreen() {
    gfx->fillScreen(C_BG);
    gfxText(305, 45, "SETTINGS", C_TEXT, 3);

    drawPill(110, 100, 580, 42, "Brightness:  70%");
    drawPill(110, 152, 580, 42, "Carousel:    " + String(carouselIntervalMs / 1000UL) + " sec");
    drawPill(110, 204, 580, 42, "Motion Wake: " + String(motionWakeEnabled ? "On" : "Off"));
    drawPill(110, 256, 580, 42, "Timeout:     " + String(screenTimeoutMs / 60000UL) + " min");
    drawPill(110, 308, 580, 42, "Theme:       Dark");
    drawPill(110, 360, 580, 42, "Wi-Fi:       " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Offline"));
    drawPill(110, 412, 580, 42, "Spotify:     " + spotifyStatus);

    gfx->fillRect(280, 448, 240, 44, C_PANEL2);
    gfxText(314, 458, "Save & Close", C_BLUE2, 2);
}

void drawCurrentScreen() {
    if (!screenAwake) { gfx->fillScreen(C_BLACK); return; }
    if (settingsOpen) { drawSettingsScreen(); return; }

    switch (currentPage) {
        case PAGE_CLOCK:     drawClockPage();     break;
        case PAGE_WEATHER:   drawWeatherPage();   break;
        case PAGE_SPOTIFY:   drawSpotifyPage();   break;
        case PAGE_STATUS:    drawStatusPage();    break;
        case PAGE_TIMER:     drawTimerPage();     break;
        case PAGE_STOPWATCH: drawStopwatchPage(); break;
        default:             drawClockPage();     break;
    }
    needsRedraw = false;
}

void nextPage() {
    currentPage = (Page)((currentPage + 1) % PAGE_COUNT);
    lastCarouselMs = millis();
    needsRedraw = true;
}

void prevPage() {
    int p = (int)currentPage - 1;
    if (p < 0) p = PAGE_COUNT - 1;
    currentPage = (Page)p;
    lastCarouselMs = millis();
    needsRedraw = true;
}

void handleTouch() {
    TouchPoint t = readTouch();
    if (!t.pressed) return;

    lastMotionMs = millis();
    if (!screenAwake) { screenAwake = true; needsRedraw = true; return; }

    if (inBox(t.x, t.y, SCREEN_W - 90, 15, 75, 40)) {
        settingsOpen = !settingsOpen;
        needsRedraw = true;
        return;
    }

    if (settingsOpen) {
        if (inBox(t.x, t.y, 280, 448, 240, 44)) {
            saveSettings();
            settingsOpen = false;
            needsRedraw = true;
        }
        return;
    }

    if (t.x < 80)              prevPage();
    if (t.x > SCREEN_W - 80)  nextPage();

    if (currentPage == PAGE_TIMER) {
        if (inBox(t.x, t.y, 180, 355, 160, 60)) {
            if (timerRunning || timerPaused) resetTimer(); else resetTimer();
        }
        if (inBox(t.x, t.y, 440, 355, 160, 60)) {
            if (timerRunning) pauseTimer(); else startTimer();
        }
    }

    if (currentPage == PAGE_STOPWATCH) {
        if (inBox(t.x, t.y, 180, 345, 160, 60)) resetStopwatch();
        if (inBox(t.x, t.y, 450, 345, 160, 60)) {
            if (stopwatchRunning) pauseStopwatch(); else startStopwatch();
        }
    }
}

void handleMotionSensor() {
    if (!motionWakeEnabled) return;
    if (digitalRead(PIR_PIN) == HIGH) {
        lastMotionMs = millis();
        if (!screenAwake) { screenAwake = true; needsRedraw = true; }
    }
    if (screenAwake && millis() - lastMotionMs > screenTimeoutMs) {
        screenAwake = false;
        needsRedraw = true;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(I2C_SDA, I2C_SCL, 400000);
    ch422g_init();

    gfx->begin();
    gfx->fillScreen(C_BG);

    pinMode(TOUCH_INT, INPUT);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);

    pinMode(PIR_PIN, INPUT);
    lastMotionMs = millis();

    loadSettings();
    connectWiFi();
    setupTime();
    fetchWeather();
    setupSpotify();

    timerRemainingMs = ((long)timerHours * 3600L + timerMinutes * 60L + timerSeconds) * 1000L;
    needsRedraw = true;
}

void loop() {
    handleMotionSensor();
    handleTouch();
    updateTimer();
    updateSpotify();

    if (WiFi.status() == WL_CONNECTED && millis() - lastWeatherFetchMs > weatherFetchIntervalMs) {
        fetchWeather();
        needsRedraw = true;
    }

    if (!settingsOpen && screenAwake && millis() - lastCarouselMs > carouselIntervalMs)
        nextPage();

    bool periodicRedraw = false;
    if (millis() - lastSerialPagePrintMs > 1000) {
        lastSerialPagePrintMs = millis();
        if (currentPage == PAGE_CLOCK || currentPage == PAGE_TIMER || currentPage == PAGE_STOPWATCH)
            periodicRedraw = true;
    }

    if (needsRedraw || periodicRedraw)
        drawCurrentScreen();

    delay(20);
}
