// src/ResidentNvsStore.h
//
// NVS-backed PersistentStore. Stores the app source as a single blob under
// namespace "resident", key "app". Compiled only on ESP32 (Arduino or
// ESP-IDF); native test builds inject an in-memory store instead.
#ifndef RESIDENT_NVS_STORE_H
#define RESIDENT_NVS_STORE_H

#if defined(ARDUINO) || defined(ESP_PLATFORM)

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "ResidentPersistentStore.h"

namespace Resident {

class NvsPersistentStore : public PersistentStore {
public:
  bool begin() override {
    // nvs_flash_init is idempotent; Courier/WiFi may already have run it.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    return err == ESP_OK;
  }

  bool save(const char* source, size_t len) override {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, KEY, source, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
  }

  String load() override {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return String();
    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, KEY, nullptr, &len);
    if (err != ESP_OK || len == 0) { nvs_close(h); return String(); }

    String out;
    char* buf = (char*)malloc(len + 1);
    if (!buf) { nvs_close(h); return String(); }
    err = nvs_get_blob(h, KEY, buf, &len);
    if (err == ESP_OK) { buf[len] = '\0'; out = String(buf); }
    free(buf);
    nvs_close(h);
    return out;
  }

  void clear() override {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, KEY);   // ESP_ERR_NVS_NOT_FOUND is fine
    nvs_commit(h);
    nvs_close(h);
  }

private:
  static constexpr const char* NS  = "resident";
  static constexpr const char* KEY = "app";
};

} // namespace Resident

#endif // ARDUINO || ESP_PLATFORM
#endif // RESIDENT_NVS_STORE_H
