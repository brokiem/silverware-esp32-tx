#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "../main/radio/bayang.h"

// Expose internal functions for testing
extern "C" uint8_t dynamic_trim(uint16_t v) {
    return (v >> 2) & 0xFC;
}

extern "C" uint8_t calculate_checksum(const uint8_t* packet) {
    uint8_t sum = 0;
    for (int i = 0; i < BAYANG_PACKET_SIZE - 1; i++) {
        sum += packet[i];
    }
    return sum;
}

void test_dynamic_trim() {
    TEST_ASSERT_EQUAL(128, dynamic_trim(512));
    TEST_ASSERT_EQUAL(0, dynamic_trim(0));
    TEST_ASSERT_EQUAL(252, dynamic_trim(1023));
}

void test_calculate_checksum() {
    uint8_t packet[BAYANG_PACKET_SIZE] = {0};
    packet[0] = 0xA5;
    packet[1] = 0xFA;
    TEST_ASSERT_EQUAL((uint8_t)(0xA5 + 0xFA), calculate_checksum(packet));
}

void test_bit_reverse() {
    // Just a basic check if we want to copy the logic here
    uint8_t b = 0x55; // 01010101
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    TEST_ASSERT_EQUAL(0xAA, b); // 10101010
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_dynamic_trim);
    RUN_TEST(test_calculate_checksum);
    RUN_TEST(test_bit_reverse);
    return UNITY_END();
}
