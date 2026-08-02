// Host-only pure-logic check (no Pico SDK needed).
// Build & run: g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp src/uart_protocol.cpp src/hid_state.cpp -o /tmp/t && /tmp/t
#include <cassert>
#include <cstdio>
#include <vector>
#include "../src/crc16.hpp"
#include "../src/uart_protocol.hpp"
#include "../src/hid_state.hpp"

static void test_crc16_known_vector() {
    const uint8_t data[] = "123456789";
    assert(crc16_ccitt_false(data, 9) == 0x29B1);
}

static std::vector<uint8_t> build_frame(uint8_t version, uint8_t type, uint16_t seq,
                                         const uint8_t* payload, uint16_t len) {
    std::vector<uint8_t> f{0xA5, 0x5A, version, type,
                            static_cast<uint8_t>(seq & 0xFF), static_cast<uint8_t>(seq >> 8),
                            static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8)};
    f.insert(f.end(), payload, payload + len);
    uint16_t crc = crc16_ccitt_false(f.data() + 2, f.size() - 2);
    f.push_back(static_cast<uint8_t>(crc & 0xFF));
    f.push_back(static_cast<uint8_t>(crc >> 8));
    return f;
}

static void test_parser_valid_ping() {
    FrameParser p;
    auto f = build_frame(1, static_cast<uint8_t>(CmdType::Ping), 42, nullptr, 0);
    ParseResult r = ParseResult::NeedMoreData;
    for (size_t i = 0; i + 1 < f.size(); ++i) {
        r = p.feed_byte(f[i], 0);
        assert(r == ParseResult::NeedMoreData);
    }
    r = p.feed_byte(f.back(), 0);
    assert(r == ParseResult::FrameReady);
    assert(p.frame().seq == 42);
    assert(p.frame().type == static_cast<uint8_t>(CmdType::Ping));
    assert(p.frame().len == 0);
}

static void test_parser_crc_error_then_resyncs() {
    FrameParser p;
    auto f = build_frame(1, static_cast<uint8_t>(CmdType::Ping), 1, nullptr, 0);
    f[f.size() - 1] ^= 0xFF; // corrupt CRC
    ParseResult r = ParseResult::NeedMoreData;
    for (auto b : f) r = p.feed_byte(b, 0);
    assert(r == ParseResult::CrcError);

    auto good = build_frame(1, static_cast<uint8_t>(CmdType::Ping), 2, nullptr, 0);
    for (size_t i = 0; i + 1 < good.size(); ++i) p.feed_byte(good[i], 0);
    assert(p.feed_byte(good.back(), 0) == ParseResult::FrameReady);
    assert(p.frame().seq == 2);
}

static void test_parser_version_error() {
    FrameParser p;
    auto f = build_frame(2, static_cast<uint8_t>(CmdType::Ping), 3, nullptr, 0);
    ParseResult r = ParseResult::NeedMoreData;
    for (auto b : f) r = p.feed_byte(b, 0);
    assert(r == ParseResult::VersionError);
}

static void test_parser_length_error() {
    FrameParser p;
    // Hand-craft a frame claiming len=200 (> MAX_PAYLOAD), no need for valid CRC/payload after.
    std::vector<uint8_t> f{0xA5, 0x5A, 1, static_cast<uint8_t>(CmdType::Ping), 0, 0, 200, 0};
    ParseResult r = ParseResult::NeedMoreData;
    for (auto b : f) r = p.feed_byte(b, 0);
    assert(r == ParseResult::LengthError);
}

static void test_parser_payload_roundtrip() {
    FrameParser p;
    uint8_t payload[2] = {0x04, 0x00};
    auto f = build_frame(1, static_cast<uint8_t>(CmdType::KeyDown), 7, payload, 1);
    ParseResult r = ParseResult::NeedMoreData;
    for (auto b : f) r = p.feed_byte(b, 0);
    assert(r == ParseResult::FrameReady);
    assert(p.frame().len == 1);
    assert(p.frame().payload[0] == 0x04);
}

static void test_parser_timeout() {
    FrameParser p;
    p.feed_byte(0xA5, 0);
    p.feed_byte(0x5A, 0);
    assert(!p.timed_out(19));
    assert(p.timed_out(21));
}

static void test_encode_decode_roundtrip() {
    uint8_t buf[16];
    size_t n = encode_ack_nack(buf, RespType::Ack, 99, ErrCode::Crc);
    FrameParser p;
    ParseResult r = ParseResult::NeedMoreData;
    for (size_t i = 0; i < n; ++i) r = p.feed_byte(buf[i], 0);
    assert(r == ParseResult::FrameReady);
    assert(p.frame().type == static_cast<uint8_t>(RespType::Ack));
    assert(p.frame().seq == 99);
    assert(p.frame().len == 0);

    n = encode_ack_nack(buf, RespType::Nack, 100, ErrCode::TooManyKeys);
    p.reset();
    for (size_t i = 0; i < n; ++i) r = p.feed_byte(buf[i], 0);
    assert(r == ParseResult::FrameReady);
    assert(p.frame().type == static_cast<uint8_t>(RespType::Nack));
    assert(p.frame().len == 1);
    assert(p.frame().payload[0] == static_cast<uint8_t>(ErrCode::TooManyKeys));
}

static void test_keyboard_6kro_overflow() {
    KeyboardState k;
    for (uint8_t i = 0; i < 6; ++i) assert(k.key_down(static_cast<uint8_t>(0x04 + i)));
    assert(!k.key_down(0x0A)); // 7th distinct key rejected
    for (uint8_t i = 0; i < 6; ++i) assert(k.keys[i] == 0x04 + i);
}

static void test_keyboard_idempotent() {
    KeyboardState k;
    assert(k.key_down(0x04));
    assert(k.key_down(0x04)); // idempotent, not a duplicate slot
    int count = 0;
    for (auto usage : k.keys) if (usage == 0x04) count++;
    assert(count == 1);
    k.key_up(0x04);
    k.key_up(0x04); // idempotent no-op
    for (auto usage : k.keys) assert(usage == 0);
}

static void test_keyboard_modifiers() {
    KeyboardState k;
    assert(k.key_down(0xE0)); // left ctrl
    assert(k.key_down(0xE5)); // right shift
    assert(k.modifiers == 0x21);
    k.key_up(0xE0);
    assert(k.modifiers == 0x20);
}

static void test_mouse_mode_switch_neutral() {
    MouseState m;
    m.set_buttons(0x01);
    m.mode = MouseMode::Relative;
    m.move_relative(5, -5);
    m.virtual_x = 100;
    m.set_mode(MouseMode::Relative);
    assert(m.virtual_x == 0 && m.virtual_y == 0);
    assert(m.buttons == 0);
    assert(m.rel_dx == 0 && m.rel_dy == 0);
}

static void test_mouse_buttons_reject_invalid_bits() {
    MouseState m;
    assert(!m.set_buttons(0xE0));
    assert(m.buttons == 0);
    assert(m.set_buttons(0x1F));
    assert(m.buttons == 0x1F);
}

static void test_mouse_absolute_range() {
    MouseState m;
    assert(m.move_absolute(32767, 0));
    assert(m.abs_x == 32767);
    assert(!m.move_absolute(32768, 0));
    assert(m.abs_x == 32767); // unchanged on rejection
}

int main() {
    test_crc16_known_vector();
    test_parser_valid_ping();
    test_parser_crc_error_then_resyncs();
    test_parser_version_error();
    test_parser_length_error();
    test_parser_payload_roundtrip();
    test_parser_timeout();
    test_encode_decode_roundtrip();
    test_keyboard_6kro_overflow();
    test_keyboard_idempotent();
    test_keyboard_modifiers();
    test_mouse_mode_switch_neutral();
    test_mouse_buttons_reject_invalid_bits();
    test_mouse_absolute_range();
    std::printf("all tests passed\n");
    return 0;
}
