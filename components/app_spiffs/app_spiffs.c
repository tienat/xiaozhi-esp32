#include "app_spiffs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"

static const char *TAG = "app_spiffs";
static bool s_is_mounted = false;

esp_err_t app_spiffs_init(void)
{
    if (s_is_mounted) {
        ESP_LOGI(TAG, "Storage partition '%s' already mounted at '%s'", SPIFFS_STORAGE_PARTITION, SPIFFS_MOUNT_POINT);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SPIFFS storage '%s' at '%s'", SPIFFS_STORAGE_PARTITION, SPIFFS_MOUNT_POINT);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_MOUNT_POINT,
        .partition_label = SPIFFS_STORAGE_PARTITION,
        .max_files = 8,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem for '%s'", SPIFFS_STORAGE_PARTITION);
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition '%s'", SPIFFS_STORAGE_PARTITION);
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(SPIFFS_STORAGE_PARTITION, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get SPIFFS partition info (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS partition '%s': total=%u KB, used=%u KB (%.1f%%)",
                 SPIFFS_STORAGE_PARTITION,
                 (unsigned)(total / 1024),
                 (unsigned)(used / 1024),
                 total ? ((float)used * 100.0f / (float)total) : 0.0f);
    }

    s_is_mounted = true;
    return ESP_OK;
}

bool app_spiffs_is_mounted(void)
{
    return s_is_mounted;
}

esp_err_t app_spiffs_get_info(size_t *total_bytes, size_t *used_bytes)
{
    if (!s_is_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_spiffs_info(SPIFFS_STORAGE_PARTITION, total_bytes, used_bytes);
}

bool app_spiffs_file_exists(const char *path)
{
    if (!path || !s_is_mounted) {
        return false;
    }
    struct stat st;
    return (stat(path, &st) == 0);
}

esp_err_t app_spiffs_write_file(const char *path, const char *data, size_t len)
{
    if (!path || !data || !s_is_mounted) {
        return ESP_ERR_INVALID_ARG;
    }

    char tmp_path[256];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n < 0 || n >= (int)sizeof(tmp_path)) {
        ESP_LOGE(TAG, "File path too long: %s", path);
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open temporary file for writing: %s", tmp_path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, f);
    if (written != len) {
        ESP_LOGE(TAG, "Short write: %zu of %zu bytes written to %s", written, len, tmp_path);
        fclose(f);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    /* Atomic replace */
    unlink(path);
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to rename %s to %s", tmp_path, path);
        unlink(tmp_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully wrote %zu bytes to %s", len, path);
    return ESP_OK;
}

char *app_spiffs_read_file(const char *path, size_t *out_len)
{
    if (out_len) {
        *out_len = 0;
    }
    if (!path || !s_is_mounted) {
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "File not found or cannot open: %s", path);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    /* Allocate buffer with PSRAM preference for large files */
    char *buf = (char *)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (char *)malloc(size + 1);
    }
    if (!buf) {
        ESP_LOGE(TAG, "Out of memory allocating %ld bytes for %s", size + 1, path);
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    fclose(f);

    buf[read_bytes] = '\0';
    if (out_len) {
        *out_len = read_bytes;
    }

    return buf;
}
