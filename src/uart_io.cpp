#include "uart_io.hpp"
#include "uart_protocol.hpp"
#include "hid_state.hpp"
#include "hid_usb.hpp"
#include "mode_button.hpp"
#include "timed_actions.hpp"
#include "contacts.hpp"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

namespace uart_io {
namespace {

uart_inst_t* const UART_ID = uart0;
constexpr uint UART_TX_PIN = 16;
constexpr uint UART_RX_PIN = 17;
constexpr uint32_t BAUD_RATE = 921600;

constexpr size_t RING_SIZE = 512; // power of 2, so index wrap is a mask
constexpr size_t RING_MASK = RING_SIZE - 1;
constexpr size_t CMD_QUEUE_SIZE = 32;

volatile uint8_t rx_buf[RING_SIZE];
volatile size_t rx_head = 0;
volatile size_t rx_tail = 0;
volatile bool rx_overflow = false;

volatile uint8_t tx_buf[RING_SIZE];
volatile size_t tx_head = 0;
volatile size_t tx_tail = 0;

struct PendingCommand {
    CmdType type;
    uint16_t seq;
    uint8_t payload[MAX_PAYLOAD];
    uint16_t len;
};
PendingCommand cmd_queue[CMD_QUEUE_SIZE];
size_t cmd_head = 0, cmd_tail = 0, cmd_count = 0;

FrameParser parser;
UsbLinkState usb_state = UsbLinkState::Disconnected;
uint16_t error_count = 0;
bool last_oled_ok = true;

// Drena o anel de transmissão para a FIFO do UART e deixa a interrupção de TX
// ligada apenas enquanto sobrar byte no anel.
//
// A interrupção de transmissão do PL011 só é gerada quando o nível da FIFO
// CRUZA o limiar de disparo, de cima para baixo. Habilitá-la com a FIFO vazia
// não produz cruzamento nenhum: a interrupção nunca chega, o anel nunca é
// drenado e a transmissão trava para sempre. Por isso quem enfileira escreve
// na FIFO aqui mesmo, em vez de esperar por uma interrupção que não viria.
void pump_tx() {
    while (uart_is_writable(UART_ID) && tx_tail != tx_head) {
        uart_putc_raw(UART_ID, static_cast<char>(tx_buf[tx_tail]));
        tx_tail = (tx_tail + 1) & RING_MASK;
    }
    uart_set_irq_enables(UART_ID, true, tx_tail != tx_head);
}

// pump_tx() a partir do laço principal. tx_tail também é escrito pela ISR, daí
// o mascaramento.
void pump_tx_locked() {
    irq_set_enabled(UART0_IRQ, false);
    pump_tx();
    irq_set_enabled(UART0_IRQ, true);
}

void on_uart_irq() {
    while (uart_is_readable(UART_ID)) {
        uint8_t b = static_cast<uint8_t>(uart_getc(UART_ID));
        size_t next = (rx_head + 1) & RING_MASK;
        if (next != rx_tail) {
            rx_buf[rx_head] = b;
            rx_head = next;
        } else {
            rx_overflow = true; // ring full: drop the byte, poll() reports it
        }
    }
    pump_tx();
}

void queue_tx(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        size_t next = (tx_head + 1) & RING_MASK;
        if (next == tx_tail) break; // TX ring full: drop remainder (frames are far smaller than 512B)
        tx_buf[tx_head] = data[i];
        tx_head = next;
    }
    pump_tx_locked();
}

void send_ack(uint16_t seq) {
    uint8_t buf[10];
    queue_tx(buf, encode_ack_nack(buf, RespType::Ack, seq, ErrCode::Crc));
}

void send_nack(uint16_t seq, ErrCode err) {
    uint8_t buf[11];
    queue_tx(buf, encode_ack_nack(buf, RespType::Nack, seq, err));
}

void emit_event1(EventCode code) {
    uint8_t p[1] = {static_cast<uint8_t>(code)};
    emit_event(p, 1);
}

DeviceSnapshot build_snapshot() {
    DeviceSnapshot s;
    s.usb_state = usb_state;
    s.mouse_mode = g_mouse_state.mode;
    s.modifiers = g_keyboard_state.modifiers;
    for (int i = 0; i < 6; ++i) s.keys[i] = g_keyboard_state.keys[i];
    s.mouse_buttons = g_mouse_state.buttons;
    s.abs_x = g_mouse_state.abs_x;
    s.abs_y = g_mouse_state.abs_y;
    s.virtual_x = g_mouse_state.virtual_x;
    s.virtual_y = g_mouse_state.virtual_y;
    s.last_wheel_v = g_mouse_state.last_wheel_v;
    s.last_wheel_h = g_mouse_state.last_wheel_h;
    s.uart_error_count = error_count;
    s.oled_ok = shared_state_get_oled_ok();
    return s;
}

void send_status(uint16_t seq) {
    DeviceSnapshot snap = build_snapshot();
    uint8_t buf[10 + sizeof(DeviceSnapshot)];
    queue_tx(buf, encode_status(buf, seq, reinterpret_cast<const uint8_t*>(&snap), sizeof(snap)));
}

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

// Aplica as transições devolvidas pela tabela de temporizadores: tecla afunda
// ou solta, contato fecha ou abre.
void apply_timed_events(const TimedEvent* evs, size_t n) {
    bool keyboard_touched = false;
    for (size_t i = 0; i < n; ++i) {
        if (evs[i].kind == TimedKind::ContactPulse) {
            contacts::set(evs[i].target, evs[i].press);
            continue;
        }
        if (evs[i].press) {
            g_keyboard_state.key_down(evs[i].target);
        } else {
            g_keyboard_state.key_up(evs[i].target);
        }
        keyboard_touched = true;
    }
    if (keyboard_touched) hid_usb::mark_keyboard_dirty();
}

void cancel_key_timer(uint8_t usage) {
    TimedEvent evs[MAX_TIMED_ACTIONS];
    apply_timed_events(evs, g_timed_actions.cancel_key(usage, evs, MAX_TIMED_ACTIONS));
}

void cancel_contact_timer(uint8_t contact) {
    TimedEvent evs[MAX_TIMED_ACTIONS];
    apply_timed_events(evs, g_timed_actions.cancel_contact(contact, evs, MAX_TIMED_ACTIONS));
}

void cancel_keyboard_timers() {
    TimedEvent evs[MAX_TIMED_ACTIONS];
    apply_timed_events(evs, g_timed_actions.cancel_keys(evs, MAX_TIMED_ACTIONS));
}

// Rede de segurança: nenhum temporizador pendente e nenhum contato fechado.
// Um temporizador pendente é uma tecla que seria afundada no futuro, e um
// contato fechado é o botão de força segurado, possivelmente sem ninguém no
// controle.
void abort_all_timed() {
    TimedEvent evs[MAX_TIMED_ACTIONS];
    apply_timed_events(evs, g_timed_actions.cancel_all(evs, MAX_TIMED_ACTIONS));
    contacts::open_all();
}

void send_contacts(uint16_t seq) {
    uint8_t payload[1] = {contacts::bitmap()};
    uint8_t buf[11];
    queue_tx(buf, encode_frame(buf, static_cast<uint8_t>(RespType::Contacts), seq, payload, 1));
}

void clear_hid_state_and_queue() {
    abort_all_timed();
    g_keyboard_state.release_all();
    g_mouse_state.release_all_buttons();
    cmd_head = cmd_tail = cmd_count = 0;
    hid_usb::mark_keyboard_dirty();
    hid_usb::mark_mouse_rel_dirty();
    hid_usb::mark_mouse_abs_dirty();
}

bool is_known_cmd(uint8_t t) {
    switch (static_cast<CmdType>(t)) {
        case CmdType::Ping: case CmdType::GetStatus: case CmdType::KeyDown: case CmdType::KeyUp:
        case CmdType::KeyReleaseAll: case CmdType::KeyHold: case CmdType::KeyHammer:
        case CmdType::MouseMove: case CmdType::MouseButtons:
        case CmdType::MouseWheel: case CmdType::MouseReleaseAll: case CmdType::SetMouseMode:
        case CmdType::ContactPulse: case CmdType::ContactDown: case CmdType::ContactUp:
        case CmdType::GetContacts:
            return true;
    }
    return false;
}

bool usb_ready_for_hid() { return usb_state == UsbLinkState::Mounted; }

void mark_mouse_dirty_for_current_mode() {
    (g_mouse_state.mode == MouseMode::Relative) ? hid_usb::mark_mouse_rel_dirty()
                                                 : hid_usb::mark_mouse_abs_dirty();
}

bool apply_command(const PendingCommand& c, uint32_t now_ms, ErrCode& err) {
    switch (c.type) {
        case CmdType::Ping:
            return true;

        case CmdType::GetStatus:
            return true; // dispatch_one() special-cases this to send STATUS, not ACK

        case CmdType::KeyDown:
            if (c.len != 1) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            if (!g_keyboard_state.key_down(c.payload[0])) { err = ErrCode::TooManyKeys; return false; }
            hid_usb::mark_keyboard_dirty();
            return true;

        case CmdType::KeyUp:
            if (c.len != 1) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            // Quem manda soltar, soltou: um KEY_HOLD/KEY_HAMMER em curso sobre
            // esse usage morre aqui.
            cancel_key_timer(c.payload[0]);
            g_keyboard_state.key_up(c.payload[0]);
            hid_usb::mark_keyboard_dirty();
            return true;

        case CmdType::KeyReleaseAll:
            if (c.len != 0) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            // Sem cancelar os temporizadores, o release_all do host deixaria de
            // ser rede de segurança: soltaria o que está afundado e um
            // temporizador afundaria de novo depois.
            cancel_keyboard_timers();
            g_keyboard_state.release_all();
            hid_usb::mark_keyboard_dirty();
            return true;

        case CmdType::KeyHold:
        case CmdType::KeyHammer: {
            const bool hammer = (c.type == CmdType::KeyHammer);
            if (c.len != (hammer ? 5 : 3)) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            const uint8_t usage = c.payload[0];
            const uint16_t duration = rd16(&c.payload[1]);
            const uint16_t interval = hammer ? rd16(&c.payload[3]) : 0;
            if (duration == 0) { err = ErrCode::BadPayload; return false; }
            if (hammer && (interval == 0 || interval > duration)) {
                err = ErrCode::BadPayload;
                return false;
            }
            const bool scheduled =
                hammer ? g_timed_actions.schedule_key_hammer(usage, duration, interval, now_ms)
                       : g_timed_actions.schedule_key_hold(usage, duration, now_ms);
            if (!scheduled) { err = ErrCode::QueueFull; return false; }
            if (!g_keyboard_state.key_down(usage)) {
                // key_down() só recusa um usage que ainda não estava afundado —
                // portanto sem temporizador anterior que o agendamento acima
                // pudesse ter substituído. Desfazer é seguro.
                cancel_key_timer(usage);
                err = ErrCode::TooManyKeys;
                return false;
            }
            hid_usb::mark_keyboard_dirty();
            return true;
        }

        case CmdType::MouseMove: {
            if (c.len != 4) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            uint16_t raw_x = static_cast<uint16_t>(c.payload[0] | (c.payload[1] << 8));
            uint16_t raw_y = static_cast<uint16_t>(c.payload[2] | (c.payload[3] << 8));
            if (g_mouse_state.mode == MouseMode::Relative) {
                g_mouse_state.move_relative(static_cast<int16_t>(raw_x), static_cast<int16_t>(raw_y));
            } else if (!g_mouse_state.move_absolute(raw_x, raw_y)) {
                err = ErrCode::AbsRange;
                return false;
            }
            mark_mouse_dirty_for_current_mode();
            return true;
        }

        case CmdType::MouseButtons:
            if (c.len != 1) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            if (!g_mouse_state.set_buttons(c.payload[0])) { err = ErrCode::BadPayload; return false; }
            mark_mouse_dirty_for_current_mode();
            return true;

        case CmdType::MouseWheel:
            if (c.len != 2) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            g_mouse_state.set_wheel(static_cast<int8_t>(c.payload[0]), static_cast<int8_t>(c.payload[1]));
            mark_mouse_dirty_for_current_mode();
            return true;

        case CmdType::MouseReleaseAll:
            if (c.len != 0) { err = ErrCode::Length; return false; }
            if (!usb_ready_for_hid()) { err = ErrCode::UsbNotReady; return false; }
            g_mouse_state.release_all_buttons();
            mark_mouse_dirty_for_current_mode();
            return true;

        case CmdType::SetMouseMode:
            if (c.len != 1 || (c.payload[0] != 0 && c.payload[0] != 1)) {
                err = ErrCode::BadPayload;
                return false;
            }
            mode_button::mode_set(c.payload[0] == 0 ? MouseMode::Relative : MouseMode::Absolute);
            return true;

        // Os contatos NÃO exigem USB pronto: a máquina controlada desligada é
        // exatamente o caso em que o host precisa fechar o contato de força, e
        // com ela desligada não existe enumeração USB para esperar.
        case CmdType::ContactPulse: {
            if (c.len != 3) { err = ErrCode::Length; return false; }
            const uint8_t contact = c.payload[0];
            const uint16_t duration = rd16(&c.payload[1]);
            if (!contacts::valid(contact) || duration == 0) {
                err = ErrCode::BadPayload;
                return false;
            }
            if (!g_timed_actions.schedule_contact_pulse(contact, duration, now_ms)) {
                err = ErrCode::QueueFull;
                return false;
            }
            contacts::set(contact, true);
            return true;
        }

        case CmdType::ContactDown:
            if (c.len != 1) { err = ErrCode::Length; return false; }
            if (!contacts::valid(c.payload[0])) { err = ErrCode::BadPayload; return false; }
            contacts::set(c.payload[0], true);
            return true;

        case CmdType::ContactUp:
            if (c.len != 1) { err = ErrCode::Length; return false; }
            if (!contacts::valid(c.payload[0])) { err = ErrCode::BadPayload; return false; }
            cancel_contact_timer(c.payload[0]);
            contacts::set(c.payload[0], false);
            return true;

        case CmdType::GetContacts:
            if (c.len != 0) { err = ErrCode::Length; return false; }
            return true; // dispatch_one() responde CONTACTS, não ACK
    }
    err = ErrCode::UnknownCmd;
    return false;
}

void dispatch_one(const PendingCommand& c, uint32_t now_ms) {
    if (c.type == CmdType::GetStatus) {
        send_status(c.seq);
        return;
    }
    ErrCode err = ErrCode::BadPayload;
    if (apply_command(c, now_ms, err)) {
        if (c.type == CmdType::GetContacts) {
            send_contacts(c.seq);
            return;
        }
        send_ack(c.seq);
        republish_snapshot();
    } else {
        send_nack(c.seq, err);
    }
}

void enqueue_command(const ParsedFrame& f) {
    if (!is_known_cmd(f.type)) {
        send_nack(f.seq, ErrCode::UnknownCmd);
        error_count++;
        emit_event1(EventCode::UnknownCommand);
        return;
    }
    if (cmd_count >= CMD_QUEUE_SIZE) {
        send_nack(f.seq, ErrCode::QueueFull);
        error_count++;
        emit_event1(EventCode::QueueOverflow);
        abort_all_timed();
        return;
    }
    PendingCommand& slot = cmd_queue[cmd_head];
    slot.type = static_cast<CmdType>(f.type);
    slot.seq = f.seq;
    slot.len = f.len;
    for (uint16_t i = 0; i < f.len; ++i) slot.payload[i] = f.payload[i];
    cmd_head = (cmd_head + 1) % CMD_QUEUE_SIZE;
    cmd_count++;
}

} // namespace

void init() {
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    rx_head = rx_tail = 0;
    tx_head = tx_tail = 0;
    rx_overflow = false;

    irq_set_exclusive_handler(UART0_IRQ, on_uart_irq);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false); // RX always on, TX enabled on demand
}

void poll(uint32_t now_ms) {
    // Se um quadro sobrou no anel sem ninguém enfileirar mais nada, ele sai
    // aqui em vez de esperar a próxima resposta.
    pump_tx_locked();

    while (rx_tail != rx_head) {
        uint8_t b = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & RING_MASK;
        ParseResult r = parser.feed_byte(b, now_ms);
        switch (r) {
            case ParseResult::FrameReady:
                enqueue_command(parser.frame());
                break;
            case ParseResult::CrcError:
                error_count++;
                emit_event1(EventCode::CrcError);
                break;
            case ParseResult::VersionError:
                error_count++;
                emit_event1(EventCode::VersionError);
                break;
            case ParseResult::LengthError:
                error_count++;
                emit_event1(EventCode::LengthError);
                break;
            case ParseResult::NeedMoreData:
                break;
        }
    }

    if (parser.timed_out(now_ms)) {
        parser.reset();
        error_count++;
        emit_event1(EventCode::FrameTimeout);
    }

    if (rx_overflow) {
        rx_overflow = false;
        error_count++;
        emit_event1(EventCode::BufferOverflow);
        abort_all_timed();
    }

    while (cmd_count > 0) {
        PendingCommand c = cmd_queue[cmd_tail];
        cmd_tail = (cmd_tail + 1) % CMD_QUEUE_SIZE;
        cmd_count--;
        dispatch_one(c, now_ms);
    }

    TimedEvent evs[MAX_TIMED_ACTIONS];
    size_t fired = g_timed_actions.tick(now_ms, evs, MAX_TIMED_ACTIONS);
    if (fired > 0) {
        apply_timed_events(evs, fired);
        republish_snapshot();
    }

    bool oled_ok = shared_state_get_oled_ok();
    if (oled_ok != last_oled_ok) {
        last_oled_ok = oled_ok;
        emit_event1(oled_ok ? EventCode::DisplayRecovered : EventCode::DisplayFailure);
        republish_snapshot();
    }
}

void notify_usb_state(UsbLinkState state) {
    UsbLinkState prev = usb_state;
    usb_state = state;

    // Leaving or (re-)entering Mounted both mean "start clean": no replay of
    // stale input after a reconnect, no leftover keys/buttons after a drop.
    if ((prev == UsbLinkState::Mounted) != (state == UsbLinkState::Mounted)) {
        clear_hid_state_and_queue();
    }

    switch (state) {
        case UsbLinkState::Mounted: emit_event1(EventCode::UsbConnected); break;
        case UsbLinkState::Disconnected: emit_event1(EventCode::UsbDisconnected); break;
        case UsbLinkState::Suspended: emit_event1(EventCode::UsbSuspended); break;
    }
    republish_snapshot();
}

void emit_event(const uint8_t* payload, uint16_t len) {
    uint8_t buf[10 + MAX_PAYLOAD];
    queue_tx(buf, encode_event(buf, payload, len));
}

void republish_snapshot() {
    shared_state_publish(build_snapshot());
}

} // namespace uart_io
