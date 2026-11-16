#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPIFFS_STORAGE_PARTITION "storage"
#define SPIFFS_MOUNT_POINT       "/spiffs"

/**
 * @brief Initialize and mount the SPIFFS partition 'storage' at '/spiffs'.
 *        Formats if mounting fails.
 * @return ESP_OK on success, or appropriate error code.
 */
esp_err_t app_spiffs_init(void);

/**
 * @brief Check if the storage partition is currently mounted.
 */
bool app_spiffs_is_mounted(void);

/**
 * @brief Safely write data to a file using atomic write (.tmp -> rename).
 * @param path Target file path (e.g. "/spiffs/devices.json")
 * @param data Buffer containing data to write
 * @param len  Number of bytes to write
 * @return ESP_OK on success, or ESP_FAIL.
 */
esp_err_t app_spiffs_write_file(const char *path, const char *data, size_t len);

/**
 * @brief Read entire contents of a file into memory (allocated from SPIRAM if available, else heap).
 * @param path File path to read
 * @param out_len Pointer to store read byte length (can be NULL)
 * @return Pointer to null-terminated string, or NULL on failure. Caller must free().
 */
char *app_spiffs_read_file(const char *path, size_t *out_len);

/**
 * @brief Check whether a file exists at the given path.
 */
bool app_spiffs_file_exists(const char *path);

/**
 * @brief Get total and used bytes of the storage partition.
 */
esp_err_t app_spiffs_get_info(size_t *total_bytes, size_t *used_bytes);

#ifdef __cplusplus
}
#endif
