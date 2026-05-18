#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/* Feature gate: set to 0 to compile-out web portal */
#ifndef WEB_PORTAL_ENABLED
#define WEB_PORTAL_ENABLED 1
#endif

esp_err_t webportal_start(void);
void      webportal_stop(void);

#ifdef __cplusplus
}
#endif
