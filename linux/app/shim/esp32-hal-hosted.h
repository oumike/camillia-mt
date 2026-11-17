// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// ESP-Hosted (P4 Wi-Fi coprocessor) queries; nothing is hosted here.
static inline bool hostedIsInitialized() { return false; }
static inline bool hostedIsBLEActive() { return false; }
static inline void hostedGetHostVersion(uint32_t *a, uint32_t *b, uint32_t *c) { *a = *b = *c = 0; }
static inline void hostedGetSlaveVersion(uint32_t *a, uint32_t *b, uint32_t *c) { *a = *b = *c = 0; }
static inline const char *hostedGetSlaveTargetName() { return "none"; }
