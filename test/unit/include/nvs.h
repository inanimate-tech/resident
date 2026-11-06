// test/unit/include/nvs.h
//
// Declarations-only stub of ESP-IDF's <nvs.h> for native unit tests: the
// subset ResidentNvsStore.h calls, with ESP-IDF's own error values. A test
// that includes ResidentNvsStore.h defines these functions over a fake
// partition (see test_nvs_store). nvs_stats_t carries only the fields every
// supported ESP-IDF has (4.4 lacks available_entries), so a store that reached
// for a newer field would fail here as it would on an older device build.
#pragma once
#include <cstddef>
#include <cstdint>

typedef int esp_err_t;
#define ESP_OK   0
#define ESP_FAIL -1

#define ESP_ERR_NVS_BASE              0x1100
#define ESP_ERR_NVS_NOT_FOUND         (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE  (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_REMOVE_FAILED     (ESP_ERR_NVS_BASE + 0x08)
#define ESP_ERR_NVS_NO_FREE_PAGES     (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

typedef struct {
  size_t used_entries;
  size_t free_entries;
  size_t total_entries;
  size_t namespace_count;
} nvs_stats_t;

esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* out);
void      nvs_close(nvs_handle_t h);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len);
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_get_stats(const char* part_name, nvs_stats_t* stats);
