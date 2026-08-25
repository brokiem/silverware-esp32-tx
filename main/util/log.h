#pragma once
#include <stdint.h>
#include <stdio.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_timer.h>
static inline void print_timestamp(void) {
    const uint64_t total_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    const unsigned long long ms = total_ms % 1000ULL;
    const unsigned long long total_s = total_ms / 1000ULL;
    const unsigned long long s = total_s % 60ULL;
    const unsigned long long m = (total_s / 60ULL) % 60ULL;
    const unsigned long long h = total_s / 3600ULL;
    printf("[%02llu:%02llu:%02llu.%03llu] ", h, m, s, ms);
}
#else
static inline void print_timestamp(void) {
    printf("[00:00:00.000] ");
}
#endif

#define LOG(fmt, ...)                    \
    do {                                 \
        print_timestamp();               \
        printf(fmt "\n", ##__VA_ARGS__); \
    } while (0)
