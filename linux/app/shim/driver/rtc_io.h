// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpio.h"
static inline bool rtc_gpio_is_valid_gpio(gpio_num_t) { return false; }
static inline esp_err_t rtc_gpio_pullup_en(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t rtc_gpio_pulldown_en(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t rtc_gpio_pullup_dis(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t rtc_gpio_pulldown_dis(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t rtc_gpio_hold_en(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t rtc_gpio_hold_dis(gpio_num_t) { return ESP_ERR_NOT_SUPPORTED; }
