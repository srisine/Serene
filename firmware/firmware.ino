#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#define WIFI_SSID "<Your WIFI name here>"
#define WIFI_PASS "<YOur WIFI pass here>"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define SNOOZE_MINUTES 5
#define ALARM_TIMEOUT_S 120

#define TFT_SCLK D0
#define TFT_MOSI D1
#define TFT_RST D2
#define TFT_DC D3
#define TFT_CS D4
#define TFT_BL D5
#define KEY_PIN_1 D6
#define KEY_PIN_2 D8
#define KEY_PIN_3 D9
#define KEY_PIN_4 D10
#define BUZZER_PIN D7
#define BL_ACTIVE_LOW 1

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

const int SCR_W = 284;
const int SCR_H = 76;
const int NSTARS = 36;
const bool WIFI_ON = sizeof(WIFI_SSID) > 1;

const uint16_t C_BG = RGB(7, 9, 15);
const uint16_t C_PANEL = RGB(19, 25, 40);
const uint16_t C_LINE = RGB(38, 48, 70);
const uint16_t C_DIM = RGB(70, 82, 106);
const uint16_t C_MUTED = RGB(122, 136, 162);
const uint16_t C_TEXT = RGB(236, 242, 252);
const uint16_t C_ACCENT = RGB(0, 198, 255);
const uint16_t C_AMBER = RGB(255, 176, 46);
const uint16_t C_GOOD = RGB(70, 225, 140);

const GFXfont *const F_XL = &FreeSansBold24pt7b;
const GFXfont *const F_L = &FreeSansBold18pt7b;
const GFXfont *const F_M = &FreeSansBold12pt7b;
const GFXfont *const F_S = &FreeSansBold9pt7b;
const GFXfont *const F_R = &FreeSans9pt7b;

class Panel : public Adafruit_ST7789 {
 public:
  Panel(SPIClass *spi, int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7789(spi, cs, dc, rst) {}
  void setOffsets(uint8_t col, uint8_t row) {
    _colstart = _colstart2 = col;
    _rowstart = _rowstart2 = row;
  }
};

struct Note {
  uint16_t f;
  uint16_t ms;
};

struct Star {
  float x;
  float v;
  int16_t y;
  uint8_t b;
};

struct Key {
  uint8_t pin;
  bool repeatable;
  bool raw;
  bool down;
  bool longDone;
  bool repeated;
  bool mute;
  uint32_t edge;
  uint32_t downAt;
  uint32_t lastRep;
  uint16_t reps;
};

struct Config {
  uint8_t alarmHour;
  uint8_t alarmMin;
  uint8_t alarmRepeat;
  uint8_t alarmOn;
  uint8_t use24h;
  uint8_t bright;
  uint8_t saver;
  uint8_t keySound;
  int16_t utcMin;
  uint16_t timerSec;
};

enum Screen : uint8_t { S_CLOCK, S_MENU, S_ALARM, S_STOPWATCH, S_TIMER, S_SAVER, S_SETTINGS, S_SETCLOCK };
enum Ring : uint8_t { RING_NONE, RING_ALARM, RING_TIMER };
enum TState : uint8_t { T_SET, T_RUN, T_PAUSE, T_DONE };
enum Ev : uint8_t { EV_NONE, EV_DOWN, EV_PRESS, EV_LONG, EV_REPEAT };
enum KeyId : uint8_t { K_BACK, K_PREV, K_NEXT, K_OK };

const char *const DAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *const MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const char *const REPEATS[4] = {"ONCE", "DAILY", "WEEKDAYS", "WEEKENDS"};
const char *const MENU_NAMES[6] = {"CLOCK", "ALARM", "STOPWATCH", "TIMER", "SCREENSAVER", "SETTINGS"};
const char *const SET_NAMES[6] = {"TIME FORMAT", "UTC OFFSET", "BRIGHTNESS", "SCREENSAVER", "KEY SOUND", "SET CLOCK"};
const char *const SAVER_NAMES[6] = {"OFF", "30 SEC", "1 MIN", "2 MIN", "5 MIN", "10 MIN"};
const uint16_t SAVER_SECS[6] = {0, 30, 60, 120, 300, 600};
const uint8_t BRIGHT[5] = {18, 55, 110, 180, 255};

const Note SND_BOOT[] = {{880, 90}, {0, 20}, {1175, 90}, {0, 20}, {1760, 170}};
const Note SND_CLICK[] = {{2600, 10}};
const Note SND_ALARM[] = {{3000, 100}, {0, 100}, {3000, 100}, {0, 700}};
const Note SND_ALARM_FAST[] = {{3200, 80}, {0, 60}, {3200, 80}, {0, 60}, {3200, 80}, {0, 60}, {3200, 80}, {0, 350}};
const Note SND_TIMER[] = {{2400, 120}, {0, 80}, {3000, 120}, {0, 80}, {2400, 120}, {0, 80}, {3000, 120}, {0, 600}};

Panel tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 cv(SCR_W, SCR_H);
Preferences prefs;
Config cfg;

Key keys[4] = {{KEY_PIN_1, false}, {KEY_PIN_2, true}, {KEY_PIN_3, true}, {KEY_PIN_4, false}};
uint8_t ev[4];

Screen screen = S_CLOCK;
Ring ring = RING_NONE;
TState tState = T_SET;

struct tm nowTm;
time_t nowEpoch = 0;
bool snoozing = false;
time_t snoozeUntil = 0;
uint32_t lastFiredMinute = 0;
uint32_t ringStart = 0;
bool ringFast = false;

int menuSel = 0;
float menuSlide = 0;
int alarmField = 0;
int setSel = 0;
int setTop = 0;
bool setEdit = false;
int scField = 0;
int scF[5];

bool swRunning = false;
uint32_t swStart = 0, swBase = 0, swLapMark = 0, swLast = 0, swBest = 0;
uint16_t swLaps = 0;
uint32_t tEnd = 0, tRemain = 0, tTotal = 0;

Star stars[NSTARS];
float bx = 20, by = 10, vx = 0.7f, vy = 0.45f;

uint32_t lastActivity = 0;
uint32_t lastFrame = 0;
bool dirty = false;
uint32_t dirtyAt = 0;
uint8_t blCur = 0, blTarget = 0;
volatile bool ntpSynced = false;
bool ntpStarted = false;
char tzBuf[16];

const Note *sndSeq = nullptr;
uint8_t sndLen = 0, sndIdx = 0;
bool sndLoop = false;
uint32_t sndNext = 0;

#define PLAY(seq, loop) soundPlay(seq, sizeof(seq) / sizeof(seq[0]), loop)

void pwmAttach(uint8_t pin, uint8_t ch, uint32_t freq, uint8_t res) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(pin, freq, res, ch);
#else
  ledcSetup(ch, freq, res);
  ledcAttachPin(pin, ch);
#endif
}

void pwmWrite(uint8_t pin, uint8_t ch, uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(ch, duty);
#endif
}

void pwmTone(uint8_t pin, uint8_t ch, uint32_t freq) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(pin, freq);
#else
  ledcWriteTone(ch, freq);
#endif
}

void soundStop() {
  sndSeq = nullptr;
  pwmTone(BUZZER_PIN, 2, 0);
}

void soundPlay(const Note *s, uint8_t n, bool loop) {
  sndSeq = s;
  sndLen = n;
  sndIdx = 0;
  sndLoop = loop;
  sndNext = millis();
}

void soundUpdate() {
  if (!sndSeq) return;
  uint32_t now = millis();
  if ((int32_t)(now - sndNext) < 0) return;
  if (sndIdx >= sndLen) {
    if (sndLoop) {
      sndIdx = 0;
    } else {
      soundStop();
      return;
    }
  }
  const Note &n = sndSeq[sndIdx++];
  pwmTone(BUZZER_PIN, 2, n.f);
  sndNext = now + n.ms;
}

void click() {
  if (cfg.keySound && ring == RING_NONE && !sndSeq) PLAY(SND_CLICK, false);
}

uint16_t mix(uint16_t a, uint16_t b, uint8_t t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + ((br - ar) * t) / 255;
  int g = ag + ((bg - ag) * t) / 255;
  int l = ab + ((bb - ab) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | l);
}

uint16_t hue565(uint32_t h) {
  h %= 768;
  uint8_t x = h & 255, r, g, b;
  switch (h >> 8) {
    case 0: r = 255; g = x; b = 0; break;
    case 1: r = 0; g = 255; b = x; break;
    default: r = x; g = 0; b = 255; break;
  }
  return RGB(r, g, b);
}

int textW(const GFXfont *f, const char *s) {
  int w = 0;
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c >= f->first && c <= f->last) w += f->glyph[c - f->first].xAdvance;
  }
  return w;
}

int digitH(const GFXfont *f) {
  return f->glyph['0' - f->first].height;
}

int smallW(const char *s, int sp) {
  int n = (int)strlen(s);
  return n ? n * (5 + sp) - sp : 0;
}

void drawText(const GFXfont *f, int x, int base, const char *s, uint16_t c) {
  cv.setFont(f);
  cv.setTextColor(c);
  cv.setCursor(x, base);
  cv.print(s);
}

void drawTextC(const GFXfont *f, int cx, int base, const char *s, uint16_t c) {
  drawText(f, cx - textW(f, s) / 2, base, s, c);
}

void drawTextR(const GFXfont *f, int rx, int base, const char *s, uint16_t c) {
  drawText(f, rx - textW(f, s), base, s, c);
}

void drawSmall(int x, int y, const char *s, uint16_t c, int sp) {
  cv.setFont(nullptr);
  for (; *s; s++, x += 5 + sp) cv.drawChar(x, y, *s, c, c, 1);
}

void drawSmallC(int cx, int y, const char *s, uint16_t c, int sp) {
  drawSmall(cx - smallW(s, sp) / 2, y, s, c, sp);
}

int hour12(int h) {
  int x = h % 12;
  return x == 0 ? 12 : x;
}

int daysIn(int y, int m) {
  static const uint8_t d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return d[m - 1];
}

uint32_t minuteKey() {
  return (uint32_t)(nowEpoch / 60);
}

void markDirty() {
  dirty = true;
  dirtyAt = millis();
}

void loadConfig() {
  cfg.alarmHour = prefs.getUChar("ah", 7);
  cfg.alarmMin = prefs.getUChar("am", 0);
  cfg.alarmRepeat = prefs.getUChar("ar", 2);
  cfg.alarmOn = prefs.getUChar("ao", 0);
  cfg.use24h = prefs.getUChar("h24", 1);
  cfg.bright = prefs.getUChar("br", 3);
  cfg.saver = prefs.getUChar("ss", 2);
  cfg.keySound = prefs.getUChar("ks", 1);
  cfg.utcMin = prefs.getShort("utc", 0);
  cfg.timerSec = prefs.getUShort("ts", 300);
  if (cfg.alarmHour > 23) cfg.alarmHour = 7;
  if (cfg.alarmMin > 59) cfg.alarmMin = 0;
  if (cfg.alarmRepeat > 3) cfg.alarmRepeat = 2;
  if (cfg.bright > 4) cfg.bright = 3;
  if (cfg.saver > 5) cfg.saver = 2;
  if (cfg.timerSec < 15 || cfg.timerSec > 5999) cfg.timerSec = 300;
}

void saveConfig() {
  prefs.putUChar("ah", cfg.alarmHour);
  prefs.putUChar("am", cfg.alarmMin);
  prefs.putUChar("ar", cfg.alarmRepeat);
  prefs.putUChar("ao", cfg.alarmOn);
  prefs.putUChar("h24", cfg.use24h);
  prefs.putUChar("br", cfg.bright);
  prefs.putUChar("ss", cfg.saver);
  prefs.putUChar("ks", cfg.keySound);
  prefs.putShort("utc", cfg.utcMin);
  prefs.putUShort("ts", cfg.timerSec);
  dirty = false;
}

void persistUpdate() {
  if (dirty && millis() - dirtyAt > 1500) saveConfig();
}

void applyTz() {
  int m = cfg.utcMin;
  int a = m < 0 ? -m : m;
  snprintf(tzBuf, sizeof(tzBuf), "UTC%c%02d:%02d", m > 0 ? '-' : '+', a / 60, a % 60);
  setenv("TZ", tzBuf, 1);
  tzset();
}

void changeUtc(int v) {
  int old = cfg.utcMin;
  cfg.utcMin = (int16_t)v;
  if (!ntpSynced) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    tv.tv_sec += (old - v) * 60;
    settimeofday(&tv, nullptr);
  }
  applyTz();
}

void seedClock() {
  if (time(nullptr) > 1700000000) return;
  char mon[4] = "Jan";
  int d = 1, y = 2026, hh = 0, mm = 0, ss = 0;
  sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  const char *names = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *p = strstr(names, mon);
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = p ? (int)(p - names) / 3 : 0;
  t.tm_mday = d;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  t.tm_isdst = -1;
  struct timeval tv = {mktime(&t), 0};
  settimeofday(&tv, nullptr);
}

void refreshTime() {
  nowEpoch = time(nullptr);
  localtime_r(&nowEpoch, &nowTm);
}

void onNtpSync(struct timeval *tv) {
  ntpSynced = true;
}

void wifiBegin() {
  if (!WIFI_ON) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  sntp_set_time_sync_notification_cb(onNtpSync);
}

void wifiUpdate() {
  if (!WIFI_ON || ntpStarted) return;
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(tzBuf, NTP_SERVER_1, NTP_SERVER_2);
    ntpStarted = true;
  }
}

bool repeatMatches(uint8_t mode, int wday) {
  switch (mode) {
    case 2: return wday >= 1 && wday <= 5;
    case 3: return wday == 0 || wday == 6;
    default: return true;
  }
}

time_t nextAlarmTime() {
  if (!cfg.alarmOn) return 0;
  for (int d = 0; d < 8; d++) {
    struct tm c = nowTm;
    c.tm_mday += d;
    c.tm_hour = cfg.alarmHour;
    c.tm_min = cfg.alarmMin;
    c.tm_sec = 0;
    c.tm_isdst = -1;
    time_t e = mktime(&c);
    if (e > nowEpoch && repeatMatches(cfg.alarmRepeat, c.tm_wday)) return e;
  }
  return 0;
}

bool nextAlarmIn(char *out, size_t n) {
  time_t t = snoozing ? snoozeUntil : 0;
  time_t na = nextAlarmTime();
  if (na && (!t || na < t)) t = na;
  if (!t) return false;
  long s = (long)(t - nowEpoch);
  if (s < 0) s = 0;
  long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  if (d > 0) snprintf(out, n, "IN %ldD %ldH", d, h);
  else if (h > 0) snprintf(out, n, "IN %ldH %ldM", h, m);
  else if (m > 0) snprintf(out, n, "IN %ldM", m);
  else snprintf(out, n, "IN <1M");
  return true;
}

void alarmTouched() {
  cfg.alarmOn = 1;
  snoozing = false;
  lastFiredMinute = minuteKey();
}

uint32_t swElapsed() {
  return swRunning ? swBase + (millis() - swStart) : swBase;
}

uint32_t timerRemainMs() {
  switch (tState) {
    case T_RUN: {
      int32_t d = (int32_t)(tEnd - millis());
      return d > 0 ? (uint32_t)d : 0;
    }
    case T_PAUSE: return tRemain;
    case T_SET: return (uint32_t)cfg.timerSec * 1000UL;
    default: return 0;
  }
}

void forceRedraw() {
  lastFrame = 0;
}

void initStars() {
  for (int i = 0; i < NSTARS; i++) {
    stars[i].x = (float)random(SCR_W);
    stars[i].y = (int16_t)random(SCR_H);
    stars[i].v = 0.15f + random(100) / 60.0f;
    stars[i].b = (uint8_t)(70 + random(150));
  }
}

void goScreen(Screen s) {
  screen = s;
  lastActivity = millis();
  forceRedraw();
  if (s == S_ALARM) alarmField = 0;
  if (s == S_SETTINGS) setEdit = false;
  if (s == S_SETCLOCK) {
    scF[0] = nowTm.tm_hour;
    scF[1] = nowTm.tm_min;
    scF[2] = nowTm.tm_mday;
    scF[3] = nowTm.tm_mon + 1;
    scF[4] = nowTm.tm_year + 1900;
    scField = 0;
  }
}

void startRing(Ring r) {
  ring = r;
  ringStart = millis();
  ringFast = false;
  if (screen == S_SAVER) goScreen(S_CLOCK);
  if (r == RING_ALARM) PLAY(SND_ALARM, true);
  else PLAY(SND_TIMER, true);
  forceRedraw();
}

void stopRing() {
  ring = RING_NONE;
  soundStop();
  lastActivity = millis();
  forceRedraw();
}

void checkAlarm() {
  if (ring == RING_ALARM) return;
  if (snoozing && nowEpoch >= snoozeUntil) {
    snoozing = false;
    startRing(RING_ALARM);
    return;
  }
  if (cfg.alarmOn && nowTm.tm_hour == cfg.alarmHour && nowTm.tm_min == cfg.alarmMin && repeatMatches(cfg.alarmRepeat, nowTm.tm_wday)) {
    uint32_t k = minuteKey();
    if (k != lastFiredMinute) {
      lastFiredMinute = k;
      if (cfg.alarmRepeat == 0) {
        cfg.alarmOn = 0;
        markDirty();
      }
      startRing(RING_ALARM);
    }
  }
}

void updateTimer() {
  if (tState == T_RUN && (int32_t)(millis() - tEnd) >= 0) {
    tState = T_DONE;
    startRing(RING_TIMER);
  }
}

void updateRing() {
  if (ring == RING_NONE) return;
  uint32_t el = millis() - ringStart;
  if (ring == RING_ALARM) {
    if (!ringFast && el > 25000UL) {
      ringFast = true;
      PLAY(SND_ALARM_FAST, true);
    }
    if (el > (uint32_t)ALARM_TIMEOUT_S * 1000UL) stopRing();
  } else if (el > 60000UL) {
    tState = T_SET;
    stopRing();
  }
}

void idleLogic() {
  if (ring != RING_NONE) {
    lastActivity = millis();
    return;
  }
  uint32_t idle = millis() - lastActivity;
  if (screen == S_CLOCK) {
    uint32_t to = (uint32_t)SAVER_SECS[cfg.saver] * 1000UL;
    if (to && idle > to) goScreen(S_SAVER);
  } else if (screen != S_SAVER) {
    bool busy = (screen == S_STOPWATCH && swRunning) || (screen == S_TIMER && tState == T_RUN);
    if (!busy && idle > 45000UL) goScreen(S_CLOCK);
  }
}

void blUpdate() {
  uint8_t lvl = BRIGHT[cfg.bright];
  if (screen == S_SAVER && ring == RING_NONE && lvl > 30) lvl = 30;
  blTarget = lvl;
  if (blCur == blTarget) return;
  int d = (int)blTarget - (int)blCur;
  if (d > 4) d = 4;
  if (d < -4) d = -4;
  blCur = (uint8_t)(blCur + d);
  pwmWrite(TFT_BL, 0, BL_ACTIVE_LOW ? 255 - blCur : blCur);
}

void drawBellMini(int x, int y, uint16_t c) {
  cv.fillCircle(x + 4, y + 3, 3, c);
  cv.fillRect(x + 1, y + 3, 7, 4, c);
  cv.fillRect(x, y + 6, 9, 2, c);
  cv.drawPixel(x + 4, y + 9, c);
}

void drawStatus() {
  int x = SCR_W - 6;
  if (WIFI_ON) {
    uint16_t c = ntpSynced ? C_GOOD : (WiFi.status() == WL_CONNECTED ? C_AMBER : C_DIM);
    x -= 8;
    for (int i = 0; i < 3; i++) cv.fillRect(x + i * 3, 10 - (3 + 2 * i), 2, 3 + 2 * i, c);
    x -= 8;
  }
  if (cfg.alarmOn || snoozing) {
    x -= 9;
    drawBellMini(x, 2, snoozing ? C_AMBER : C_ACCENT);
    x -= 8;
  }
  if ((tState == T_RUN || tState == T_PAUSE) && screen != S_TIMER) {
    char b[10];
    int s = (int)((timerRemainMs() + 999UL) / 1000UL);
    snprintf(b, sizeof(b), "%02d:%02d", s / 60, s % 60);
    x -= smallW(b, 1);
    drawSmall(x, 3, b, tState == T_RUN ? C_AMBER : C_DIM, 1);
    x -= 10;
    cv.fillTriangle(x, 3, x + 6, 3, x + 3, 6, C_AMBER);
    cv.fillTriangle(x, 10, x + 6, 10, x + 3, 7, C_AMBER);
    x -= 8;
  }
  if (swRunning && screen != S_STOPWATCH) {
    x -= smallW("SW", 1);
    drawSmall(x, 3, "SW", C_GOOD, 1);
  }
}

void drawHeader(const char *title, uint16_t col, const char *sub, uint16_t subCol) {
  drawSmall(6, 3, title, col, 2);
  if (sub) drawSmall(6 + smallW(title, 2) + 10, 3, sub, subCol, 1);
  drawStatus();
  cv.drawFastHLine(0, 12, SCR_W, C_LINE);
}

void drawKeyGlyph(int cx, int cy, char g, uint16_t c) {
  switch (g) {
    case '<': cv.fillTriangle(cx + 3, cy - 4, cx + 3, cy + 4, cx - 3, cy, c); break;
    case '>': cv.fillTriangle(cx - 3, cy - 4, cx - 3, cy + 4, cx + 3, cy, c); break;
    case '^': cv.fillTriangle(cx - 4, cy + 3, cx + 4, cy + 3, cx, cy - 3, c); break;
    case 'v': cv.fillTriangle(cx - 4, cy - 3, cx + 4, cy - 3, cx, cy + 3, c); break;
    case '+':
      cv.drawFastHLine(cx - 3, cy, 7, c);
      cv.drawFastVLine(cx, cy - 3, 7, c);
      break;
    case '-': cv.drawFastHLine(cx - 3, cy, 7, c); break;
  }
}

void drawKey(int slot, int span, const char *s, bool hot) {
  if (!s || !*s) return;
  int x = slot * 71 + 3, w = span * 71 - 6, y = 65, h = 11;
  uint16_t fg;
  if (hot) {
    cv.fillRoundRect(x, y, w, h, 4, C_ACCENT);
    fg = C_BG;
  } else {
    cv.drawRoundRect(x, y, w, h, 4, C_LINE);
    fg = C_MUTED;
  }
  int cx = x + w / 2;
  if (strlen(s) == 1 && strchr("<>^v+-", s[0])) drawKeyGlyph(cx, y + 5, s[0], hot ? C_BG : C_TEXT);
  else drawSmall(cx - smallW(s, 1) / 2, y + 2, s, fg, 1);
}

void drawKeys(const char *a, const char *b, const char *c, const char *d, int hot) {
  const char *l[4] = {a, b, c, d};
  for (int i = 0; i < 4; i++) drawKey(i, 1, l[i], hot == i);
}

void drawSwitch(int x, int y, bool on) {
  cv.fillRoundRect(x, y, 30, 14, 7, on ? C_GOOD : C_LINE);
  cv.fillCircle(on ? x + 23 : x + 7, y + 7, 5, C_TEXT);
}

void drawRing(int cx, int cy, int r, float frac, uint16_t col, uint16_t track) {
  const int N = 90;
  int lit = (int)(frac * N + 0.5f);
  for (int a = 0; a < N; a++) {
    float ang = -1.5708f + a * (6.28318f / N);
    int x = cx + (int)lroundf(cosf(ang) * r);
    int y = cy + (int)lroundf(sinf(ang) * r);
    cv.fillCircle(x, y, 2, a < lit ? col : track);
  }
}

void drawMenuIcon(int id, int cx, int cy, uint16_t c, uint16_t bg) {
  switch (id) {
    case 0:
      cv.drawCircle(cx, cy, 12, c);
      cv.drawCircle(cx, cy, 11, c);
      cv.drawLine(cx, cy, cx, cy - 8, c);
      cv.drawLine(cx, cy, cx + 6, cy + 3, c);
      cv.fillCircle(cx, cy, 2, c);
      break;
    case 1:
      cv.fillCircle(cx, cy - 3, 8, c);
      cv.fillRect(cx - 8, cy - 3, 17, 9, c);
      cv.fillRect(cx - 11, cy + 6, 23, 3, c);
      cv.fillCircle(cx, cy + 11, 2, c);
      cv.fillRect(cx - 1, cy - 13, 3, 3, c);
      break;
    case 2:
      cv.drawCircle(cx, cy + 2, 10, c);
      cv.drawCircle(cx, cy + 2, 9, c);
      cv.fillRect(cx - 3, cy - 12, 7, 3, c);
      cv.drawLine(cx + 8, cy - 6, cx + 11, cy - 9, c);
      cv.drawLine(cx + 9, cy - 6, cx + 12, cy - 9, c);
      cv.drawLine(cx, cy + 2, cx + 4, cy - 3, c);
      cv.drawLine(cx + 1, cy + 2, cx + 5, cy - 3, c);
      break;
    case 3:
      cv.fillRect(cx - 9, cy - 12, 19, 2, c);
      cv.fillRect(cx - 9, cy + 11, 19, 2, c);
      cv.drawTriangle(cx - 8, cy - 10, cx + 8, cy - 10, cx, cy, c);
      cv.drawTriangle(cx - 8, cy + 11, cx + 8, cy + 11, cx, cy, c);
      cv.fillTriangle(cx - 5, cy + 10, cx + 5, cy + 10, cx, cy + 4, c);
      cv.drawFastVLine(cx, cy - 6, 6, c);
      break;
    case 4:
      cv.fillCircle(cx - 2, cy, 10, c);
      cv.fillCircle(cx + 3, cy - 4, 9, bg);
      cv.drawFastHLine(cx + 6, cy + 6, 5, c);
      cv.drawFastVLine(cx + 8, cy + 4, 5, c);
      cv.fillCircle(cx + 10, cy - 8, 1, c);
      break;
    default: {
      const int8_t kx[3] = {-5, 5, -1};
      for (int i = 0; i < 3; i++) {
        int y = cy - 8 + i * 8;
        cv.drawFastHLine(cx - 11, y, 23, c);
        cv.fillCircle(cx + kx[i], y, 3, c);
        cv.fillCircle(cx + kx[i], y, 1, bg);
      }
    }
  }
}

int drawHM(const GFXfont *f, int x, int base, int h, int m, uint16_t cH, uint16_t cM, uint16_t cC, bool h24) {
  char b[4];
  int dw = textW(f, "0"), cw = textW(f, ":");
  int hh = h24 ? h : hour12(h);
  if (h24 || hh >= 10) {
    snprintf(b, sizeof(b), "%02d", hh);
    drawText(f, x, base, b, cH);
  } else {
    snprintf(b, sizeof(b), "%d", hh);
    drawText(f, x + dw, base, b, cH);
  }
  drawText(f, x + 2 * dw, base, ":", cC);
  snprintf(b, sizeof(b), "%02d", m);
  drawText(f, x + 2 * dw + cw, base, b, cM);
  return x + 4 * dw + cw;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
  tft.init(SCR_W, SCR_H);
  tft.setOffsets(82, 18);
  tft.setRotation(1);
  tft.fillScreen(C_BG);

  pinMode(KEY_PIN_1, INPUT_PULLUP);
  pinMode(KEY_PIN_2, INPUT_PULLUP);
  pinMode(KEY_PIN_3, INPUT_PULLUP);
  pinMode(KEY_PIN_4, INPUT_PULLUP);

  pwmAttach(BUZZER_PIN, 2, 2000, 8);
  
  prefs.begin("clock", false);
  loadConfig();
  applyTz();
  seedClock();
  wifiBegin();
  initStars();
  
  PLAY(SND_BOOT, false);
}

void loop() {
  uint32_t now = millis();
  refreshTime();
  wifiUpdate();
  updateRing();
  soundUpdate();
  persistUpdate();
  checkAlarm();
  updateTimer();
  idleLogic();
  blUpdate();

  // Rendering logic placeholder or loop updates
  delay(10);
}
