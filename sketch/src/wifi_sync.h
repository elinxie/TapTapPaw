#pragma once
#include <Arduino.h>

// WiFi mode: the board gets NTP time and Open-Meteo weather by itself when no host (desktop app / web page) is connected.
// Only compiled with -D ENABLE_WIFI=1 (PlatformIO env esp32dev_wifi, larger partition table, see docs/WIFI_MODE.md).
// In the normal build every function below is a no-op, so main.cpp does not need #ifdefs.
#ifndef ENABLE_WIFI
#define ENABLE_WIFI 0
#endif

struct WifiWeather {
  uint8_t group; // 0 sunny, 1 cloudy, 2 rainy, 3 stormy, 4 snowy, 5 night (same as the weather packet)
  int8_t temp_c;
  uint8_t humidity;
  uint8_t hour; // observation time at the location
  uint8_t minute;
};

#if ENABLE_WIFI
void wifi_sync_begin(); // load settings and start the background task
bool wifi_sync_set_credentials(const char *ssid, const char *pass); // saves and enables; empty ssid = forget
void wifi_sync_set_location(float lat, float lon);
void wifi_sync_set_enabled(bool on);
bool wifi_sync_enabled();
void wifi_sync_host_present(bool present); // a serial host is connected: WiFi is switched off while it is
bool wifi_sync_connected(); // enabled, no host, and associated with the access point
bool wifi_sync_poll_weather(WifiWeather &w); // call from the main loop; true once per new result
bool wifi_sync_time(uint8_t &h, uint8_t &m, uint8_t &s); // local time at the weather location, false until NTP has synced
const char *wifi_sync_status(); // short text for the settings screen
#else
static inline void wifi_sync_begin() {}
static inline bool wifi_sync_set_credentials(const char *, const char *) {
  Serial.println("[wifi] this firmware has no WiFi mode (flash the esp32dev_wifi build)");
  return false;
}
static inline void wifi_sync_set_location(float, float) {}
static inline void wifi_sync_set_enabled(bool) {}
static inline bool wifi_sync_enabled() { return false; }
static inline void wifi_sync_host_present(bool) {}
static inline bool wifi_sync_connected() { return false; }
static inline bool wifi_sync_poll_weather(WifiWeather &) { return false; }
static inline bool wifi_sync_time(uint8_t &, uint8_t &, uint8_t &) { return false; }
static inline const char *wifi_sync_status() { return "not in this build"; }
#endif
