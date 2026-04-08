#pragma once

/*
 * os_hal.h
 * ─────────────────────────────────────────────────────────────────
 * Hardware Abstraction Layer (HAL) umbrella header for ESP32OS.
 *
 * Include this single header to access all hardware driver APIs:
 *   - GPIO (digital I/O)
 *   - ADC  (analog input)
 *   - I2C  (master bus)
 *   - SPI  (master bus)
 *   - UART (additional ports)
 *
 * Architecture position:
 *   Applications / Shell
 *       │
 *   System Services (os_kernel, os_fs, os_networking)
 *       │
 *   ► HAL (os_hal / os_drivers)
 *       │
 *   ESP-IDF peripheral drivers
 *       │
 *   Hardware
 * ─────────────────────────────────────────────────────────────────
 */

#include "os_drivers.h"
