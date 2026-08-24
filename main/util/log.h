#pragma once
#include <stdio.h>
#include <stdint.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_timer.h>
static inline void print_timestamp(void) {
    int64_t now_us = esp_timer_get_time();
    uint32_t total_ms = (uint32_t)(now_us / 1000);
    uint32_t ms = total_ms % 1000;
    uint32_t total_s = total_ms / 1000;
    uint32_t s = total_s % 60;
    uint32_t m = (total_s / 60) % 60;
    uint32_t h = total_s / 3600;
    printf("[%02u:%02u:%02u.%03u] ", h, m, s, ms);
}
#else
static inline void print_timestamp(void) {
    printf("[00:00:00.000] ");
}
#endif

#define LOG(fmt, ...) do { \
    print_timestamp(); \
    printf(fmt "\n", ##__VA_ARGS__); \
} while(0)
