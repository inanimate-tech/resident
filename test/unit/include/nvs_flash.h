// test/unit/include/nvs_flash.h
//
// Declarations-only stub of ESP-IDF's <nvs_flash.h> for native unit tests;
// see nvs.h beside it.
#pragma once
#include "nvs.h"

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
