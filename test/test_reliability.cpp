#include <unity.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../main/config.h"
#include "../main/gamepad/button_edges.h"
#include "../main/gamepad/control_mapping.h"
#include "../main/radio/bayang.h"
#include "../main/radio/nfe_silverware_profile.h"
#include "../main/radio/radio_validation.h"
#include "../main/safety/failsafe.h"
#include "../main/storage/settings.h"
#include "../main/telemetry/telemetry.h"

static void set_checksum(uint8_t* packet) {
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < BAYANG_PACKET_SIZE - 1; ++i)
        checksum += packet[i];
    packet[BAYANG_PACKET_SIZE - 1] = checksum;
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

    // Disarming clears every FC mode and CH5.
    nfe_silverware_update_aux(&aux, false, false, false, false, false, false);
    controls = {};
    nfe_silverware_apply_multi_aux(&controls, false, aux);
    bayang_build_data_packet(packet, &controls);
    TEST_ASSERT_EQUAL_HEX8(0, packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0, packet[3]);

    // Locked gesture passthrough changes pitch only and cannot arm or add throttle.
    controls = nfe_silverware_make_locked_control(true, 1023);
    bayang_build_data_packet(packet, &controls);
    TEST_ASSERT_EQUAL_HEX8(0, packet[2]);
    TEST_ASSERT_EQUAL_HEX8(0, packet[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, packet[6]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, packet[7]);
    TEST_ASSERT_EQUAL_HEX8(0x7C, packet[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00, packet[9]);
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

static void test_telemetry_never_state() {
    telemetry_init();
    TEST_ASSERT_EQUAL(static_cast<int>(TelemetryFreshness::Never),
                      static_cast<int>(telemetry_get_snapshot(1234).freshness));
}

static void test_failsafe_transitions() {
    failsafe_init();
    failsafe_update_at(0, true, 0, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());

    failsafe_update_at(1000, true, 1000, false, true, false, false);
    TEST_ASSERT_EQUAL(STATE_LOCKED, failsafe_get_state());

    failsafe_update_at(2000, true, 2000, true, true, false, false);
    TEST_ASSERT_EQUAL(STATE_ACTIVE, failsafe_get_state());

    failsafe_update_at(502000, true, 2000, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_ACTIVE, failsafe_get_state());
    failsafe_update_at(502001, true, 2000, true, false, false, false);
    TEST_ASSERT_EQUAL(STATE_GAMEPAD_FAILSAFE, failsafe_get_state());

    failsafe_update_at(503000, true, 503000, true, false, false, false);
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

static void test_storage_adapter_failures_and_validation() {
    FakeStorage fake = {false, false, {1, 2, 3, 4, 5}};
    const SettingsStorageAdapter adapter = {&fake, fake_read_id, fake_write_id};
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
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_throttle_mapping);
    RUN_TEST(test_bayang_channel_mapping);
    RUN_TEST(test_start_view_and_disarm_button_edges);
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
    RUN_TEST(test_telemetry_never_state);
    RUN_TEST(test_failsafe_transitions);
    RUN_TEST(test_binding_and_radio_error_latch);
    RUN_TEST(test_storage_adapter_failures_and_validation);
    return UNITY_END();
}
