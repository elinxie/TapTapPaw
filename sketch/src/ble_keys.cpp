// Bluetooth keyboard for button widgets (BtnAction BA_BLE_KEY / BA_BLE_MEDIA). Only the esp32dev_ble build has it
// (-D ENABLE_BLE_KEYS=1); every other build answers false and the button shows a short note.
#include <Arduino.h>

#ifndef ENABLE_BLE_KEYS
#define ENABLE_BLE_KEYS 0
#endif

#if !ENABLE_BLE_KEYS
bool ble_keys_send(uint8_t, uint8_t) { return false; }
bool ble_media_send(uint16_t) { return false; }
void ble_keys_begin() {}
#endif
