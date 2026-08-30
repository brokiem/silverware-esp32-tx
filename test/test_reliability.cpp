#include <unity.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../main/config.h"
#include "../main/feedback/feedback_pattern.h"
#include "../main/gamepad/button_edges.h"
#include "../main/gamepad/control_mapping.h"
#include "../main/radio/bayang.h"
#include "../main/radio/nfe_silverware_profile.h"
#include "../main/radio/radio_validation.h"
#include "../main/safety/failsafe.h"
#include "../main/safety/prearm_guard.h"
#include "../main/storage/settings.h"
#include "../main/telemetry/telemetry.h"
#include "../main/telemetry/pc_telemetry_protocol.h"
#include "../main/telemetry/pc_telemetry_state.h"

static void set_checksum(uint8_t* packet) {
    packet[BAYANG_PACKET_SIZE - 1] = bayang_calculate_checksum(packet);
}

static void test_throttle_mapping() {
    TEST_ASSERT_EQUAL_UINT16(0, map_trigger_throttle(-1));
    TEST_ASSERT_EQUAL_UINT16(512, map_trigger_throttle(512));
    TEST_ASSERT_EQUAL_UINT16(1023, map_trigger_throttle(2048));

    TEST_ASSERT_EQUAL_UINT16(511, map_half_stick_throttle(-512, 0.05f));
    TEST_ASSERT_EQUAL_UINT16(0, map_half_stick_throttle(0, 0.05f));
    TEST_ASSERT_EQUAL_UINT16(0, map_half_stick_throttle(511, 0.05f));
}

static void test_bayang_channel_mapping() {
    TEST_ASSERT_EQUAL_UINT16(0, map_bayang_channel(-100, true, false, 0.0f, 0.0f));
    TEST_ASSERT_EQUAL_UINT16(1023, map_bayang_channel(1023, true, false, 0.0f, 0.0f));
    TEST_ASSERT_EQUAL_UINT16(512, map_bayang_channel(0, false, false, 0.05f, 0.0f));
    TEST_ASSERT_EQUAL_UINT16(512, map_bayang_channel(0, false, true, 0.05f, 0.0f));
}

static void test_start_view_and_disarm_button_edges() {
    ButtonEdgeState previous = {};
    ButtonEdges edges = detect_button_edges(previous, true, false, false);
    TEST_ASSERT_TRUE(edges.startClicked);
    TEST_ASSERT_FALSE(edges.viewClicked);
    TEST_ASSERT_FALSE(detect_button_edges(previous, true, false, false).startClicked);

    edges = detect_button_edges(previous, false, true, false);
    TEST_ASSERT_TRUE(edges.viewClicked);
    TEST_ASSERT_FALSE(edges.startClicked);
    TEST_ASSERT_FALSE(detect_button_edges(previous, false, true, false).viewClicked);

    edges = detect_button_edges(previous, false, false, true);
    TEST_ASSERT_TRUE(edges.bClicked);
    TEST_ASSERT_FALSE(detect_button_edges(previous, false, false, true).bClicked);
}

static void test_feedback_state_patterns() {
    FeedbackOutput output = feedback_pattern(STATE_LOCKED, STATE_ACTIVE, 0);
    TEST_ASSERT_TRUE(output.ledOn);
    TEST_ASSERT_TRUE(output.buzzerOn);
    TEST_ASSERT_FALSE(feedback_pattern(STATE_LOCKED, STATE_ACTIVE, 250).buzzerOn);

    output = feedback_pattern(STATE_ACTIVE, STATE_LOCKED, 75);
    TEST_ASSERT_TRUE(output.ledOn);
    TEST_ASSERT_FALSE(output.buzzerOn);
    TEST_ASSERT_TRUE(feedback_pattern(STATE_ACTIVE, STATE_LOCKED, 150).buzzerOn);

    TEST_ASSERT_TRUE(feedback_pattern(STATE_LOCKED, STATE_BINDING, 50).ledOn);
    TEST_ASSERT_FALSE(feedback_pattern(STATE_LOCKED, STATE_BINDING, 150).ledOn);

    TEST_ASSERT_TRUE(feedback_pattern(STATE_ACTIVE, STATE_GAMEPAD_FAILSAFE, 10).buzzerOn);
    TEST_ASSERT_FALSE(feedback_pattern(STATE_ACTIVE, STATE_GAMEPAD_FAILSAFE, 100).buzzerOn);
    TEST_ASSERT_TRUE(feedback_pattern(STATE_ACTIVE, STATE_GAMEPAD_FAILSAFE, 170).buzzerOn);

    TEST_ASSERT_TRUE(feedback_pattern(STATE_LOCKED, STATE_RADIO_ERROR, 50).ledOn);
    TEST_ASSERT_FALSE(feedback_pattern(STATE_LOCKED, STATE_RADIO_ERROR, 150).ledOn);
    TEST_ASSERT_TRUE(feedback_pattern(STATE_LOCKED, STATE_RADIO_ERROR, 450).buzzerOn);
    TEST_ASSERT_TRUE(feedback_pattern(STATE_LOCKED, STATE_PREARM_MODE, 50).ledOn);
    TEST_ASSERT_FALSE(feedback_pattern(STATE_LOCKED, STATE_PREARM_MODE, 150).ledOn);
}

static void test_bayang_packet_flags_clamping_and_checksum() {
    const uint8_t tx_id[5] = {1, 2, 3, 4, 5};
    bayang_init(tx_id);

    BayangControlState state = {};
    state.roll = 2000;
    state.pitch = 2000;
    state.throttle = 2000;
    state.yaw = 2000;
    state.aux_flip = true;
    state.aux_rth = true;
    state.aux_headless = true;
    state.aux_picture = true;
    state.aux_video = true;
    state.aux_take_off = true;
    state.aux_inverted = true;
    state.aux_emg_stop = true;

    uint8_t packet[BAYANG_PACKET_SIZE];
    bayang_build_data_packet(packet, &state);

    TEST_ASSERT_EQUAL_HEX8(0xA5, packet[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3B, packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0xAC, packet[3]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, packet[4]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, packet[5]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, packet[8]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, packet[9]);

    uint8_t expected_checksum = 0;
    for (uint8_t i = 0; i < BAYANG_PACKET_SIZE - 1; ++i)
        expected_checksum += packet[i];
    TEST_ASSERT_EQUAL_HEX8(expected_checksum, packet[14]);
}

static void test_golden_centered_packet_and_individual_aux_masks() {
    const uint8_t tx_id[5] = {1, 2, 3, 4, 5};
    bayang_init(tx_id);
    BayangControlState state = {};
    state.roll = 512;
    state.pitch = 512;
    state.yaw = 512;

    uint8_t packet[BAYANG_PACKET_SIZE];
    bayang_build_data_packet(packet, &state);
    const uint8_t golden[BAYANG_PACKET_SIZE] = {
        0xA5, 0xFA, 0x00, 0x00, 0x82, 0x00, 0x82, 0x00, 0x7C, 0x00, 0x82, 0x00, 0x03, 0x0A, 0xAE,
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, packet, BAYANG_PACKET_SIZE);

    state.aux_rth = true;
    bayang_build_data_packet(packet, &state);
    TEST_ASSERT_EQUAL_HEX8(0x01, packet[2]);
    state = {};
    state.aux_headless = true;
    bayang_build_data_packet(packet, &state);
    TEST_ASSERT_EQUAL_HEX8(0x02, packet[2]);
    state = {};
    state.aux_flip = true;
    bayang_build_data_packet(packet, &state);
    TEST_ASSERT_EQUAL_HEX8(0x08, packet[2]);
    state = {};
    state.aux_video = true;
    bayang_build_data_packet(packet, &state);
    TEST_ASSERT_EQUAL_HEX8(0x10, packet[2]);
    state = {};
    state.aux_picture = true;
    bayang_build_data_packet(packet, &state);
    TEST_ASSERT_EQUAL_HEX8(0x20, packet[2]);
    state = {};
    state.aux_inverted = true;
    bayang_build_data_packet(packet, &state);
    TEST_ASSERT_EQUAL_HEX8(0x80, packet[3]);
    state = {};
    state.aux_emg_stop = true;
    bayang_build_data_packet(packet, &state);
    TEST_ASSERT_EQUAL_HEX8(0x0C, packet[3]);
}

static void test_bayang_bind_packet_and_hopping() {
    const uint8_t tx_id[5] = {0x10, 0x20, 0x30, 0x04, 0x50};
    bayang_init(tx_id);
    uint8_t channels[BAYANG_RF_CHANNELS];
    bayang_get_hopping_channels(channels);
    TEST_ASSERT_EQUAL_HEX8(0x00, channels[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, channels[1]);
    TEST_ASSERT_EQUAL_HEX8(0x34, channels[2]);
    TEST_ASSERT_EQUAL_HEX8(0x54, channels[3]);

    uint8_t packet[BAYANG_PACKET_SIZE];
    bayang_build_bind_packet(packet, BAYANG_BIND_A3);
    TEST_ASSERT_EQUAL_HEX8(BAYANG_BIND_A3, packet[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx_id, &packet[1], 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(channels, &packet[6], 4);
    TEST_ASSERT_TRUE(bayang_check_telemetry(packet) == false);
}

static void test_nfe_silverware_autobind_multi_profile() {
    TEST_ASSERT_EQUAL_HEX8(BAYANG_BIND_A1, bayang_select_bind_header(true, true));
    TEST_ASSERT_EQUAL_HEX8(BAYANG_BIND_A2, bayang_select_bind_header(false, true));
    TEST_ASSERT_EQUAL_HEX8(BAYANG_BIND_A3, bayang_select_bind_header(true, false));
    TEST_ASSERT_EQUAL_HEX8(BAYANG_BIND_A4, bayang_select_bind_header(false, false));

    const uint8_t tx_id[5] = {0x10, 0x20, 0x30, 0x04, 0x50};
    bayang_init(tx_id);
    uint8_t packet[BAYANG_PACKET_SIZE] = {};
    bayang_build_bind_packet(packet, bayang_select_bind_header(BAYANG_ENABLE_TELEMETRY, BAYANG_ENABLE_ANALOG_AUX));

    TEST_ASSERT_EQUAL_HEX8(BAYANG_BIND_A3, packet[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, packet[13]);
    TEST_ASSERT_EQUAL_INT(5, CONTROL_LOOP_PERIOD_MS);

    struct AuxMappingCase {
        bool buttonA;
        bool buttonX;
        bool buttonY;
        bool buttonRB;
        bool buttonLB;
        uint8_t packet2;
        uint8_t packet3;
    };
    const AuxMappingCase cases[] = {
        {true, false, false, false, false, BAYANG_FLAG_FLIP | BAYANG_FLAG_RTH, 0},
        {false, true, false, false, false, BAYANG_FLAG_FLIP | BAYANG_FLAG_PICTURE, 0},
        {false, false, true, false, false, BAYANG_FLAG_FLIP | BAYANG_FLAG_VIDEO, 0},
        {false, false, false, true, false, BAYANG_FLAG_FLIP | BAYANG_FLAG_HEADLESS, 0},
        {false, false, false, false, true, BAYANG_FLAG_FLIP, BAYANG_FLAG_INVERTED},
    };

    // Rajawali USE_MULTI: CH5=arm/idle, CH6=level, CH7=race, CH8=horizon,
    // CH9=PID profile, and CH10=LEDs.
    for (const AuxMappingCase& mapping : cases) {
        NfeSilverwareAuxState single_aux = {};
        nfe_silverware_update_aux(&single_aux, true, mapping.buttonA, mapping.buttonX, mapping.buttonY,
                                  mapping.buttonRB, mapping.buttonLB);
        BayangControlState single_control = {};
        nfe_silverware_apply_multi_aux(&single_control, true, single_aux);
        bayang_build_data_packet(packet, &single_control);
        TEST_ASSERT_EQUAL_HEX8(mapping.packet2, packet[2]);
        TEST_ASSERT_EQUAL_HEX8(mapping.packet3, packet[3]);
    }

    NfeSilverwareAuxState aux = {};
    nfe_silverware_update_aux(&aux, true, true, true, true, true, true);
    BayangControlState controls = {};

    // Held buttons do not retrigger; a second rising edge toggles the mode off.
    nfe_silverware_update_aux(&aux, true, true, true, true, true, true);
    TEST_ASSERT_TRUE(aux.levelMode);
    nfe_silverware_update_aux(&aux, true, false, false, false, false, false);
    nfe_silverware_update_aux(&aux, true, true, false, false, false, false);
    TEST_ASSERT_FALSE(aux.levelMode);

    // Disarming preserves FC configuration channels while clearing CH5.
    nfe_silverware_update_aux(&aux, false, false, false, false, false, false);
    controls = {};
    nfe_silverware_apply_multi_aux(&controls, false, aux);
    bayang_build_data_packet(packet, &controls);
    TEST_ASSERT_EQUAL_HEX8(BAYANG_FLAG_PICTURE | BAYANG_FLAG_VIDEO | BAYANG_FLAG_HEADLESS, packet[2]);
    TEST_ASSERT_EQUAL_HEX8(BAYANG_FLAG_INVERTED, packet[3]);

    // Locked control advertises Acro while allowing stick gestures, but cannot arm or add throttle.
    controls = nfe_silverware_make_locked_control(true, 512, 1023);
    bayang_build_data_packet(packet, &controls);
    TEST_ASSERT_EQUAL_HEX8(0, packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0, packet[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, packet[6]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, packet[7]);
    TEST_ASSERT_EQUAL_HEX8(0x7C, packet[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00, packet[9]);

    NfeSilverwareAuxState restored = {};
    restored.previousA = restored.previousX = true;
    nfe_silverware_restore_aux(&restored, 0x15);
    TEST_ASSERT_EQUAL_HEX8(0x15, nfe_silverware_aux_flags(restored));
    TEST_ASSERT_FALSE(restored.previousA);
    TEST_ASSERT_FALSE(restored.previousX);

    NfeSilverwareGesturePlayer player = {};
    NfeSilverwareGestureOutput gesture =
        nfe_silverware_update_gesture(&player, NfeSilverwareGesture::IncreasePid, 1000000);
    TEST_ASSERT_TRUE(gesture.active);
    TEST_ASSERT_EQUAL_UINT16(512, gesture.roll);
    TEST_ASSERT_EQUAL_UINT16(512, gesture.pitch);
    gesture = nfe_silverware_update_gesture(&player, NfeSilverwareGesture::None, 1800000);
    TEST_ASSERT_EQUAL_UINT16(512, gesture.roll);
    TEST_ASSERT_EQUAL_UINT16(1023, gesture.pitch);
    gesture = nfe_silverware_update_gesture(&player, NfeSilverwareGesture::None, 2200000);
    TEST_ASSERT_EQUAL_UINT16(512, gesture.roll);
    TEST_ASSERT_EQUAL_UINT16(0, gesture.pitch);
    gesture = nfe_silverware_update_gesture(&player, NfeSilverwareGesture::None, 2600000);
    TEST_ASSERT_EQUAL_UINT16(1023, gesture.roll);
    TEST_ASSERT_EQUAL_UINT16(512, gesture.pitch);
    gesture = nfe_silverware_update_gesture(&player, NfeSilverwareGesture::None, 3000000);
    TEST_ASSERT_FALSE(gesture.active);
}

static void test_radio_length_guards_and_status_decode() {
    TEST_ASSERT_FALSE(nrf24_payload_length_valid(0));
    TEST_ASSERT_TRUE(nrf24_payload_length_valid(1));
    TEST_ASSERT_TRUE(nrf24_payload_length_valid(32));
    TEST_ASSERT_FALSE(nrf24_payload_length_valid(33));
    TEST_ASSERT_TRUE(xn297_tx_payload_length_valid(25));
    TEST_ASSERT_FALSE(xn297_tx_payload_length_valid(26));

    TEST_ASSERT_EQUAL(static_cast<int>(Xn297TxStatus::Pending), static_cast<int>(xn297_decode_tx_status(0x00)));
    TEST_ASSERT_EQUAL(static_cast<int>(Xn297TxStatus::Sent), static_cast<int>(xn297_decode_tx_status(0x20)));
    TEST_ASSERT_EQUAL(static_cast<int>(Xn297TxStatus::Error), static_cast<int>(xn297_decode_tx_status(0x10)));
    TEST_ASSERT_EQUAL(static_cast<int>(Xn297TxStatus::Error), static_cast<int>(xn297_decode_tx_status(0x30)));
}

static void make_valid_telemetry(uint8_t* packet) {
    memset(packet, 0, BAYANG_PACKET_SIZE);
    packet[0] = 0x85;
    packet[3] = 0x01;
    packet[4] = 0x90;
    packet[5] = 0x01;
    packet[6] = 0x94;
    packet[7] = 72;
    packet[8] = 0x41;
    packet[9] = 0x23;
    set_checksum(packet);
}

static void telemetry_write_bits(uint8_t* packet, uint8_t* offset, uint32_t value, uint8_t count) {
    for (int bit = count - 1; bit >= 0; --bit) {
        const uint8_t byte_index = 2 + (*offset >> 3);
        const uint8_t byte_bit = 7 - (*offset & 7);
        if (value & (1UL << bit))
            packet[byte_index] |= 1U << byte_bit;
        ++*offset;
    }
}

static uint32_t signed_field(int32_t value, uint8_t bits) {
    return static_cast<uint32_t>(value) & ((1UL << bits) - 1UL);
}

static void make_extended_telemetry(uint8_t* packet, uint8_t page, uint8_t sequence,
                                    bool armed = false, bool failsafe = false) {
    memset(packet, 0, BAYANG_PACKET_SIZE);
    packet[0] = 0x86;
    packet[1] = (page << 6) | (armed ? 1U << 5 : 0) | (failsafe ? 1U << 4 : 0) | (sequence & 0x0F);
}

static void test_telemetry_validation_and_freshness() {
    telemetry_init();
    uint8_t packet[BAYANG_PACKET_SIZE];
    make_valid_telemetry(packet);

    TEST_ASSERT_TRUE(telemetry_parse(packet, 1000000));
    TelemetrySnapshot snapshot = telemetry_get_snapshot(1499000);
    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryFreshness::Fresh), static_cast<int>(snapshot.freshness));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.04f, snapshot.data.batteryCompensatedV);
    TEST_ASSERT_EQUAL_UINT16(144, snapshot.data.receiverPacketsPerSecond);
    TEST_ASSERT_EQUAL_UINT16(0x123, snapshot.data.pidI);
    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryProtocol::Original), static_cast<int>(snapshot.data.protocol));

    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryFreshness::Fresh),
                      static_cast<int>(telemetry_get_snapshot(1500000).freshness));
    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryFreshness::Stale),
                      static_cast<int>(telemetry_get_snapshot(1501000).freshness));

    packet[5] ^= 0x04;
    TEST_ASSERT_FALSE(telemetry_parse(packet, 2000000));
    snapshot = telemetry_get_snapshot(2000000);
    TEST_ASSERT_EQUAL_INT64(1000000, snapshot.ageUs);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.04f, snapshot.data.batteryCompensatedV);

    make_valid_telemetry(packet);
    packet[3] ^= 0x01;
    packet[4] ^= 0x02;
    TEST_ASSERT_FALSE(telemetry_parse(packet, 3000000));
    TEST_ASSERT_EQUAL_INT64(2000000, telemetry_get_snapshot(3000000).ageUs);
}

static void test_extended_control_and_flight_pages() {
    telemetry_init();
    uint8_t packet[BAYANG_PACKET_SIZE];

    make_extended_telemetry(packet, 0, 0, true, false);
    uint8_t offset = 0;
    telemetry_write_bits(packet, &offset, signed_field(-512, 10), 10);
    telemetry_write_bits(packet, &offset, 0, 10);
    telemetry_write_bits(packet, &offset, 511, 10);
    telemetry_write_bits(packet, &offset, signed_field(-100, 10), 10);
    telemetry_write_bits(packet, &offset, 100, 10);
    telemetry_write_bits(packet, &offset, 250, 10);
    telemetry_write_bits(packet, &offset, 63, 6);
    telemetry_write_bits(packet, &offset, 32, 6);
    telemetry_write_bits(packet, &offset, 0, 6);
    telemetry_write_bits(packet, &offset, 21, 6);
    telemetry_write_bits(packet, &offset, 42, 6);
    telemetry_write_bits(packet, &offset, 63, 6);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 1000));

    TelemetrySnapshot snapshot = telemetry_get_snapshot(1000);
    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryProtocol::ExtendedV1), static_cast<int>(snapshot.data.protocol));
    TEST_ASSERT_EQUAL_UINT16(TELEMETRY_EXTENDED_PAGE_CONTROL, snapshot.data.extendedPagesSeen);
    TEST_ASSERT_TRUE(snapshot.data.armed);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2048.0f, snapshot.data.gyroRollDps);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2044.0f, snapshot.data.gyroYawDps);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -400.0f, snapshot.data.setpointRollDps);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, snapshot.data.commandedThrottlePercent);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.79f, snapshot.data.appliedThrottlePercent);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.33f, snapshot.data.motorOutputPercent[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, snapshot.data.motorOutputPercent[3]);

    make_extended_telemetry(packet, 1, 1, true, true);
    offset = 0;
    telemetry_write_bits(packet, &offset, 1234, 12);
    telemetry_write_bits(packet, &offset, signed_field(-567, 12), 12);
    telemetry_write_bits(packet, &offset, 1800, 12);
    telemetry_write_bits(packet, &offset, 256, 12);
    telemetry_write_bits(packet, &offset, signed_field(-512, 12), 12);
    telemetry_write_bits(packet, &offset, 2047, 12);
    telemetry_write_bits(packet, &offset, 4321, 16);
    telemetry_write_bits(packet, &offset, 0x7F, 8);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 2000));

    snapshot = telemetry_get_snapshot(2000);
    TEST_ASSERT_TRUE(snapshot.data.armed);
    TEST_ASSERT_TRUE(snapshot.data.failsafe);
    TEST_ASSERT_TRUE(snapshot.data.onGround);
    TEST_ASSERT_TRUE(snapshot.data.idleUp);
    TEST_ASSERT_TRUE(snapshot.data.pidProfile);
    TEST_ASSERT_EQUAL(static_cast<int>(FlightMode::RaceHorizon), static_cast<int>(snapshot.data.flightMode));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 123.4f, snapshot.data.rollDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -56.7f, snapshot.data.pitchDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 180.0f, snapshot.data.relativeYawDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, snapshot.data.accelXG);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.0f, snapshot.data.accelYG);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.996f, snapshot.data.accelZG);
    TEST_ASSERT_EQUAL_UINT16(4321, snapshot.data.flightTimeSeconds);
    TEST_ASSERT_EQUAL_INT64(2000, snapshot.data.lastFlightPageUs);
    TEST_ASSERT_EQUAL_UINT16(TELEMETRY_EXTENDED_PAGE_CONTROL | TELEMETRY_EXTENDED_PAGE_FLIGHT,
                             snapshot.data.extendedPagesSeen);
}

static void test_extended_power_and_system_pages() {
    telemetry_init();
    uint8_t packet[BAYANG_PACKET_SIZE];

    make_extended_telemetry(packet, 2, 0);
    uint8_t offset = 0;
    telemetry_write_bits(packet, &offset, 3712, 16);
    telemetry_write_bits(packet, &offset, 3890, 16);
    telemetry_write_bits(packet, &offset, 198, 8);
    telemetry_write_bits(packet, &offset, 2, 8);
    telemetry_write_bits(packet, &offset, 99, 8);
    telemetry_write_bits(packet, &offset, 1, 8);
    telemetry_write_bits(packet, &offset, 350, 16);
    telemetry_write_bits(packet, &offset, 52, 8);
    telemetry_write_bits(packet, &offset, 7, 8);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 1000));

    TelemetrySnapshot snapshot = telemetry_get_snapshot(1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.712f, snapshot.data.batteryRawV);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.890f, snapshot.data.batteryCompensatedV);
    TEST_ASSERT_EQUAL_UINT16(198, snapshot.data.receiverPacketsPerSecond);
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.data.packetsLostPerSecond);
    TEST_ASSERT_EQUAL_UINT8(99, snapshot.data.linkQualityPercent);
    TEST_ASSERT_TRUE(snapshot.data.lowVoltage);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 35.0f, snapshot.data.maximumRxGapMs);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.2f, snapshot.data.currentRxGapMs);
    TEST_ASSERT_EQUAL_UINT8(7, snapshot.data.failsafeCount);

    make_extended_telemetry(packet, 3, 1);
    offset = 0;
    telemetry_write_bits(packet, &offset, 0, 1);
    telemetry_write_bits(packet, &offset, 1001, 16);
    telemetry_write_bits(packet, &offset, 1150, 16);
    telemetry_write_bits(packet, &offset, 3, 16);
    telemetry_write_bits(packet, &offset, signed_field(-321, 16), 16);
    telemetry_write_bits(packet, &offset, 0x68, 8);
    telemetry_write_bits(packet, &offset, 73, 8);
    telemetry_write_bits(packet, &offset, 12345, 15);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 2000));

    make_extended_telemetry(packet, 3, 2);
    offset = 0;
    telemetry_write_bits(packet, &offset, 1, 1);
    telemetry_write_bits(packet, &offset, 1234567, 32);
    telemetry_write_bits(packet, &offset, 2345, 32);
    telemetry_write_bits(packet, &offset, 7654321, 31);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 3000));

    snapshot = telemetry_get_snapshot(3000);
    TEST_ASSERT_EQUAL_UINT8(TELEMETRY_SYSTEM_SUBPAGE_HEALTH | TELEMETRY_SYSTEM_SUBPAGE_COUNTERS,
                            snapshot.data.systemSubpagesSeen);
    TEST_ASSERT_EQUAL_UINT16(1001, snapshot.data.loopTimeAverageUs);
    TEST_ASSERT_EQUAL_UINT16(1150, snapshot.data.loopTimeMaximumUs);
    TEST_ASSERT_EQUAL_UINT16(3, snapshot.data.loopOverrunCount);
    TEST_ASSERT_EQUAL_INT16(-321, snapshot.data.imuTemperatureRaw);
    TEST_ASSERT_EQUAL_UINT8(0x68, snapshot.data.imuType);
    TEST_ASSERT_EQUAL_UINT8(73, snapshot.data.cpuLoadPercent);
    TEST_ASSERT_EQUAL_UINT32(1234567, snapshot.data.receiverPacketTotal);
    TEST_ASSERT_EQUAL_UINT32(2345, snapshot.data.estimatedLostPacketTotal);
    TEST_ASSERT_EQUAL_UINT32(7654321, snapshot.data.telemetryTxCount);
}

static void test_extended_flight_modes() {
    struct ModeCase {
        uint16_t flags;
        FlightMode expected;
    };
    const ModeCase cases[] = {
        {0, FlightMode::Acro},
        {1U << 3, FlightMode::Level},
        {(1U << 3) | (1U << 4), FlightMode::Race},
        {(1U << 3) | (1U << 5), FlightMode::Horizon},
        {(1U << 3) | (1U << 4) | (1U << 5), FlightMode::RaceHorizon},
    };

    telemetry_init();
    for (const ModeCase& mode_case : cases) {
        uint8_t packet[BAYANG_PACKET_SIZE];
        make_extended_telemetry(packet, 1, 0);
        uint8_t offset = 0;
        for (int field = 0; field < 6; ++field)
            telemetry_write_bits(packet, &offset, 0, 12);
        telemetry_write_bits(packet, &offset, 0, 16);
        telemetry_write_bits(packet, &offset, mode_case.flags, 8);
        set_checksum(packet);
        TEST_ASSERT_TRUE(telemetry_parse(packet, 1000));
        TEST_ASSERT_EQUAL(static_cast<int>(mode_case.expected),
                          static_cast<int>(telemetry_get_snapshot(1000).data.flightMode));
    }
}

static void test_extended_sequence_checksum_and_protocol_transitions() {
    telemetry_init();
    uint8_t packet[BAYANG_PACKET_SIZE];

    make_extended_telemetry(packet, 0, 0, true);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 1000));

    packet[2] ^= 0x01;
    TEST_ASSERT_FALSE(telemetry_parse(packet, 1500));
    TelemetrySnapshot snapshot = telemetry_get_snapshot(1500);
    TEST_ASSERT_EQUAL_INT64(500, snapshot.ageUs);

    make_extended_telemetry(packet, 0, 2, true);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 2000));
    snapshot = telemetry_get_snapshot(2000);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.data.telemetryPacketsLost);

    make_valid_telemetry(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 3000));
    snapshot = telemetry_get_snapshot(3000);
    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryProtocol::Original), static_cast<int>(snapshot.data.protocol));
    TEST_ASSERT_EQUAL_UINT16(0, snapshot.data.extendedPagesSeen);
    TEST_ASSERT_EQUAL(static_cast<int>(FlightMode::Unknown), static_cast<int>(snapshot.data.flightMode));
    TEST_ASSERT_FALSE(snapshot.data.armed);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, snapshot.data.commandedThrottlePercent);
    TEST_ASSERT_EQUAL_UINT16(0x123, snapshot.data.pidI);

    make_extended_telemetry(packet, 2, 0);
    set_checksum(packet);
    TEST_ASSERT_TRUE(telemetry_parse(packet, 4000));
    snapshot = telemetry_get_snapshot(4000);
    TEST_ASSERT_EQUAL_UINT16(TELEMETRY_EXTENDED_PAGE_POWER, snapshot.data.extendedPagesSeen);
    TEST_ASSERT_EQUAL(static_cast<int>(FlightMode::Unknown), static_cast<int>(snapshot.data.flightMode));
}

static void test_pc_telemetry_crc_and_cobs() {
    const uint8_t crc_vector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x29B1, pc_telemetry_crc16_ccitt_false(crc_vector, sizeof(crc_vector)));

    const uint8_t input[] = {0x00, 0x11, 0x00, 0x22, 0x33, 0x00};
    uint8_t encoded[16] = {};
    const size_t encoded_length = pc_telemetry_cobs_encode(input, sizeof(input), encoded, sizeof(encoded));
    TEST_ASSERT_GREATER_THAN_UINT32(0, encoded_length);
    uint8_t decoded[sizeof(input)] = {};
    size_t decoded_length = 0;
    TEST_ASSERT_TRUE(
        pc_telemetry_cobs_decode(encoded, encoded_length, decoded, sizeof(decoded), &decoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(input), decoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(input, decoded, sizeof(input));

    uint8_t maximum_input[254];
    memset(maximum_input, 0xA5, sizeof(maximum_input));
    uint8_t maximum_encoded[256] = {};
    TEST_ASSERT_EQUAL_UINT32(sizeof(maximum_encoded), pc_telemetry_cobs_max_encoded_size(sizeof(maximum_input)));
    const size_t maximum_encoded_length =
        pc_telemetry_cobs_encode(maximum_input, sizeof(maximum_input), maximum_encoded, sizeof(maximum_encoded));
    TEST_ASSERT_EQUAL_UINT32(sizeof(maximum_encoded), maximum_encoded_length);
    uint8_t maximum_decoded[sizeof(maximum_input)] = {};
    TEST_ASSERT_TRUE(pc_telemetry_cobs_decode(maximum_encoded, maximum_encoded_length, maximum_decoded,
                                             sizeof(maximum_decoded), &decoded_length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(maximum_input), decoded_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(maximum_input, maximum_decoded, sizeof(maximum_input));

    const uint8_t zero_code[] = {0x00};
    const uint8_t truncated[] = {0x03, 0x11};
    TEST_ASSERT_FALSE(pc_telemetry_cobs_decode(zero_code, sizeof(zero_code), decoded, sizeof(decoded), &decoded_length));
    TEST_ASSERT_FALSE(pc_telemetry_cobs_decode(truncated, sizeof(truncated), decoded, sizeof(decoded), &decoded_length));
}

static void test_pc_telemetry_golden_frame_and_copy() {
    uint8_t packet[BAYANG_PACKET_SIZE] = {0x86, 0x2A};
    packet[14] = 0xB0;
    PcTelemetrySample sample = {};
    pc_telemetry_make_sample(&sample, packet, 0x0102030405060708ULL, 0xFFFE);
    memset(packet, 0xCC, sizeof(packet));
    TEST_ASSERT_EQUAL_HEX8(0x86, sample.packet[0]);
    TEST_ASSERT_EQUAL_HEX8(0xB0, sample.packet[14]);

    uint8_t frame[PC_TELEMETRY_MAX_FRAME_SIZE] = {};
    const size_t frame_length = pc_telemetry_encode_frame(sample, frame, sizeof(frame));
    const uint8_t golden[] = {
        0x03, 0x01, 0x01, 0x0E, 0x0F, 0xFF, 0xFE, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x86, 0x2A, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0xB0, 0x9E, 0x78, 0x00,
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(golden), frame_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, frame, sizeof(golden));

    PcTelemetrySample decoded = {};
    TEST_ASSERT_TRUE(pc_telemetry_decode_frame(frame, frame_length, &decoded));
    TEST_ASSERT_EQUAL_HEX16(0xFFFE, decoded.sequence);
    TEST_ASSERT_EQUAL_HEX64(0x0102030405060708ULL, decoded.timestampUs);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sample.packet, decoded.packet, BAYANG_PACKET_SIZE);

    frame[15] ^= 0x01;
    TEST_ASSERT_FALSE(pc_telemetry_decode_frame(frame, frame_length, &decoded));
}

static void test_pc_telemetry_saved_flight_config_overlay() {
    uint8_t packet[BAYANG_PACKET_SIZE];
    make_extended_telemetry(packet, 1, 0);
    packet[13] = 0x07;  // Preserve ground, idle-up, and low-voltage status.
    set_checksum(packet);

    const uint8_t saved_aux = NFE_SILVERWARE_AUX_LEVEL | NFE_SILVERWARE_AUX_RACE |
                              NFE_SILVERWARE_AUX_HORIZON | NFE_SILVERWARE_AUX_PID_PROFILE |
                              NFE_SILVERWARE_AUX_LEDS;
    TEST_ASSERT_TRUE(pc_telemetry_overlay_saved_flight_config(packet, saved_aux));
    TEST_ASSERT_EQUAL_HEX8(0x7F, packet[13]);
    TEST_ASSERT_TRUE(bayang_check_telemetry(packet));

    TEST_ASSERT_TRUE(pc_telemetry_overlay_saved_flight_config(packet, 0));
    TEST_ASSERT_EQUAL_HEX8(0x07, packet[13]);
    TEST_ASSERT_TRUE(bayang_check_telemetry(packet));
    TEST_ASSERT_TRUE(pc_telemetry_overlay_saved_flight_config(packet, saved_aux));

    telemetry_init();
    TEST_ASSERT_TRUE(telemetry_parse(packet, 1000));
    const TelemetrySnapshot snapshot = telemetry_get_snapshot(1000);
    TEST_ASSERT_EQUAL(static_cast<int>(FlightMode::RaceHorizon), static_cast<int>(snapshot.data.flightMode));
    TEST_ASSERT_TRUE(snapshot.data.pidProfile);
    TEST_ASSERT_TRUE(snapshot.data.onGround);
    TEST_ASSERT_TRUE(snapshot.data.idleUp);
    TEST_ASSERT_TRUE(snapshot.data.lowVoltage);

    make_extended_telemetry(packet, 0, 0);
    set_checksum(packet);
    uint8_t original[BAYANG_PACKET_SIZE];
    memcpy(original, packet, sizeof(original));
    TEST_ASSERT_FALSE(pc_telemetry_overlay_saved_flight_config(packet, saved_aux));
    TEST_ASSERT_EQUAL_MEMORY(original, packet, sizeof(packet));

    make_extended_telemetry(packet, 1, 0);
    packet[14] ^= 1U;
    memcpy(original, packet, sizeof(original));
    TEST_ASSERT_FALSE(pc_telemetry_overlay_saved_flight_config(packet, saved_aux));
    TEST_ASSERT_EQUAL_MEMORY(original, packet, sizeof(packet));
}

static void test_pc_telemetry_original_wrap_and_resynchronization() {
    uint8_t packet[BAYANG_PACKET_SIZE];
    make_valid_telemetry(packet);
    PcTelemetrySample sample = {};
    pc_telemetry_make_sample(&sample, packet, 1234, 0xFFFF);
    uint8_t frame[PC_TELEMETRY_MAX_FRAME_SIZE] = {};
    size_t frame_length = pc_telemetry_encode_frame(sample, frame, sizeof(frame));
    PcTelemetrySample decoded = {};
    TEST_ASSERT_TRUE(pc_telemetry_decode_frame(frame, frame_length, &decoded));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, decoded.sequence);
    TEST_ASSERT_EQUAL_HEX8(0x85, decoded.packet[0]);

    pc_telemetry_make_sample(&sample, packet, 1235, 0x0000);
    frame_length = pc_telemetry_encode_frame(sample, frame, sizeof(frame));
    TEST_ASSERT_TRUE(pc_telemetry_decode_frame(frame, frame_length, &decoded));
    TEST_ASSERT_EQUAL_HEX16(0x0000, decoded.sequence);

    uint8_t stream[PC_TELEMETRY_MAX_FRAME_SIZE + 3] = {0xA0, 0x55, 0x00};
    memcpy(&stream[3], frame, frame_length);
    TEST_ASSERT_FALSE(pc_telemetry_decode_frame(stream, 3, &decoded));
    TEST_ASSERT_TRUE(pc_telemetry_decode_frame(&stream[3], frame_length, &decoded));

    uint8_t record[PC_TELEMETRY_RECORD_SIZE] = {};
    size_t record_length = 0;
    TEST_ASSERT_TRUE(pc_telemetry_cobs_decode(frame, frame_length - 1, record, sizeof(record), &record_length));
    record[0] = 2;
    const uint16_t crc = pc_telemetry_crc16_ccitt_false(record, 29);
    record[29] = static_cast<uint8_t>(crc >> 8);
    record[30] = static_cast<uint8_t>(crc);
    const size_t unknown_encoded_length =
        pc_telemetry_cobs_encode(record, sizeof(record), frame, sizeof(frame) - 1);
    frame[unknown_encoded_length] = 0;
    TEST_ASSERT_FALSE(pc_telemetry_decode_frame(frame, unknown_encoded_length + 1, &decoded));

    record[0] = PC_TELEMETRY_PROTOCOL_VERSION;
    record[1] = 2;
    const uint16_t type_crc = pc_telemetry_crc16_ccitt_false(record, 29);
    record[29] = static_cast<uint8_t>(type_crc >> 8);
    record[30] = static_cast<uint8_t>(type_crc);
    const size_t unknown_type_length = pc_telemetry_cobs_encode(record, sizeof(record), frame, sizeof(frame) - 1);
    frame[unknown_type_length] = 0;
    TEST_ASSERT_FALSE(pc_telemetry_decode_frame(frame, unknown_type_length + 1, &decoded));
}

static void test_pc_telemetry_local_state_golden_frame() {
    PcTelemetryLocalState state = {};
    state.systemState = STATE_GAMEPAD_FAILSAFE;
    state.statusFlags = 0x07FF;
    state.buttons = 0x3FFF;
    state.auxModes = 0x1F;
    state.consecutiveTxFailures = 3;
    state.nextHoppingChannelIndex = 2;
    state.rollRaw = -1234;
    state.pitchRaw = 2345;
    state.yawRaw = -32768;
    state.throttleRaw = 1023;
    state.gamepadAgeMs = 42;
    state.fcTelemetryAgeMs = PC_TELEMETRY_AGE_MAX;
    state.txPackets = 0x01020304;
    state.txFailures = 0x11121314;
    state.telemetryAccepted = 0x21222324;
    state.telemetryRejected = 0x31323334;
    state.deadlineMisses = 0x41424344;
    state.exportQueueDrops = 0x51525354;

    uint8_t frame[PC_TELEMETRY_MAX_FRAME_SIZE] = {};
    const size_t frame_length =
        pc_telemetry_encode_local_state_frame(state, 0xFFFF, 0x0102030405060708ULL, frame, sizeof(frame));
    const uint8_t golden[] = {
        0x03, 0x01, 0x02, 0x19, 0x2C, 0xFF, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x05, 0x07, 0xFF, 0x3F, 0xFF, 0x1F, 0x03, 0x02, 0xFB, 0x2E, 0x09,
        0x29, 0x80, 0x03, 0x03, 0xFF, 0x1E, 0x2A, 0xFF, 0xFE, 0x01, 0x02, 0x03, 0x04,
        0x11, 0x12, 0x13, 0x14, 0x21, 0x22, 0x23, 0x24, 0x31, 0x32, 0x33, 0x34, 0x41,
        0x42, 0x43, 0x44, 0x51, 0x52, 0x53, 0x54, 0xEA, 0x8F, 0x00,
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(golden), frame_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, frame, sizeof(golden));

    memset(&state, 0, sizeof(state));
    uint16_t sequence = 0;
    uint64_t timestamp_us = 0;
    TEST_ASSERT_TRUE(pc_telemetry_decode_local_state_frame(frame, frame_length, &state, &sequence, &timestamp_us));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, sequence);
    TEST_ASSERT_EQUAL_HEX64(0x0102030405060708ULL, timestamp_us);
    TEST_ASSERT_EQUAL_UINT8(STATE_GAMEPAD_FAILSAFE, state.systemState);
    TEST_ASSERT_EQUAL_HEX16(0x07FF, state.statusFlags);
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, state.buttons);
    TEST_ASSERT_EQUAL_INT16(-1234, state.rollRaw);
    TEST_ASSERT_EQUAL_INT16(2345, state.pitchRaw);
    TEST_ASSERT_EQUAL_INT16(-32768, state.yawRaw);
    TEST_ASSERT_EQUAL_UINT32(0x51525354, state.exportQueueDrops);

    const size_t wrapped_length = pc_telemetry_encode_local_state_frame(state, 0, 9, frame, sizeof(frame));
    TEST_ASSERT_TRUE(pc_telemetry_decode_local_state_frame(frame, wrapped_length, &state, &sequence, &timestamp_us));
    TEST_ASSERT_EQUAL_HEX16(0, sequence);
}

static void test_pc_telemetry_local_state_derivation() {
    PcTelemetryLocalStateInput input = {};
    input.nowUs = 1000000;
    input.systemState = STATE_ACTIVE;
    input.controls.connected = true;
    input.controls.lastUpdateUs = 900000;
    input.controls.rollRaw = -100;
    input.controls.pitchRaw = 200;
    input.controls.yawRaw = -300;
    input.controls.throttleRaw = 400;
    input.controls.btnA = input.controls.btnB = input.controls.btnX = input.controls.btnY = true;
    input.controls.btnLB = input.controls.btnRB = input.controls.btnL3 = input.controls.btnR3 = true;
    input.controls.btnStart = input.controls.btnView = true;
    input.controls.btnDPadUp = input.controls.btnDPadDown = true;
    input.controls.btnDPadLeft = input.controls.btnDPadRight = true;
    input.auxState.levelMode = input.auxState.raceMode = input.auxState.horizonMode = true;
    input.auxState.pidProfile = input.auxState.leds = true;
    input.radioInitialized = true;
    input.consecutiveTxFailures = 2;
    input.nextHoppingChannelIndex = 7;
    input.telemetry.freshness = TelemetryFreshness::Fresh;
    input.telemetry.ageUs = 200000;
    input.txPackets = 10;
    input.txFailures = 11;
    input.telemetryAccepted = 12;
    input.telemetryRejected = 13;
    input.deadlineMisses = 14;
    input.exportQueueDrops = 15;

    PcTelemetryLocalState state = pc_telemetry_make_local_state(input);
    TEST_ASSERT_EQUAL_UINT8(STATE_ACTIVE, state.systemState);
    TEST_ASSERT_BITS_HIGH(PC_TELEMETRY_STATUS_GAMEPAD_CONNECTED | PC_TELEMETRY_STATUS_GAMEPAD_FRESH |
                              PC_TELEMETRY_STATUS_CONTROL_ENABLED | PC_TELEMETRY_STATUS_RADIO_INITIALIZED |
                              PC_TELEMETRY_STATUS_FC_TELEMETRY_SEEN | PC_TELEMETRY_STATUS_FC_TELEMETRY_FRESH,
                          state.statusFlags);
    TEST_ASSERT_BITS_LOW(PC_TELEMETRY_STATUS_SAFETY_LOCKED | PC_TELEMETRY_STATUS_BINDING |
                             PC_TELEMETRY_STATUS_GAMEPAD_FAILSAFE | PC_TELEMETRY_STATUS_RADIO_ERROR |
                             PC_TELEMETRY_STATUS_FC_TELEMETRY_STALE,
                         state.statusFlags);
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, state.buttons);
    TEST_ASSERT_EQUAL_HEX8(0x1F, state.auxModes);
    TEST_ASSERT_EQUAL_UINT8(3, state.nextHoppingChannelIndex);
    TEST_ASSERT_EQUAL_UINT16(100, state.gamepadAgeMs);
    TEST_ASSERT_EQUAL_UINT16(200, state.fcTelemetryAgeMs);
    TEST_ASSERT_EQUAL_UINT32(15, state.exportQueueDrops);

    const SystemState states[] = {STATE_BOOT,           STATE_WAIT_GAMEPAD,     STATE_BINDING,   STATE_LOCKED,
                                  STATE_ACTIVE,         STATE_GAMEPAD_FAILSAFE, STATE_RADIO_ERROR,
                                  STATE_PREARM_MODE};
    for (SystemState system_state : states) {
        input.systemState = system_state;
        state = pc_telemetry_make_local_state(input);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(system_state), state.systemState);
        TEST_ASSERT_EQUAL(system_state == STATE_ACTIVE,
                          (state.statusFlags & PC_TELEMETRY_STATUS_CONTROL_ENABLED) != 0);
        TEST_ASSERT_EQUAL(system_state != STATE_ACTIVE,
                          (state.statusFlags & PC_TELEMETRY_STATUS_SAFETY_LOCKED) != 0);
        TEST_ASSERT_EQUAL(system_state == STATE_BINDING, (state.statusFlags & PC_TELEMETRY_STATUS_BINDING) != 0);
        TEST_ASSERT_EQUAL(system_state == STATE_GAMEPAD_FAILSAFE,
                          (state.statusFlags & PC_TELEMETRY_STATUS_GAMEPAD_FAILSAFE) != 0);
        TEST_ASSERT_EQUAL(system_state == STATE_RADIO_ERROR,
                          (state.statusFlags & PC_TELEMETRY_STATUS_RADIO_ERROR) != 0);
    }
}

static void test_pc_telemetry_local_state_ages_and_schedule() {
    PcTelemetryLocalStateInput input = {};
    input.nowUs = 80000000;
    input.systemState = STATE_LOCKED;
    input.controls.connected = false;
    input.controls.lastUpdateUs = 0;
    input.telemetry.freshness = TelemetryFreshness::Never;
    PcTelemetryLocalState state = pc_telemetry_make_local_state(input);
    TEST_ASSERT_EQUAL_HEX16(PC_TELEMETRY_AGE_NEVER, state.gamepadAgeMs);
    TEST_ASSERT_EQUAL_HEX16(PC_TELEMETRY_AGE_NEVER, state.fcTelemetryAgeMs);
    TEST_ASSERT_BITS_LOW(PC_TELEMETRY_STATUS_GAMEPAD_CONNECTED | PC_TELEMETRY_STATUS_GAMEPAD_FRESH |
                             PC_TELEMETRY_STATUS_FC_TELEMETRY_SEEN | PC_TELEMETRY_STATUS_FC_TELEMETRY_FRESH |
                             PC_TELEMETRY_STATUS_FC_TELEMETRY_STALE,
                         state.statusFlags);

    input.controls.connected = true;
    input.controls.lastUpdateUs = 1;
    input.telemetry.freshness = TelemetryFreshness::Stale;
    input.telemetry.ageUs = 70000000;
    state = pc_telemetry_make_local_state(input);
    TEST_ASSERT_EQUAL_HEX16(PC_TELEMETRY_AGE_MAX, state.gamepadAgeMs);
    TEST_ASSERT_EQUAL_HEX16(PC_TELEMETRY_AGE_MAX, state.fcTelemetryAgeMs);
    TEST_ASSERT_BITS_LOW(PC_TELEMETRY_STATUS_GAMEPAD_FRESH | PC_TELEMETRY_STATUS_FC_TELEMETRY_FRESH,
                         state.statusFlags);
    TEST_ASSERT_BITS_HIGH(PC_TELEMETRY_STATUS_GAMEPAD_CONNECTED | PC_TELEMETRY_STATUS_FC_TELEMETRY_SEEN |
                              PC_TELEMETRY_STATUS_FC_TELEMETRY_STALE,
                          state.statusFlags);

    int64_t next_publish_us = 0;
    TEST_ASSERT_TRUE(pc_telemetry_local_state_due(100, &next_publish_us));
    TEST_ASSERT_EQUAL_INT64(50100, next_publish_us);
    TEST_ASSERT_FALSE(pc_telemetry_local_state_due(50099, &next_publish_us));
    TEST_ASSERT_TRUE(pc_telemetry_local_state_due(50100, &next_publish_us));
    TEST_ASSERT_EQUAL_INT64(100100, next_publish_us);
    TEST_ASSERT_TRUE(pc_telemetry_local_state_due(500000, &next_publish_us));
    TEST_ASSERT_EQUAL_INT64(550000, next_publish_us);
    TEST_ASSERT_FALSE(pc_telemetry_local_state_due(500001, &next_publish_us));
}

static void test_telemetry_never_state() {
    telemetry_init();
    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryFreshness::Never),
                      static_cast<int>(telemetry_get_snapshot(1234).freshness));
}

static void test_prearm_configuration_guard() {
    NfeSilverwareAuxState aux = {};
    aux.levelMode = true;
    aux.horizonMode = true;
    aux.pidProfile = true;

    TelemetrySnapshot snapshot = {};
    snapshot.freshness = TelemetryFreshness::Fresh;
    snapshot.data.protocol = TelemetryProtocol::ExtendedV1;
    snapshot.data.lastValidUs = 1100;
    snapshot.data.lastFlightPageUs = 1100;
    snapshot.data.flightMode = FlightMode::Horizon;
    snapshot.data.pidProfile = true;
    TEST_ASSERT_TRUE(prearm_configuration_confirmed(snapshot, aux, 1000));

    snapshot.data.lastFlightPageUs = 1000;
    TEST_ASSERT_FALSE(prearm_configuration_confirmed(snapshot, aux, 1000));
    snapshot.data.lastFlightPageUs = 1100;
    snapshot.data.armed = true;
    TEST_ASSERT_FALSE(prearm_configuration_confirmed(snapshot, aux, 1000));
    snapshot.data.armed = false;
    snapshot.data.flightMode = FlightMode::Race;
    TEST_ASSERT_FALSE(prearm_configuration_confirmed(snapshot, aux, 1000));
    snapshot.data.flightMode = FlightMode::Horizon;
    snapshot.data.pidProfile = false;
    TEST_ASSERT_FALSE(prearm_configuration_confirmed(snapshot, aux, 1000));

    snapshot.data.protocol = TelemetryProtocol::Original;
    snapshot.data.lastValidUs = 1100;
    TEST_ASSERT_TRUE(prearm_configuration_confirmed(snapshot, aux, 1000));
    snapshot.data.lastValidUs = 1000;
    TEST_ASSERT_FALSE(prearm_configuration_confirmed(snapshot, aux, 1000));
    snapshot.data.lastValidUs = 1100;
    snapshot.freshness = TelemetryFreshness::Stale;
    TEST_ASSERT_FALSE(prearm_configuration_confirmed(snapshot, aux, 1000));
}

static void test_failsafe_transitions() {
    failsafe_init();
    failsafe_update_at(0, true, 0, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());

    failsafe_update_at(1000, true, 1000, false, true, false, false);
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());

    failsafe_update_at(2000, true, 2000, true, true, false, false);
    TEST_ASSERT_EQUAL(STATE_PREARM_MODE, failsafe_get_state());
    failsafe_complete_prearm();
    TEST_ASSERT_EQUAL(STATE_ACTIVE, failsafe_get_state());

    failsafe_update_at(502000, true, 2000, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_ACTIVE, failsafe_get_state());
    failsafe_update_at(502001, true, 2000, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_GAMEPAD_FAILSAFE, failsafe_get_state());

    failsafe_update_at(503000, true, 503000, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());

    failsafe_update_at(504000, true, 504000, true, true, false, false);
    TEST_ASSERT_EQUAL(STATE_PREARM_MODE, failsafe_get_state());
    failsafe_update_at(505000, true, 505000, false, false, false, false);
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());

    failsafe_update_at(506000, true, 506000, true, true, false, false);
    TEST_ASSERT_EQUAL(STATE_PREARM_MODE, failsafe_get_state());
    failsafe_cancel_prearm();
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());
}

static void test_binding_and_radio_error_latch() {
    failsafe_init();
    failsafe_update_at(0, true, 0, true, false, false, false);
    failsafe_update_at(1000, true, 1000, true, false, true, false);
    TEST_ASSERT_EQUAL(STATE_BINDING, failsafe_get_state());
    failsafe_update_at(2001001, true, 2001001, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());

    failsafe_report_radio_error();
    failsafe_update_at(3000000, true, 3000000, true, true, true, true);
    TEST_ASSERT_EQUAL(STATE_RADIO_ERROR, failsafe_get_state());
}

struct FakeStorage {
    bool readSucceeds;
    bool writeSucceeds;
    uint8_t stored[5];
    uint8_t aux;
};

static bool fake_read_id(void* context, uint8_t* tx_id) {
    FakeStorage* storage = static_cast<FakeStorage*>(context);
    if (!storage->readSucceeds)
        return false;
    memcpy(tx_id, storage->stored, sizeof(storage->stored));
    return true;
}

static bool fake_write_id(void* context, const uint8_t* tx_id) {
    FakeStorage* storage = static_cast<FakeStorage*>(context);
    if (!storage->writeSucceeds)
        return false;
    memcpy(storage->stored, tx_id, sizeof(storage->stored));
    return true;
}

static bool fake_read_aux(void* context, uint8_t* aux_flags) {
    FakeStorage* storage = static_cast<FakeStorage*>(context);
    if (!storage->readSucceeds)
        return false;
    *aux_flags = storage->aux;
    return true;
}

static bool fake_write_aux(void* context, uint8_t aux_flags) {
    FakeStorage* storage = static_cast<FakeStorage*>(context);
    if (!storage->writeSucceeds)
        return false;
    storage->aux = aux_flags;
    return true;
}

static void test_storage_adapter_failures_and_validation() {
    FakeStorage fake = {false, false, {1, 2, 3, 4, 5}, 0};
    const SettingsStorageAdapter adapter = {&fake, fake_read_id, fake_write_id, fake_read_aux, fake_write_aux};
    uint8_t id[5] = {};

    TEST_ASSERT_FALSE(load_transmitter_id_from(adapter, id));
    fake.readSucceeds = true;
    memset(fake.stored, 0, sizeof(fake.stored));
    TEST_ASSERT_FALSE(load_transmitter_id_from(adapter, id));
    fake.stored[0] = 7;
    TEST_ASSERT_TRUE(load_transmitter_id_from(adapter, id));

    TEST_ASSERT_FALSE(save_transmitter_id_to(adapter, id));
    fake.writeSucceeds = true;
    TEST_ASSERT_TRUE(save_transmitter_id_to(adapter, id));
    memset(id, 0, sizeof(id));
    TEST_ASSERT_FALSE(save_transmitter_id_to(adapter, id));

    uint8_t aux_flags = 0;
    TEST_ASSERT_TRUE(save_aux_flags_to(adapter, SETTINGS_AUX_MASK));
    TEST_ASSERT_EQUAL_HEX8(SETTINGS_AUX_MASK, fake.aux);
    TEST_ASSERT_TRUE(load_aux_flags_from(adapter, &aux_flags));
    TEST_ASSERT_EQUAL_HEX8(SETTINGS_AUX_MASK, aux_flags);
    TEST_ASSERT_FALSE(save_aux_flags_to(adapter, SETTINGS_AUX_MASK | 0x80));
    fake.aux = 0x80;
    TEST_ASSERT_FALSE(load_aux_flags_from(adapter, &aux_flags));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_throttle_mapping);
    RUN_TEST(test_bayang_channel_mapping);
    RUN_TEST(test_start_view_and_disarm_button_edges);
    RUN_TEST(test_feedback_state_patterns);
    RUN_TEST(test_bayang_packet_flags_clamping_and_checksum);
    RUN_TEST(test_golden_centered_packet_and_individual_aux_masks);
    RUN_TEST(test_bayang_bind_packet_and_hopping);
    RUN_TEST(test_nfe_silverware_autobind_multi_profile);
    RUN_TEST(test_radio_length_guards_and_status_decode);
    RUN_TEST(test_telemetry_validation_and_freshness);
    RUN_TEST(test_extended_control_and_flight_pages);
    RUN_TEST(test_extended_power_and_system_pages);
    RUN_TEST(test_extended_flight_modes);
    RUN_TEST(test_extended_sequence_checksum_and_protocol_transitions);
    RUN_TEST(test_pc_telemetry_crc_and_cobs);
    RUN_TEST(test_pc_telemetry_golden_frame_and_copy);
    RUN_TEST(test_pc_telemetry_saved_flight_config_overlay);
    RUN_TEST(test_pc_telemetry_original_wrap_and_resynchronization);
    RUN_TEST(test_pc_telemetry_local_state_golden_frame);
    RUN_TEST(test_pc_telemetry_local_state_derivation);
    RUN_TEST(test_pc_telemetry_local_state_ages_and_schedule);
    RUN_TEST(test_telemetry_never_state);
    RUN_TEST(test_prearm_configuration_guard);
    RUN_TEST(test_failsafe_transitions);
    RUN_TEST(test_binding_and_radio_error_latch);
    RUN_TEST(test_storage_adapter_failures_and_validation);
    return UNITY_END();
}
