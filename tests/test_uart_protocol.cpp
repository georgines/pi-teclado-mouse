// Host-only pure-logic check (no Pico SDK needed).
// Build & run: g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp src/uart_protocol.cpp src/hid_state.cpp src/timed_actions.cpp -o /tmp/t && /tmp/t
#include <cassert>
#include <cstdio>
#include <vector>
#include "../src/crc16.hpp"
#include "../src/uart_protocol.hpp"
#include "../src/hid_state.hpp"
#include "../src/timed_actions.hpp"

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

// --- tabela de ações temporizadas ---

static void test_hold_solta_sozinho() {
    TimedActions t;
    TimedEvent evs[MAX_TIMED_ACTIONS];
    assert(t.schedule_key_hold(0x28, 3000, 1000));
    assert(t.active_count() == 1);
    assert(t.tick(2000, evs, MAX_TIMED_ACTIONS) == 0); // dentro da janela
    assert(t.active_count() == 1);
    size_t n = t.tick(4000, evs, MAX_TIMED_ACTIONS);
    assert(n == 1 && !evs[0].press && evs[0].target == 0x28);
    assert(t.active_count() == 0);
    assert(t.tick(9000, evs, MAX_TIMED_ACTIONS) == 0); // não repete
}

static void test_hammer_alterna_e_termina_solto() {
    TimedActions t;
    TimedEvent evs[MAX_TIMED_ACTIONS];
    assert(t.schedule_key_hammer(0x52, 1000, 200, 0)); // meio período 100ms
    assert(t.tick(100, evs, MAX_TIMED_ACTIONS) == 1 && !evs[0].press); // solta
    assert(t.tick(200, evs, MAX_TIMED_ACTIONS) == 1 && evs[0].press);  // afunda de novo
    size_t n = t.tick(1000, evs, MAX_TIMED_ACTIONS);
    assert(n == 1 && !evs[0].press); // fim da janela com a tecla solta
    assert(t.active_count() == 0);
}

static void test_substitui_em_vez_de_empilhar() {
    TimedActions t;
    TimedEvent evs[MAX_TIMED_ACTIONS];
    assert(t.schedule_key_hold(0x04, 1000, 0));
    assert(t.schedule_key_hold(0x04, 5000, 0)); // mesmo usage: substitui
    assert(t.active_count() == 1);
    assert(t.tick(1500, evs, MAX_TIMED_ACTIONS) == 0); // contagem reiniciada
    assert(t.tick(5000, evs, MAX_TIMED_ACTIONS) == 1);
}

static void test_tabela_cheia_recusa_a_decima_primeira() {
    TimedActions t;
    for (uint8_t i = 0; i < MAX_TIMED_ACTIONS; ++i) {
        assert(t.schedule_key_hold(static_cast<uint8_t>(0x04 + i), 1000, 0));
    }
    assert(!t.schedule_key_hold(0x50, 1000, 0)); // décima primeira
    assert(t.active_count() == MAX_TIMED_ACTIONS); // as dez vigentes intactas
}

static void test_teclado_e_contatos_dividem_a_tabela() {
    TimedActions t;
    TimedEvent evs[MAX_TIMED_ACTIONS];
    assert(t.schedule_key_hold(0x04, 1000, 0));
    assert(t.schedule_contact_pulse(1, 500, 0));
    assert(t.active_count() == 2);
    // Mesmo número de alvo em domínios diferentes não colide.
    assert(t.schedule_key_hold(1, 1000, 0));
    assert(t.active_count() == 3);
    size_t n = t.tick(500, evs, MAX_TIMED_ACTIONS);
    assert(n == 1 && evs[0].kind == TimedKind::ContactPulse && evs[0].target == 1 && !evs[0].press);
}

static void test_cancelamentos_soltam_o_alvo() {
    TimedActions t;
    TimedEvent evs[MAX_TIMED_ACTIONS];
    assert(t.schedule_key_hold(0x04, 5000, 0));
    assert(t.schedule_key_hold(0x05, 5000, 0));
    assert(t.schedule_contact_pulse(1, 5000, 0));

    size_t n = t.cancel_key(0x04, evs, MAX_TIMED_ACTIONS);
    assert(n == 1 && evs[0].target == 0x04 && !evs[0].press);
    assert(!t.has_key(0x04) && t.has_key(0x05));

    n = t.cancel_keys(evs, MAX_TIMED_ACTIONS);
    assert(n == 1 && evs[0].target == 0x05);
    assert(t.active_count() == 1); // o contato sobrevive ao release_all do teclado

    n = t.cancel_all(evs, MAX_TIMED_ACTIONS);
    assert(n == 1 && evs[0].kind == TimedKind::ContactPulse && !evs[0].press);
    assert(t.active_count() == 0);
    assert(t.tick(6000, evs, MAX_TIMED_ACTIONS) == 0); // nada é reproduzido depois
}

static void test_relogio_da_a_volta() {
    TimedActions t;
    TimedEvent evs[MAX_TIMED_ACTIONS];
    const uint32_t quase_fim = 0xFFFFFF00u;
    assert(t.schedule_key_hold(0x28, 1000, quase_fim)); // vence depois do wrap
    assert(t.tick(quase_fim + 500, evs, MAX_TIMED_ACTIONS) == 0);
    assert(t.tick(static_cast<uint32_t>(quase_fim + 1000), evs, MAX_TIMED_ACTIONS) == 1);
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
    test_hold_solta_sozinho();
    test_hammer_alterna_e_termina_solto();
    test_substitui_em_vez_de_empilhar();
    test_tabela_cheia_recusa_a_decima_primeira();
    test_teclado_e_contatos_dividem_a_tabela();
    test_cancelamentos_soltam_o_alvo();
    test_relogio_da_a_volta();
    std::printf("all tests passed\n");
    return 0;
}
