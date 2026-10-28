#include "wifi_sync.h"

#if ENABLE_WIFI
#include "accessory.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

// All network work runs in its own task. It never touches LVGL: results are handed to the main loop
// through wifi_sync_poll_weather(), which applies them to the UI.

enum WifiStatus : uint8_t { WS_OFF, WS_NO_CREDS, WS_HOST, WS_CONNECTING, WS_NO_LOCATION, WS_CONNECTED, WS_WEATHER_FAIL };
static volatile uint8_t status = WS_OFF;

static char ssid[33] = "";
static char pass[64] = "";
static float lat = 0, lon = 0;
static bool have_loc = false;
static bool enabled = false;
static volatile bool host_present = false;
static volatile bool creds_changed = false;
static volatile uint32_t next_weather = 0;

static WifiWeather pending;
static volatile bool pending_ready = false;
static volatile long tz_offset = 0; // seconds, from Open-Meteo "utc_offset_seconds" (includes daylight saving)
static bool sntp_started = false;

#define WEATHER_REFRESH_MS (15UL * 60UL * 1000UL)
#define WEATHER_RETRY_MS (60UL * 1000UL)

static void prefs_load() {
  Preferences p;
  p.begin("wifi", false);
  // isKey() first: reading a missing key logs an error on every first boot
  if (p.isKey("ssid")) p.getString("ssid", "").toCharArray(ssid, sizeof(ssid));
  if (p.isKey("pass")) p.getString("pass", "").toCharArray(pass, sizeof(pass));
  if (p.isKey("lat")) lat = p.getFloat("lat", 0);
  if (p.isKey("lon")) lon = p.getFloat("lon", 0);
  have_loc = p.isKey("loc") && p.getBool("loc", false);
  enabled = p.isKey("en") && p.getBool("en", false);
  p.end();
}

static void prefs_save() {
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.putFloat("lat", lat);
  p.putFloat("lon", lon);
  p.putBool("loc", have_loc);
  p.putBool("en", enabled);
  p.end();
}

// Same mapping as app/core/weather.js and web-sync/index.html
static uint8_t classify(int code, bool day) {
  if (code == 0 || code == 1) return day ? 0 : 5;
  if (code == 2 || code == 3 || code == 45 || code == 48) return day ? 1 : 5;
  switch (code) {
    case 51: case 53: case 55: case 61: case 63: case 65: case 66: case 67: case 80: case 81: case 82: return 2;
    case 95: case 96: case 99: return 3;
    case 71: case 73: case 75: case 77: return 4;
    default: return 1;
  }
}

static const char *after(const char *s, const char *key) {
  const char *p = strstr(s, key);
  return p ? p + strlen(key) : nullptr;
}

// Open-Meteo answer, e.g. {"utc_offset_seconds":-25200,...,"current":{"time":"2026-10-28T15:45","interval":900,
// "temperature_2m":20.5,"relative_humidity_2m":36,"weather_code":1,"is_day":1}}. "current_units" also has these keys,
// so everything except the offset is searched after the "current" object starts.
static bool parse_weather(const char *body) {
  const char *cur = after(body, "\"current\":{");
  if (!cur) return false;
  const char *tm = after(cur, "\"time\":\"");
  const char *t = after(cur, "\"temperature_2m\":");
  const char *h = after(cur, "\"relative_humidity_2m\":");
  const char *c = after(cur, "\"weather_code\":");
  const char *d = after(cur, "\"is_day\":");
  const char *off = after(body, "\"utc_offset_seconds\":");
  int yy, mo, dd, hh, mi;
  if (!tm || !t || !h || !c || !d || sscanf(tm, "%d-%d-%dT%d:%d", &yy, &mo, &dd, &hh, &mi) != 5) return false;
  pending.group = classify(atoi(c), atoi(d) == 1);
  pending.temp_c = (int8_t)lroundf((float)atof(t));
  pending.humidity = (uint8_t)atoi(h);
  pending.hour = (uint8_t)hh;
  pending.minute = (uint8_t)mi;
  if (off) tz_offset = atol(off);
  pending_ready = true; // last, the main loop reads the struct once this is set
  return true;
}

static bool fetch_weather() {
  char url[240];
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,weather_code,is_day&timezone=auto",
           lat, lon);
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[wifi] weather HTTP %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  bool ok = parse_weather(body.c_str());
  if (!ok) Serial.println("[wifi] weather answer not understood");
  return ok;
}

static void wifi_task(void *) {
  uint32_t last_attempt = 0;
  bool attempted = false, announced = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (creds_changed) {
      creds_changed = false;
      WiFi.disconnect(true, false);
      attempted = false;
      announced = false;
    }
    if (!enabled || ssid[0] == 0 || host_present) {
      if (WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
      }
      status = (ssid[0] == 0) ? WS_NO_CREDS : (!enabled ? WS_OFF : WS_HOST);
      attempted = false;
      announced = false;
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      status = WS_CONNECTING;
      announced = false;
      if (!attempted || millis() - last_attempt > 20000) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(ssid, pass);
        attempted = true;
        last_attempt = millis();
        Serial.printf("[wifi] connecting to %s (free heap %u, largest block %u)\n", ssid, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
      }
      continue;
    }
    if (!announced) {
      announced = true;
      status = have_loc ? WS_CONNECTED : WS_NO_LOCATION;
      Serial.printf("[wifi] connected, ip %s, free heap %u\n", WiFi.localIP().toString().c_str(), (unsigned)ESP.getFreeHeap());
    }
    if (!sntp_started) {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // the system clock stays UTC; wifi_sync_time() adds the location's offset
      sntp_started = true;
    }
    if (!have_loc) {
      status = WS_NO_LOCATION;
      continue;
    }
    if ((int32_t)(millis() - next_weather) >= 0) {
      bool ok = fetch_weather();
      status = ok ? WS_CONNECTED : WS_WEATHER_FAIL;
      next_weather = millis() + (ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS);
    }
  }
}

void wifi_sync_begin() {
  prefs_load();
  xTaskCreatePinnedToCore(wifi_task, "wifi", 8192, nullptr, 1, nullptr, 0);
}

bool wifi_sync_set_credentials(const char *s, const char *p) {
  if (s == nullptr || s[0] == 0) {
    ssid[0] = 0;
    pass[0] = 0;
    enabled = false;
    Serial.println("[wifi] credentials forgotten");
  } else {
    strlcpy(ssid, s, sizeof(ssid));
    strlcpy(pass, p ? p : "", sizeof(pass));
    enabled = true;
    Serial.printf("[wifi] credentials saved for %s\n", ssid);
  }
  prefs_save();
  next_weather = 0;
  creds_changed = true;
  return true;
}

void wifi_sync_set_location(float la, float lo) {
  lat = la;
  lon = lo;
  have_loc = true;
  prefs_save();
  next_weather = 0;
  Serial.printf("[wifi] location set %.4f, %.4f\n", la, lo);
}

void wifi_sync_set_enabled(bool on) {
  enabled = on;
  prefs_save();
  next_weather = 0;
}

bool wifi_sync_enabled() { return enabled; }
void wifi_sync_host_present(bool present) { host_present = present; }

bool wifi_sync_connected() { return enabled && !host_present && WiFi.status() == WL_CONNECTED; }

bool wifi_sync_poll_weather(WifiWeather &w) {
  if (!pending_ready) return false;
  w = pending;
  pending_ready = false;
  return true;
}

bool wifi_sync_time(uint8_t &h, uint8_t &m, uint8_t &s) {
  if (!sntp_started) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false; // not synced yet
  now += tz_offset;
  struct tm t;
  gmtime_r(&now, &t);
  h = t.tm_hour;
  m = t.tm_min;
  s = t.tm_sec;
  return true;
}

const char *wifi_sync_status() {
  switch (status) {
    case WS_NO_CREDS: return "no network saved (use the web page)";
    case WS_HOST: return "paused, app/web page connected";
    case WS_CONNECTING: return "connecting...";
    case WS_NO_LOCATION: return "connected, needs a location";
    case WS_CONNECTED: return "connected";
    case WS_WEATHER_FAIL: return "connected, weather failed";
    default: return "off";
  }
}
#endif // ENABLE_WIFI
