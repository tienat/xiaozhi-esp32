#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Quyet dinh man hinh thuan, dung ca tren firmware va host test. */
typedef enum {
    ONBOARD_SPLASH, ONBOARD_SCAN, ONBOARD_PASSWORD, ONBOARD_CONNECTING,
    ONBOARD_WIFI_ERROR, ONBOARD_TIME, ONBOARD_TIME_ERROR, ONBOARD_CLOUD,
    ONBOARD_PAIRING, ONBOARD_SUCCESS, ONBOARD_HOME
} onboarding_screen_t;

typedef struct {
    bool splash_done, network_ready, has_saved_wifi, connected, time_synced, wifi_failed;
    bool cached_home, linked, pairing, edit_wifi, entered_home;
    uint32_t elapsed_ms;
} onboarding_input_t;

static inline onboarding_screen_t onboarding_decide(onboarding_screen_t current,
                                                     const onboarding_input_t *in)
{
    if (!in->splash_done) return ONBOARD_SPLASH;
    if (current == ONBOARD_SPLASH) {
        if (!in->network_ready) return ONBOARD_SPLASH;
        if (in->cached_home && (in->entered_home || !in->linked)) return ONBOARD_HOME;
        if (in->edit_wifi || !in->has_saved_wifi) return ONBOARD_SCAN;
        return ONBOARD_CONNECTING;
    }
    /* Thao tac chon mang/nhap mat khau khong bi event nen phu len. */
    if (in->edit_wifi) return current;
    if (in->cached_home && (in->entered_home || !in->linked)) return ONBOARD_HOME;
    if (in->linked) return in->entered_home ? ONBOARD_HOME : ONBOARD_SUCCESS;
    if (in->pairing) return ONBOARD_PAIRING;
    if (!in->network_ready) return in->elapsed_ms >= 30000 ? ONBOARD_WIFI_ERROR : ONBOARD_CONNECTING;
    if (!in->connected) {
        if (in->wifi_failed || in->elapsed_ms >= 30000) return ONBOARD_WIFI_ERROR;
        return ONBOARD_CONNECTING;
    }
    if (!in->time_synced && in->elapsed_ms >= 30000) {
        return ONBOARD_TIME_ERROR;
    }
    return ONBOARD_CLOUD;
}
