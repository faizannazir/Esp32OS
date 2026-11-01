#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/* Feature gate: set to 0 to compile-out web portal */
#include "sdkconfig.h"

/* The unauthenticated control portal is opt-in only. */
#ifndef WEB_PORTAL_ENABLED
#if defined(CONFIG_ESP32OS_ENABLE_WEB_PORTAL) && CONFIG_ESP32OS_ENABLE_WEB_PORTAL
#define WEB_PORTAL_ENABLED 1
#else
#define WEB_PORTAL_ENABLED 0
#endif
#endif

esp_err_t webportal_start(void);
void      webportal_stop(void);

#ifdef __cplusplus
}
#endif
