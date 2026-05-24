/*
  DeskHub_AllInOne.ino
  ------------------------------------------------------------
  One-file DeskHub starter for Arduino IDE / ESP32.

  What this includes:
  - Wi-Fi connection
  - Live weather from Open-Meteo API for Cary, NC / 27519
  - SpotifyEsp32 login + now-playing display data
  - Clock page using NTP time
  - Weather page
  - Spotify page
  - System status page
  - Timer page
  - Stopwatch page
  - Gear settings button concept
  - Settings screen concept with Save & Close
  - Carousel rotation logic
  - Motion wake logic
  - Screen timeout logic

  IMPORTANT:
  This version is made to compile before you own the final screen.
  It uses placeholder display/touch functions so you can test the APIs
  and app logic first.

  When you get your exact 7-inch ESP32 touchscreen, replace the
  "DISPLAY PLACEHOLDER LAYER" functions with the real display/touch driver.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <SpotifyEsp32.h>

// ============================================================
// YOUR SETTINGS
// ============================================================

const char* WIFI_SSID = "placeholder";
const char* WIFI_PASS = "placeholder";

const char* SPOTIFY_CLIENT_ID = "placeholder";
const char* SPOTIFY_CLIENT_SECRET = "placeholder";

// Cary, NC / 27519 approximate coordinates
static const float WEATHER_LAT = placeholder;
static const float WEATHER_LON = placeholder;

// Eastern Time timezone string for ESP32 time library.
// Handles EST/EDT daylight saving.
const char* TZ_INFO = "EST5EDT,M3.2.0/2,M11.1.0/2";

// Hardware placeholders
#define PIR_PIN 13          // Change later to the PIR signal pin you actually use
#define TOUCH_ENABLED 0     // Keep 0 until your exact touchscreen driver is wired

// ============================================================
// SPOTIFY OBJECT
// ============================================================

Spotify sp(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET);

// ============================================================
// DISPLAY PLACEHOLDER LAYER
// Replace this section when you get the real 7-inch screen.
// ============================================================

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// RGB565 colors
#define C_BG       0x080C
#define C_PANEL    0x1082
#define C_PANEL2   0x18C3
#define C_TEXT     0xFFFF
#define C_MUTED    0x9CF3
#define C_DIM      0x39E7
#define C_BLUE     0x3B7F
#define C_BLUE2    0x6CBF
#define C_BLACK    0x0000
#define C_WHITE    0xFFFF

struct TouchPoint {
  bool pressed;
  int16_t x;
  int16_t y;
};

void displayBegin() {
  Serial.println("[DISPLAY] Placeholder display active.");
  Serial.println("[DISPLAY] When you get the screen, replace display functions with real driver calls.");
}

void displayClear(uint16_t color = C_BG) {
  // Real display: fill screen here.
  (void)color;
}

void displayText(int16_t x, int16_t y, const String& text, uint16_t color = C_TEXT, uint8_t size = 2) {
  // Real display: set cursor/color/text size/print here.
  (void)x; (void)y; (void)color; (void)size;
  Serial.print("[UI] ");
  Serial.println(text);
}

void displayRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  (void)x; (void)y; (void)w; (void)h; (void)color;
}

void displayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  (void)x; (void)y; (void)w; (void)h; (void)color;
}

void displayCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  (void)x; (void)y; (void)r; (void)color;
}

void displayFillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  (void)x; (void)y; (void)r; (void)color;
}

void displayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
}

TouchPoint readTouch() {
  // Real touch driver goes here.
  // Return pressed=true plus actual x/y.
  return {false, 0, 0};
}

// ============================================================
// APP STATE
// ============================================================

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

unsigned long lastCarouselMs = 0;
unsigned long carouselIntervalMs = 10000;

bool motionWakeEnabled = true;
unsigned long screenTimeoutMs = 30UL * 60UL * 1000UL;
unsigned long lastMotionMs = 0;
bool screenAwake = true;

Preferences prefs;

// Weather state
float weatherTempF = NAN;
int weatherHumidity = -1;
float weatherHighF = NAN;
float weatherLowF = NAN;
int weatherCode = -1;
String weatherCondition = "Loading";
String weatherStatus = "Not loaded";
unsigned long lastWeatherFetchMs = 0;
const unsigned long weatherFetchIntervalMs = 20UL * 60UL * 1000UL; // 20 minutes

// Spotify state
String spotifyArtist = "";
String spotifyTrack = "";
String spotifyStatus = "Not authenticated";
unsigned long lastSpotifyFetchMs = 0;
const unsigned long spotifyFetchIntervalMs = 3000;

// Timer state
int timerHours = 0;
int timerMinutes = 5;
int timerSeconds = 0;
long timerRemainingMs = 0;
bool timerRunning = false;
bool timerPaused = false;
unsigned long timerLastTickMs = 0;

// Stopwatch state
bool stopwatchRunning = false;
unsigned long stopwatchStartMs = 0;
unsigned long stopwatchAccumulatedMs = 0;
unsigned long stopwatchLapMs = 0;

// Render control
bool needsRedraw = true;
unsigned long lastSerialPagePrintMs = 0;

// ============================================================
// UTILITY
// ============================================================

bool inBox(int16_t x, int16_t y, int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

String twoDigits(int n) {
  if (n < 10) return "0" + String(n);
  return String(n);
}

String formatTimeMs(unsigned long ms, bool hundredths = false) {
  unsigned long totalSeconds = ms / 1000UL;
  unsigned long hours = totalSeconds / 3600UL;
  unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
  unsigned long seconds = totalSeconds % 60UL;
  unsigned long hundred = (ms % 1000UL) / 10UL;

  if (hours > 0) {
    String s = twoDigits(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(seconds);
    if (hundredths) s += "." + twoDigits(hundred);
    return s;
  }

  String s = twoDigits(minutes) + ":" + twoDigits(seconds);
  if (hundredths) s += "." + twoDigits(hundred);
  return s;
}

String weatherCodeToText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly Clear";
    case 2: return "Partly Cloudy";
    case 3: return "Cloudy";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Freezing Drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 66:
    case 67: return "Freezing Rain";
    case 71:
    case 73:
    case 75: return "Snow";
    case 77: return "Snow Grains";
    case 80:
    case 81:
    case 82: return "Rain Showers";
    case 85:
    case 86: return "Snow Showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Thunderstorm + Hail";
    default: return "Unknown";
  }
}

// ============================================================
// WIFI + TIME
// ============================================================

void connectWiFi() {
  Serial.print("[WIFI] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] Connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] Failed to connect.");
  }
}

void setupTime() {
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
  Serial.println("[TIME] NTP time setup started.");
}

String getClockString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--";

  int hour = timeinfo.tm_hour;
  String suffix = "AM";
  if (hour >= 12) suffix = "PM";
  hour %= 12;
  if (hour == 0) hour = 12;

  return String(hour) + ":" + twoDigits(timeinfo.tm_min) + " " + suffix;
}

String getDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Loading date";

  char buffer[40];
  strftime(buffer, sizeof(buffer), "%A, %B %d", &timeinfo);
  return String(buffer);
}

// ============================================================
// WEATHER API: Open-Meteo
// ============================================================

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherStatus = "Wi-Fi offline";
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Accept HTTPS cert without a stored root certificate for this starter project.

  HTTPClient http;

  String url = "https://api.open-meteo.com/v1/forecast?";
  url += "latitude=" + String(WEATHER_LAT, 4);
  url += "&longitude=" + String(WEATHER_LON, 4);
  url += "&current=temperature_2m,relative_humidity_2m,weather_code";
  url += "&daily=temperature_2m_max,temperature_2m_min";
  url += "&temperature_unit=fahrenheit";
  url += "&timezone=auto";

  Serial.print("[WEATHER] GET ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    weatherStatus = "HTTP begin failed";
    Serial.println("[WEATHER] HTTP begin failed");
    return;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    weatherStatus = "HTTP " + String(code);
    Serial.print("[WEATHER] HTTP error: ");
    Serial.println(code);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  // Size chosen for Open-Meteo current+daily response.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    weatherStatus = "JSON parse failed";
    Serial.print("[WEATHER] JSON error: ");
    Serial.println(err.c_str());
    return;
  }

  weatherTempF = doc["current"]["temperature_2m"] | NAN;
  weatherHumidity = doc["current"]["relative_humidity_2m"] | -1;
  weatherCode = doc["current"]["weather_code"] | -1;
  weatherHighF = doc["daily"]["temperature_2m_max"][0] | NAN;
  weatherLowF = doc["daily"]["temperature_2m_min"][0] | NAN;
  weatherCondition = weatherCodeToText(weatherCode);
  weatherStatus = "Online";
  lastWeatherFetchMs = millis();

  Serial.println("[WEATHER] Updated:");
  Serial.println("  Temp: " + String(weatherTempF, 1));
  Serial.println("  Condition: " + weatherCondition);
  Serial.println("  High: " + String(weatherHighF, 1));
  Serial.println("  Low: " + String(weatherLowF, 1));
  Serial.println("  Humidity: " + String(weatherHumidity));
}

// ============================================================
// SPOTIFY
// ============================================================

void setupSpotify() {
  Serial.println("[SPOTIFY] Starting SpotifyEsp32 auth flow...");
  Serial.println("[SPOTIFY] If this is your first run, watch Serial Monitor for auth instructions.");
  sp.begin();
  spotifyStatus = "Auth needed";
}

void updateSpotify() {
  if (WiFi.status() != WL_CONNECTED) {
    spotifyStatus = "Wi-Fi offline";
    return;
  }

  if (!sp.is_auth()) {
    sp.handle_client();
    spotifyStatus = "Auth needed";
    return;
  }

  if (millis() - lastSpotifyFetchMs < spotifyFetchIntervalMs) return;
  lastSpotifyFetchMs = millis();

  String currentArtist = sp.current_artist_names();
  String currentTrack = sp.current_track_name();

  if (currentArtist.length() > 0 && currentArtist != "Something went wrong" && currentArtist != "null") {
    spotifyArtist = currentArtist;
  }

  if (currentTrack.length() > 0 && currentTrack != "Something went wrong" && currentTrack != "null") {
    spotifyTrack = currentTrack;
  }

  spotifyStatus = "Connected";
}

// ============================================================
// SETTINGS STORAGE
// ============================================================

void loadSettings() {
  prefs.begin("deskhub", false);
  carouselIntervalMs = prefs.getULong("carousel", 10000);
  screenTimeoutMs = prefs.getULong("timeout", 30UL * 60UL * 1000UL);
  motionWakeEnabled = prefs.getBool("motion", true);
  prefs.end();
}

void saveSettings() {
  prefs.begin("deskhub", false);
  prefs.putULong("carousel", carouselIntervalMs);
  prefs.putULong("timeout", screenTimeoutMs);
  prefs.putBool("motion", motionWakeEnabled);
  prefs.end();
  Serial.println("[SETTINGS] Saved.");
}

// ============================================================
// TIMER + STOPWATCH LOGIC
// ============================================================

void startTimer() {
  if (timerRemainingMs <= 0) {
    timerRemainingMs = ((long)timerHours * 3600L + (long)timerMinutes * 60L + timerSeconds) * 1000L;
  }
  timerRunning = true;
  timerPaused = false;
  timerLastTickMs = millis();
  needsRedraw = true;
}

void pauseTimer() {
  timerRunning = false;
  timerPaused = true;
  needsRedraw = true;
}

void cancelTimer() {
  timerRunning = false;
  timerPaused = false;
  timerRemainingMs = ((long)timerHours * 3600L + (long)timerMinutes * 60L + timerSeconds) * 1000L;
  needsRedraw = true;
}

void updateTimer() {
  if (!timerRunning) return;

  unsigned long now = millis();
  unsigned long delta = now - timerLastTickMs;
  timerLastTickMs = now;

  timerRemainingMs -= (long)delta;
  if (timerRemainingMs <= 0) {
    timerRemainingMs = 0;
    timerRunning = false;
    timerPaused = false;
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
  stopwatchRunning = false;
  stopwatchStartMs = 0;
  stopwatchAccumulatedMs = 0;
  stopwatchLapMs = 0;
  needsRedraw = true;
}

unsigned long currentStopwatchMs() {
  if (stopwatchRunning) {
    return stopwatchAccumulatedMs + (millis() - stopwatchStartMs);
  }
  return stopwatchAccumulatedMs;
}

// ============================================================
// UI DRAW HELPERS
// ============================================================

void drawPageDots(int activePage) {
  int total = PAGE_COUNT;
  int spacing = 22;
  int startX = SCREEN_WIDTH / 2 - ((total - 1) * spacing) / 2;
  int y = SCREEN_HEIGHT - 45;

  for (int i = 0; i < total; i++) {
    uint16_t color = (i == activePage) ? C_BLUE : C_DIM;
    displayFillCircle(startX + i * spacing, y, 5, color);
  }
}

void drawGearButton() {
  // Real display: draw gear icon. Placeholder prints only.
  displayText(SCREEN_WIDTH - 78, 32, "[gear]", C_TEXT, 1);
}

void drawTopLine() {
  displayLine(300, 45, 500, 45, C_DIM);
  displayFillCircle(400, 45, 4, C_BLUE);
}

void drawPill(int x, int y, int w, int h, const String& text, uint16_t iconColor = C_BLUE) {
  displayFillRect(x, y, w, h, C_PANEL);
  displayRect(x, y, w, h, C_PANEL2);
  displayText(x + 22, y + 18, text, C_TEXT, 2);
  (void)iconColor;
}

void drawSegmentedControl(bool timerSelected) {
  displayFillRect(280, 75, 240, 42, C_PANEL);
  if (timerSelected) {
    displayFillRect(280, 75, 120, 42, C_PANEL2);
    displayText(317, 87, "Timer", C_TEXT, 2);
    displayText(430, 87, "Stopwatch", C_MUTED, 2);
  } else {
    displayFillRect(400, 75, 120, 42, C_PANEL2);
    displayText(317, 87, "Timer", C_MUTED, 2);
    displayText(420, 87, "Stopwatch", C_TEXT, 2);
  }
}

// ============================================================
// UI PAGES
// ============================================================

void drawClockPage() {
  displayClear();
  drawGearButton();

  String timeStr = getClockString();
  String dateStr = getDateString();

  displayText(285, 170, timeStr, C_TEXT, 6);
  displayText(285, 245, dateStr, C_TEXT, 3);
  drawPageDots(PAGE_CLOCK);

  Serial.println("----- PAGE: CLOCK -----");
  Serial.println(timeStr);
  Serial.println(dateStr);
}

void drawWeatherPage() {
  displayClear();
  drawTopLine();
  drawGearButton();

  displayText(330, 85, "WEATHER", C_TEXT, 3);

  String tempText = isnan(weatherTempF) ? "--°F" : String((int)round(weatherTempF)) + "°F";
  String highText = isnan(weatherHighF) ? "High --°" : "High " + String((int)round(weatherHighF)) + "°";
  String lowText = isnan(weatherLowF) ? "Low --°" : "Low " + String((int)round(weatherLowF)) + "°";
  String humText = weatherHumidity < 0 ? "Humidity --%" : "Humidity " + String(weatherHumidity) + "%";

  displayText(365, 155, tempText, C_TEXT, 6);
  displayText(340, 245, weatherCondition, C_TEXT, 3);
  displayText(350, 285, "Cary, NC", C_MUTED, 2);

  drawPill(85, 365, 190, 52, highText);
  drawPill(305, 365, 190, 52, lowText);
  drawPill(525, 365, 210, 52, humText);

  drawPageDots(PAGE_WEATHER);

  Serial.println("----- PAGE: WEATHER -----");
  Serial.println(tempText + " " + weatherCondition + " " + humText);
  Serial.println(weatherStatus);
}

void drawSpotifyPage() {
  displayClear();
  drawTopLine();
  drawGearButton();

  displayText(345, 80, "SPOTIFY", C_TEXT, 3);

  String track = spotifyTrack.length() ? spotifyTrack : "No track";
  String artist = spotifyArtist.length() ? spotifyArtist : "No artist";

  // Album art placeholder
  displayFillRect(80, 135, 210, 150, C_PANEL2);
  displayText(112, 197, "Album Art", C_MUTED, 2);

  displayText(360, 145, track, C_TEXT, 4);
  displayText(360, 195, artist, C_TEXT, 3);
  displayText(360, 235, "Now Playing", C_MUTED, 2);

  // Playback buttons placeholders
  displayText(375, 300, "|<", C_TEXT, 3);
  displayText(475, 300, "||", C_TEXT, 3);
  displayText(575, 300, ">|", C_TEXT, 3);

  // Progress bar
  displayLine(90, 390, 650, 390, C_DIM);
  displayLine(90, 390, 300, 390, C_BLUE);
  displayFillCircle(300, 390, 6, C_BLUE);
  displayText(90, 405, "1:24", C_MUTED, 2);
  displayText(620, 405, "3:20", C_MUTED, 2);

  // Vertical volume slider
  displayText(720, 125, "vol", C_MUTED, 1);
  displayLine(730, 155, 730, 330, C_DIM);
  displayLine(730, 240, 730, 330, C_BLUE);
  displayFillCircle(730, 240, 7, C_BLUE);

  drawPageDots(PAGE_SPOTIFY);

  Serial.println("----- PAGE: SPOTIFY -----");
  Serial.println(track + " - " + artist);
  Serial.println(spotifyStatus);
}

void drawStatusPage() {
  displayClear();
  drawTopLine();
  drawGearButton();

  displayText(260, 80, "SYSTEM STATUS", C_TEXT, 3);

  drawPill(120, 145, 560, 45, "Motion Sensor: " + String(motionWakeEnabled ? "Active" : "Off"));
  drawPill(120, 200, 560, 45, "Screen Timeout: " + String(screenTimeoutMs / 60000UL) + " min");
  drawPill(120, 255, 560, 45, "Wi-Fi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Offline"));
  drawPill(120, 310, 560, 45, "Weather API: " + weatherStatus);
  drawPill(120, 365, 560, 45, "Spotify API: " + spotifyStatus);

  drawPageDots(PAGE_STATUS);

  Serial.println("----- PAGE: SYSTEM STATUS -----");
}

void drawTimerPage() {
  displayClear();
  drawTopLine();
  drawGearButton();
  drawSegmentedControl(true);

  String selected = twoDigits(timerHours) + " : " + twoDigits(timerMinutes) + " : " + twoDigits(timerSeconds);
  String remaining = formatTimeMs(timerRemainingMs > 0 ? timerRemainingMs : ((long)timerHours * 3600L + timerMinutes * 60L + timerSeconds) * 1000L);

  displayText(295, 150, selected, C_TEXT, 5);
  displayText(245, 215, "HOURS      MINUTES     SECONDS", C_MUTED, 2);
  displayText(365, 260, "Set", C_BLUE, 2);

  if (timerRunning || timerPaused) {
    displayText(320, 310, remaining, C_TEXT, 4);
  }

  String left = (timerRunning || timerPaused) ? "Cancel" : "Cancel";
  String right = timerRunning ? "Pause" : "Start";

  displayText(250, 385, left, C_TEXT, 3);
  displayText(480, 385, right, C_TEXT, 3);

  drawPageDots(PAGE_TIMER);

  Serial.println("----- PAGE: TIMER -----");
  Serial.println("Selected: " + selected + " Remaining: " + remaining);
}

void drawStopwatchPage() {
  displayClear();
  drawTopLine();
  drawGearButton();
  drawSegmentedControl(false);

  unsigned long sw = currentStopwatchMs();
  String swText = formatTimeMs(sw, true);

  displayText(250, 165, swText, C_TEXT, 6);
  displayText(310, 245, "Lap 1     " + swText, C_BLUE2, 2);

  displayText(245, 365, "Reset", C_TEXT, 3);
  displayText(500, 365, stopwatchRunning ? "Pause" : "Start", C_TEXT, 3);

  drawPageDots(PAGE_STOPWATCH);

  Serial.println("----- PAGE: STOPWATCH -----");
  Serial.println(swText);
}

void drawSettingsScreen() {
  displayClear();

  displayText(310, 50, "SETTINGS", C_TEXT, 3);
  displayText(700, 50, "[gear]", C_BLUE, 1);

  drawPill(120, 105, 560, 42, "Brightness: 70%");
  drawPill(120, 155, 560, 42, "Carousel Rotation: " + String(carouselIntervalMs / 1000UL) + " sec");
  drawPill(120, 205, 560, 42, "Motion Wake: " + String(motionWakeEnabled ? "On" : "Off"));
  drawPill(120, 255, 560, 42, "Screen Timeout: " + String(screenTimeoutMs / 60000UL) + " min");
  drawPill(120, 305, 560, 42, "Theme: Dark");
  drawPill(120, 355, 560, 42, "Wi-Fi: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Offline"));
  drawPill(120, 405, 560, 42, "Spotify: " + spotifyStatus);

  displayFillRect(280, 450, 240, 45, C_PANEL2);
  displayText(322, 462, "Save & Close", C_BLUE2, 2);

  Serial.println("----- SCREEN: SETTINGS -----");
}

void drawCurrentScreen() {
  if (!screenAwake) {
    displayClear(C_BLACK);
    return;
  }

  if (settingsOpen) {
    drawSettingsScreen();
    return;
  }

  switch (currentPage) {
    case PAGE_CLOCK: drawClockPage(); break;
    case PAGE_WEATHER: drawWeatherPage(); break;
    case PAGE_SPOTIFY: drawSpotifyPage(); break;
    case PAGE_STATUS: drawStatusPage(); break;
    case PAGE_TIMER: drawTimerPage(); break;
    case PAGE_STOPWATCH: drawStopwatchPage(); break;
    default: drawClockPage(); break;
  }

  needsRedraw = false;
}

// ============================================================
// INPUT HANDLING
// ============================================================

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
#if TOUCH_ENABLED
  TouchPoint t = readTouch();
  if (!t.pressed) return;

  lastMotionMs = millis();
  screenAwake = true;

  // Gear button area
  if (inBox(t.x, t.y, SCREEN_WIDTH - 90, 15, 75, 75)) {
    settingsOpen = true;
    needsRedraw = true;
    return;
  }

  if (settingsOpen) {
    // Save & Close button
    if (inBox(t.x, t.y, 280, 430, 240, 60)) {
      saveSettings();
      settingsOpen = false;
      needsRedraw = true;
    }
    return;
  }

  // Very simple page swipe/tap zones until real gestures are added
  if (t.x < 80) prevPage();
  if (t.x > SCREEN_WIDTH - 80) nextPage();

  // Timer buttons
  if (currentPage == PAGE_TIMER) {
    if (inBox(t.x, t.y, 220, 340, 150, 110)) {
      cancelTimer();
    }
    if (inBox(t.x, t.y, 440, 340, 150, 110)) {
      if (timerRunning) pauseTimer();
      else startTimer();
    }
  }

  // Stopwatch buttons
  if (currentPage == PAGE_STOPWATCH) {
    if (inBox(t.x, t.y, 210, 330, 170, 120)) {
      resetStopwatch();
    }
    if (inBox(t.x, t.y, 440, 330, 170, 120)) {
      if (stopwatchRunning) pauseStopwatch();
      else startStopwatch();
    }
  }
#endif
}

void handleMotionSensor() {
  if (!motionWakeEnabled) return;

  int motion = digitalRead(PIR_PIN);
  if (motion == HIGH) {
    lastMotionMs = millis();
    if (!screenAwake) {
      screenAwake = true;
      needsRedraw = true;
      Serial.println("[MOTION] Wake screen.");
    }
  }

  if (screenAwake && millis() - lastMotionMs > screenTimeoutMs) {
    screenAwake = false;
    needsRedraw = true;
    Serial.println("[SCREEN] Timed out.");
  }
}

// ============================================================
// ARDUINO SETUP + LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=================================");
  Serial.println("DeskHub All-In-One Starting");
  Serial.println("=================================");

  pinMode(PIR_PIN, INPUT);
  lastMotionMs = millis();

  loadSettings();
  displayBegin();

  connectWiFi();
  setupTime();

  fetchWeather();
  setupSpotify();

  timerRemainingMs = ((long)timerHours * 3600L + (long)timerMinutes * 60L + timerSeconds) * 1000L;

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

  // Auto carousel only when settings is closed and screen is awake.
  if (!settingsOpen && screenAwake && millis() - lastCarouselMs > carouselIntervalMs) {
    nextPage();
  }

  // Redraw periodically so clock/stopwatch updates on placeholder serial too.
  bool periodicRedraw = false;
  if (millis() - lastSerialPagePrintMs > 1000) {
    lastSerialPagePrintMs = millis();
    if (currentPage == PAGE_CLOCK || currentPage == PAGE_TIMER || currentPage == PAGE_STOPWATCH) {
      periodicRedraw = true;
    }
  }

  if (needsRedraw || periodicRedraw) {
    drawCurrentScreen();
  }

  delay(20);
}
