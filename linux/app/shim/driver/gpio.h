// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// No ESP GPIO matrix here; every caller in the shared sources is behind a pin
// that is -1 on this board.
#include "../esp_idf_linux.h"
typedef int gpio_num_t;
typedef enum { GPIO_INTR_DISABLE, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE, GPIO_INTR_ANYEDGE,
               GPIO_INTR_LOW_LEVEL, GPIO_INTR_HIGH_LEVEL } gpio_int_type_t;
typedef enum { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2 } gpio_mode_t;
typedef enum { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_PULLUP_PULLDOWN, GPIO_FLOATING } gpio_pull_mode_t;
#define GPIO_NUM_NC (-1)
static inline esp_err_t gpio_wakeup_enable(gpio_num_t, gpio_int_type_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t gpio_wakeup_disable(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t gpio_hold_en(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t gpio_hold_dis(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline void gpio_deep_sleep_hold_en(void) {}
static inline void gpio_deep_sleep_hold_dis(void) {}
static inline esp_err_t gpio_reset_pin(gpio_num_t) { return ESP_OK; }
static inline esp_err_t gpio_set_direction(gpio_num_t, gpio_mode_t) { return ESP_OK; }
static inline esp_err_t gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t) { return ESP_OK; }
static inline esp_err_t gpio_set_level(gpio_num_t, uint32_t) { return ESP_OK; }
static inline int gpio_get_level(gpio_num_t) { return 0; }
