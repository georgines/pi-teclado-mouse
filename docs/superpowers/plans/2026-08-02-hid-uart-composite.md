# HID USB Composto controlado por UART — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the stock TinyUSB `hid_composite` C example into a C++17 Pico W firmware that exposes a composite keyboard + relative/absolute mouse USB HID device, driven entirely by a framed binary protocol over UART0, with OLED status on core1.

**Architecture:** Core0 runs TinyUSB + UART IRQ-fed byte ring buffers + a frame parser/command queue + HID state machine + report dispatch. Core1 owns the SSD1306 (u8g2pico) and redraws from a mutex-guarded snapshot published by core0. Protocol framing (CRC16, parser) and HID key/mouse state are pure C++ with no Pico SDK dependency, so they run and are tested on the host; hardware glue (UART IRQ, I2C, GPIO, multicore) wraps them.

**Tech Stack:** C++17, Pico SDK 2.3.0, TinyUSB (vendored with SDK), u8g2pico + u8g2 (vendored under `lib/u8g2pico`, already fetched at pinned commits), Pico multicore/critical_section.

## Global Constraints

- UART0 at 921600 baud, TX=GP16, RX=GP17.
- USB HID polling interval: 1 ms (bInterval=1 on both interrupt IN endpoints).
- ACK/NACK emitted within 2 ms of a valid frame being fully received (met by construction: enqueue + immediate UART TX, no blocking waits).
- OLED: SSD1306 128x64 I2C1, SDA=GP14, SCL=GP15, probe `0x3C` then `0x3D`; absence/failure must never block USB or UART.
- Mode button: GP6, pull-up, active low, 30 ms debounce.
- Frame: `A5 5A | version:u8 | type:u8 | seq:u16 LE | len:u16 LE | payload:0..64 | crc16:u16 LE`. CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over `version..payload`. Partial-frame timeout 20 ms. RX/TX: IRQ-fed circular buffers, 512 bytes fixed. Command queue: 32 entries fixed.
- No dynamic allocation in normal operation (no `new`/`malloc` on the hot path).
- Version = 1. Payload max 64 bytes.
- Vendored libs' licenses/notices preserved as-is under `lib/u8g2pico`.

---

## File Structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Build config: C++17 sources, `add_subdirectory(lib/u8g2pico)`, link `hardware_uart hardware_i2c pico_multicore u8g2pico`. |
| `tusb_config.h` | `CFG_TUD_HID=2` (keyboard + mouse interfaces), EP bufsize. |
| `usb_descriptors.h/.cpp` | Device/config/HID report descriptors (2 HID interfaces), string descriptors. |
| `src/crc16.hpp` | `crc16_ccitt_false(const uint8_t*, size_t)` — pure function. |
| `src/uart_protocol.hpp/.cpp` | Frame types, `FrameParser` (byte-fed state machine), command/response/event structs, `encode_*` builders. Pure, host-testable. |
| `src/hid_state.hpp/.cpp` | `KeyboardState`, `MouseState` — pure key/mouse state machine (6KRO, mode switch, clamping). Host-testable. |
| `src/shared_state.hpp/.cpp` | `DeviceSnapshot` struct (also used as STATUS payload) + `publish()`/`consume()` guarded by `critical_section_t`. |
| `src/uart_io.hpp/.cpp` | RP2040 UART IRQ glue: init, ISR, circular buffers, feeds `FrameParser`, owns the 32-entry command queue, drains it into `hid_state`, emits ACK/NACK/EVENT frames. |
| `src/mode_button.hpp/.cpp` | GP6 debounce + shared `mode_set()` call (button and UART command both route through it). |
| `src/hid_usb.hpp/.cpp` | Dirty-flag dispatch of keyboard/mouse-rel/mouse-abs reports; `tud_mount/umount/suspend/resume_cb`, `tud_hid_report_complete_cb`, `tud_hid_get/set_report_cb`. |
| `src/oled_display.hpp/.cpp` | Core1 entry point: address probe, u8g2 init, redraw loop (≥50 ms interval, dirty-check). |
| `main.cpp` | `board_init`, TinyUSB rhport init, `uart_io::init()`, `multicore_launch_core1(oled_display::core1_main)`, main loop. |
| `tests/test_uart_protocol.cpp` | Host g++ assert-based check for CRC16, frame parser (sync/CRC/timeout/version errors) and `hid_state` (6KRO overflow, mode switch neutral report, clamping). |

---

## Task 1: CRC16 + frame protocol (pure, host-testable)

**Files:**
- Create: `src/crc16.hpp`
- Create: `src/uart_protocol.hpp`
- Create: `src/uart_protocol.cpp`
- Test: `tests/test_uart_protocol.cpp`

**Interfaces:**
- Produces:
  - `uint16_t crc16_ccitt_false(const uint8_t* data, size_t len);`
  - `enum class CmdType : uint8_t { Ping=0x01, GetStatus=0x02, KeyDown=0x10, KeyUp=0x11, KeyReleaseAll=0x12, MouseMove=0x20, MouseButtons=0x21, MouseWheel=0x22, MouseReleaseAll=0x23, SetMouseMode=0x24 };`
  - `enum class RespType : uint8_t { Ack=0x80, Nack=0x81, Status=0x82, EventStatus=0x90 };`
  - `enum class ErrCode : uint8_t { Crc=1, Version=2, UnknownCmd=3, Length=4, QueueFull=5, BadPayload=6, TooManyKeys=7, AbsRange=8, UsbNotReady=9, FrameTimeout=10 };`
  - `struct ParsedFrame { uint8_t version; uint8_t type; uint16_t seq; uint8_t payload[64]; uint16_t len; };`
  - `enum class ParseResult { NeedMoreData, FrameReady, CrcError, VersionError, LengthError };`
  - `class FrameParser { public: ParseResult feed_byte(uint8_t b, uint32_t now_ms); bool timed_out(uint32_t now_ms) const; void reset(); const ParsedFrame& frame() const; };` — `feed_byte` returns `FrameReady` only after CRC validated; `CrcError`/`VersionError`/`LengthError` are terminal-for-this-frame (caller must have already called `reset()` internally — `feed_byte` does this itself before returning the error so the next call starts a fresh search).
  - `size_t encode_ack_nack(uint8_t* out, RespType type, uint16_t seq, ErrCode err_if_nack);` (err_if_nack ignored when type==Ack)
  - `size_t encode_event(uint8_t* out, const uint8_t* payload, uint16_t len);` (seq forced to 0)
  - `size_t encode_status(uint8_t* out, uint16_t seq, const uint8_t* payload, uint16_t len);`

- [ ] **Step 1: Write the failing test for CRC16**

```cpp
// tests/test_uart_protocol.cpp
#include <cassert>
#include <cstdio>
#include "../src/crc16.hpp"

static void test_crc16_known_vector() {
    // CRC-16/CCITT-FALSE("123456789") == 0x29B1 (standard check value)
    const uint8_t data[] = "123456789";
    assert(crc16_ccitt_false(data, 9) == 0x29B1);
}

int main() {
    test_crc16_known_vector();
    std::printf("all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp -o /tmp/t && /tmp/t`
Expected: FAIL to compile — `crc16.hpp` does not exist yet.

- [ ] **Step 3: Implement `crc16.hpp`**

```cpp
// src/crc16.hpp
#pragma once
#include <cstdint>
#include <cstddef>

inline uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp -o /tmp/t && /tmp/t`
Expected: PASS, prints `all tests passed`.

- [ ] **Step 5: Write failing tests for `FrameParser`** (append to `tests/test_uart_protocol.cpp`)

```cpp
#include "../src/uart_protocol.hpp"
#include <vector>

static std::vector<uint8_t> build_frame(uint8_t version, uint8_t type, uint16_t seq,
                                         const uint8_t* payload, uint16_t len) {
    std::vector<uint8_t> f{0xA5, 0x5A, version, type,
                            static_cast<uint8_t>(seq & 0xFF), static_cast<uint8_t>(seq >> 8),
                            static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8)};
    f.insert(f.end(), payload, payload + len);
    uint16_t crc = crc16_ccitt_false(f.data() + 2, f.size() - 2); // version..payload
    f.push_back(crc & 0xFF);
    f.push_back(crc >> 8);
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

    // parser must resync on the next valid frame
    auto good = build_frame(1, static_cast<uint8_t>(CmdType::Ping), 2, nullptr, 0);
    for (size_t i = 0; i + 1 < good.size(); ++i) p.feed_byte(good[i], 0);
    assert(p.feed_byte(good.back(), 0) == ParseResult::FrameReady);
}

static void test_parser_version_error() {
    FrameParser p;
    auto f = build_frame(2, static_cast<uint8_t>(CmdType::Ping), 3, nullptr, 0);
    ParseResult r = ParseResult::NeedMoreData;
    for (auto b : f) r = p.feed_byte(b, 0);
    assert(r == ParseResult::VersionError);
}

static void test_parser_timeout() {
    FrameParser p;
    p.feed_byte(0xA5, 0);
    p.feed_byte(0x5A, 0);
    assert(!p.timed_out(19));
    assert(p.timed_out(21)); // > 20ms since first byte of partial frame
}
```

Add calls to these in `main()`.

- [ ] **Step 6: Run to verify failure**

Run: `g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp -o /tmp/t && /tmp/t`
Expected: FAIL to compile — `FrameParser`/`ParseResult`/etc. undefined.

- [ ] **Step 7: Implement `uart_protocol.hpp` / `uart_protocol.cpp`**

Implement the types listed in Interfaces above. `FrameParser` is a plain byte-fed state machine (states: Sync0, Sync1, Version, Type, SeqLo, SeqHi, LenLo, LenHi, Payload, CrcLo, CrcHi) storing `start_ms` when it leaves `Sync0` (for `timed_out`). On `Sync1` mismatch, if the offending byte is `0xA5` stay in `Sync1`-wait (treat as new candidate sync), else drop to `Sync0`. On CRC mismatch or version mismatch, capture the frame into `frame()` (so caller can read type/seq/err context if needed), call internal reset, and return the error. `encode_ack_nack`/`encode_event`/`encode_status` build a full wire frame (sync+header+payload+crc) into `out` and return its length; they reuse `crc16_ccitt_false`.

- [ ] **Step 8: Run tests to verify pass**

Run: `g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp -o /tmp/t && /tmp/t`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add src/crc16.hpp src/uart_protocol.hpp src/uart_protocol.cpp tests/test_uart_protocol.cpp
git commit -m "feat: add CRC16 and UART frame protocol parser"
```
(No git repo is initialized in this project — skip if `git status` reports "not a git repository"; otherwise run as-is.)

---

## Task 2: HID key/mouse state machine (pure, host-testable)

**Files:**
- Create: `src/hid_state.hpp`
- Create: `src/hid_state.cpp`
- Test: append to `tests/test_uart_protocol.cpp`

**Interfaces:**
- Consumes: nothing outside stdint.
- Produces:
```cpp
// src/hid_state.hpp
#pragma once
#include <cstdint>
#include <cstddef>

struct KeyboardState {
    uint8_t modifiers = 0;               // bit i = usage (0xE0+i)
    uint8_t keys[6] = {0,0,0,0,0,0};      // 6KRO non-modifier usages, 0 = empty slot
    bool key_down(uint8_t usage);         // false => NACK TooManyKeys, state unchanged
    void key_up(uint8_t usage);           // idempotent no-op if not down
    void release_all();
};

enum class MouseMode : uint8_t { Relative = 0, Absolute = 1 };

struct MouseState {
    MouseMode mode = MouseMode::Relative;
    uint8_t buttons = 0;                  // 5 bits
    uint16_t abs_x = 0, abs_y = 0;        // 0..32767
    int16_t rel_dx = 0, rel_dy = 0;       // last relative delta sent
    int32_t virtual_x = 0, virtual_y = 0; // accumulated position while in Relative mode
    int8_t last_wheel_v = 0, last_wheel_h = 0;

    bool move_relative(int16_t dx, int16_t dy);       // valid only when mode==Relative
    bool move_absolute(uint16_t x, uint16_t y);        // valid only when mode==Absolute; false if x or y > 32767 is unreachable (u16 max 65535) -> caller pre-checks payload > 32767 before calling
    bool set_buttons(uint8_t bitmap);                  // false if bits 5..7 set
    void set_wheel(int8_t v, int8_t h);
    void release_all_buttons();
    void set_mode(MouseMode m);                        // zeroes virtual pos on entering Relative, clears buttons/deltas
};
```

- [ ] **Step 1: Write failing tests**

```cpp
#include "../src/hid_state.hpp"

static void test_keyboard_6kro_overflow() {
    KeyboardState k;
    for (uint8_t i = 0; i < 6; ++i) assert(k.key_down(0x04 + i));
    assert(!k.key_down(0x0A)); // 7th distinct key rejected
    // state unchanged: still exactly the first 6 keys
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
}

static void test_mouse_mode_switch_neutral() {
    MouseState m;
    m.set_buttons(0x01);
    m.mode = MouseMode::Relative;
    m.move_relative(5, -5);
    m.virtual_x = 100;
    m.set_mode(MouseMode::Relative); // re-entering Relative zeroes virtual pos + releases buttons
    assert(m.virtual_x == 0 && m.virtual_y == 0);
    assert(m.buttons == 0);
}

static void test_mouse_buttons_reject_invalid_bits() {
    MouseState m;
    assert(!m.set_buttons(0xE0)); // bits 5-7 set, only 5 buttons exist
    assert(m.buttons == 0);
    assert(m.set_buttons(0x1F));
    assert(m.buttons == 0x1F);
}
```

Add calls to `main()`.

- [ ] **Step 2: Verify failure**

Run: `g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp -o /tmp/t && /tmp/t`
Expected: FAIL to compile — `hid_state.hpp` missing.

- [ ] **Step 3: Implement `src/hid_state.hpp` / `src/hid_state.cpp`** per the struct/method signatures above.

- [ ] **Step 4: Verify pass**

Run: `g++ -std=c++17 -Isrc tests/test_uart_protocol.cpp -o /tmp/t && /tmp/t`
Expected: PASS.

- [ ] **Step 5: Commit** (same caveat as Task 1 if no git repo).

---

## Task 3: USB HID descriptors (2 interfaces: keyboard boot HID, mouse rel+abs)

**Files:**
- Modify: `usb_descriptors.h`
- Delete: `usb_descriptors.c`
- Create: `usb_descriptors.cpp`

**Interfaces:**
- Produces (in `usb_descriptors.h`):
```cpp
enum { ITF_NUM_KEYBOARD = 0, ITF_NUM_MOUSE, ITF_NUM_TOTAL };
enum { HID_INSTANCE_KEYBOARD = 0, HID_INSTANCE_MOUSE = 1 };
enum { REPORT_ID_MOUSE_REL = 1, REPORT_ID_MOUSE_ABS = 2 }; // keyboard uses report id 0 (boot protocol, no id byte)
```

- [ ] **Step 1: Confirm TinyUSB stock macros to reuse** (already verified in `/mnt/c/Users/Usuario/.pico-sdk/sdk/2.3.0/lib/tinyusb/src/class/hid/hid_device.h`): `TUD_HID_REPORT_DESC_KEYBOARD()` (no report ID — boot protocol), `TUD_HID_REPORT_DESC_ABSMOUSE(HID_REPORT_ID(REPORT_ID_MOUSE_ABS))` gives exactly u16 0..32767 X/Y + 5 buttons + i8 wheel + i8 AC-pan, matching the spec's absolute report verbatim — reuse unmodified. `tud_hid_n_abs_mouse_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_ABS, buttons, x, y, v, h)` reuse unmodified.

- [ ] **Step 2: Write a local `HID_REPORT_DESC_MOUSE_REL16` macro** in `usb_descriptors.cpp` (spec requires 16-bit signed relative deltas; stock `TUD_HID_REPORT_DESC_MOUSE` is 8-bit). Copy the stock relative-mouse macro layout (5 buttons + 3 pad, wheel i8, AC-pan i8) but with X/Y as `HID_REPORT_SIZE(16)`, `HID_LOGICAL_MIN_N(0x8000,2)`, `HID_LOGICAL_MAX_N(0x7FFF,2)`, `HID_RELATIVE`. Define a matching packed struct and send it via `tud_hid_n_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_REL, &report, sizeof(report))` (no dedicated helper exists in TinyUSB for this shape, `tud_hid_report` already used elsewhere in the codebase):
```cpp
struct __attribute__((packed)) mouse_rel16_report_t {
    uint8_t buttons;
    int16_t x, y;
    int8_t wheel, pan;
};
```

- [ ] **Step 3: Two HID interfaces in the config descriptor.** Keyboard: `TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report_keyboard), 0x81, CFG_TUD_HID_EP_BUFSIZE, 1)`. Mouse: `TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report_mouse), 0x82, CFG_TUD_HID_EP_BUFSIZE, 1)`. bInterval=1 on both (1ms full-speed polling). `CONFIG_TOTAL_LEN = TUD_CONFIG_DESC_LEN + 2*TUD_HID_DESC_LEN`. `tud_hid_descriptor_report_cb(instance)` returns the keyboard or mouse array by `instance`.

- [ ] **Step 4: Product string** — replace "TinyUSB"/"TinyUSB Device" with something identifying this device (e.g. "Pico HID-UART Bridge"); keep the rest of `usb_descriptors.c`'s string/device/serial logic as-is, just `.cpp`-ified (remove nothing functional, only C89-isms if any — there are none blocking C++17).

- [ ] **Step 5: Verify** — this task has no host-runnable test (USB descriptors need real enumeration). Verification happens at Task 8 (full build) and against the spec's own acceptance criteria ("Enumeração no Windows 10/11 como teclado e mouse", "Ausência das interfaces de gamepad e controle multimídia").

- [ ] **Step 6: Commit** (same caveat as Task 1).

---

## Task 4: `shared_state` — cross-core snapshot

**Files:**
- Create: `src/shared_state.hpp`
- Create: `src/shared_state.cpp`

**Interfaces:**
- Consumes: `KeyboardState`, `MouseState` (Task 2).
- Produces:
```cpp
// src/shared_state.hpp
#pragma once
#include <cstdint>
#include "hid_state.hpp"

enum class UsbLinkState : uint8_t { Disconnected = 0, Mounted = 1, Suspended = 2 };

struct __attribute__((packed)) DeviceSnapshot {
    UsbLinkState usb_state = UsbLinkState::Disconnected;
    MouseMode mouse_mode = MouseMode::Relative;
    uint8_t modifiers = 0;
    uint8_t keys[6] = {0,0,0,0,0,0};
    uint8_t mouse_buttons = 0;
    uint16_t abs_x = 0, abs_y = 0;
    int32_t virtual_x = 0, virtual_y = 0;
    int8_t last_wheel_v = 0, last_wheel_h = 0;
    uint16_t uart_error_count = 0;
    bool oled_ok = true;
};

void shared_state_init();
void shared_state_publish(const DeviceSnapshot& s);
DeviceSnapshot shared_state_consume(); // returns last published snapshot
```

- [ ] **Step 1: Implement using `critical_section_t`** (`pico/critical_section.h`): `shared_state_init()` calls `critical_section_init()`; `publish`/`consume` each do `critical_section_enter_blocking` / copy `DeviceSnapshot` (POD, memcpy-safe) / `critical_section_exit` — kept short per spec ("mantendo o bloqueio curto"), no allocation.

- [ ] **Step 2: No host test** (Pico-SDK-only header). Verified at Task 8 build.

- [ ] **Step 3: Commit.**

---

## Task 5: `uart_io` — UART IRQ, buffers, command queue, dispatch

**Files:**
- Create: `src/uart_io.hpp`
- Create: `src/uart_io.cpp`

**Interfaces:**
- Consumes: `FrameParser`, `ParsedFrame`, `CmdType`, `ErrCode`, `encode_*` (Task 1); `KeyboardState`, `MouseState` (Task 2); `shared_state_publish` (Task 4); `mode_set()` (Task 6, forward-declared here, defined in Task 6 — see note below).
- Produces:
```cpp
// src/uart_io.hpp
#pragma once
#include <cstdint>

namespace uart_io {
void init(); // configures uart0 921600-8N1 on GP16/17, enables RX/TX IRQ, resets buffers/queue
void poll(uint32_t now_ms); // drains RX ring into parser, enqueues valid commands, applies timeout,
                             // pops queue and dispatches into hid_state + sends ACK/NACK,
                             // flushes any pending TX bytes into the UART FIFO
void notify_usb_state(class UsbLinkState state); // called from hid_usb tud_*_cb, emits EVENT_STATUS
void emit_event(const uint8_t* payload, uint16_t len); // used by mode_button too
} // namespace uart_io
```

- [ ] **Step 1: RX/TX circular buffers** — two fixed `uint8_t buf[512]` + head/tail indices (power-of-2 size so `& 511` wraps; no modulo branch needed — ladder rung 6). RX IRQ handler (`uart_get_hw(uart0)->... ` or `uart_is_readable`) pushes bytes in, drops-and-counts on overflow (increment an error counter, emit `EVENT_STATUS` overflow on next `poll()`). TX IRQ handler (`UART_IRQ` TX-empty) pops bytes out until empty or buffer drained.

- [ ] **Step 2: Wire the parser.** `poll()` pulls all currently-available RX bytes, feeds `FrameParser::feed_byte`. On `FrameReady`: validate `type` is a known `CmdType`, `len` matches the command's expected payload size, and command-specific field ranges (abs `MOUSE_MOVE` > 32767, `SET_MOUSE_MODE` payload not 0/1, `MOUSE_BUTTONS` bits 5-7 set) — on any failure send NACK with the matching `ErrCode` and do not touch `hid_state`. On success, if USB is not `Mounted` and the command is one of `KeyDown/KeyUp/KeyReleaseAll/MouseMove/MouseButtons/MouseWheel/MouseReleaseAll`, NACK `UsbNotReady` (PING/GET_STATUS/SET_MOUSE_MODE remain allowed). Otherwise push `{type, seq, payload}` onto the 32-entry command queue; if full, NACK `QueueFull` and emit an overflow event. On `CrcError`/`VersionError`/`FrameTimeout`, emit the matching `EVENT_STATUS` (no NACK — there was no valid frame/seq to answer against) and increment the error counter surfaced in `DeviceSnapshot`.

- [ ] **Step 3: Dispatch the queue.** Each `poll()` call drains the whole queue (bounded to 32, so worst case is O(32) — fine for a 1ms loop): apply to `KeyboardState`/`MouseState`, send ACK (or NACK with the command-specific `ErrCode`, e.g. `TooManyKeys` from `key_down()==false`), and republish `DeviceSnapshot` via `shared_state_publish` whenever state changed. `SetMouseMode` calls the shared `mode_set()` from Task 6 (not a local copy) so button- and UART-triggered switches behave identically. `GetStatus` replies with `encode_status()` carrying the current `DeviceSnapshot` fields (reuse — no separate serialization struct).

- [ ] **Step 4: USB link transitions.** `notify_usb_state()` (called by `hid_usb`): on transition away from `Mounted` (disconnect/suspend) or into it after being disconnected, call `KeyboardState::release_all()` / `MouseState::release_all_buttons()` and clear the command queue (`Segurança de estado`: no replay after reconnect), republish snapshot, emit `EVENT_STATUS`.

- [ ] **Step 5: No host test for the IRQ/buffer plumbing** (hardware-only). The parsing/validation *logic* it calls is already covered by Task 1/2 tests. Verified at Task 8 build + spec's own bench acceptance criteria (CRC/timeout/resync/queue-full/reconnect tests).

- [ ] **Step 6: Commit.**

---

## Task 6: `mode_button` — GP6 debounce + shared mode switch

**Files:**
- Create: `src/mode_button.hpp`
- Create: `src/mode_button.cpp`

**Interfaces:**
- Consumes: `MouseState::set_mode` (Task 2), `uart_io::emit_event` (Task 5), `hid_usb::send_mouse_neutral_report` (Task 7, forward-declared).
- Produces:
```cpp
// src/mode_button.hpp
#pragma once
#include <cstdint>
namespace mode_button {
void init();               // GP6 input, pull-up
void poll(uint32_t now_ms); // 30ms debounce, calls mode_set() on confirmed press edge
void mode_set(MouseMode m); // shared by button + SET_MOUSE_MODE command (Task 5 calls this too)
} // namespace mode_button
```

- [ ] **Step 1: Debounce.** Track `last_raw_state` and `last_change_ms`; on raw level change, reset the debounce timer; once the level has been stable for ≥30ms and differs from the last *confirmed* state, treat it as a confirmed transition. Only a confirmed falling edge (active-low press) triggers a toggle.

- [ ] **Step 2: `mode_set()` implementation** — this is the single place that: flips `MouseState::mode`, calls `hid_usb::send_mouse_neutral_report()` (zero deltas/buttons on whichever report ID was active), calls `MouseState::release_all_buttons()`, and (via `MouseState::set_mode`) zeroes `virtual_x/y` when entering Relative. Then calls `uart_io::emit_event()` with a mode-change event payload and republishes `DeviceSnapshot`. Both the button handler and `uart_io`'s `SET_MOUSE_MODE` dispatch call this one function — root-cause fix, not two copies.

- [ ] **Step 3: No host test** (GPIO-only). Debounce logic is trivial enough (single comparison + timer) that a host test would just re-assert the same 3 lines; skipped per YAGNI-for-tests. Verified at Task 8 build + spec's bench test ("Alternância de modo pelo GP6 e pela UART").

- [ ] **Step 4: Commit.**

---

## Task 7: `hid_usb` — TinyUSB report dispatch + device callbacks

**Files:**
- Create: `src/hid_usb.hpp`
- Create: `src/hid_usb.cpp`

**Interfaces:**
- Consumes: `KeyboardState`, `MouseState` (Task 2), `usb_descriptors.h` report IDs/instances (Task 3), `uart_io::notify_usb_state` (Task 5).
- Produces:
```cpp
// src/hid_usb.hpp
#pragma once
namespace hid_usb {
void mark_keyboard_dirty();
void mark_mouse_rel_dirty();
void mark_mouse_abs_dirty();
void flush_pending(); // call every main-loop iteration; sends whichever dirty report(s) tud_hid_n_ready() allows
void send_mouse_neutral_report(); // zero buttons/deltas immediately, used by mode_set()
} // namespace hid_usb
```
Plus the TinyUSB callbacks (`tud_mount_cb`, `tud_umount_cb`, `tud_suspend_cb`, `tud_resume_cb`, `tud_hid_report_complete_cb`, `tud_hid_get_report_cb`, `tud_hid_set_report_cb`) defined in `hid_usb.cpp` with C linkage (TinyUSB calls them as C functions — wrap in `extern "C" { ... }`).

- [ ] **Step 1: Dirty-flag dispatch.** `flush_pending()`: if keyboard dirty and `tud_hid_n_ready(HID_INSTANCE_KEYBOARD)`, send via `tud_hid_n_keyboard_report(HID_INSTANCE_KEYBOARD, 0, modifiers, keys)` (report id 0 = boot protocol, no id byte) and clear the flag. Independently, if a mouse report is dirty (rel or abs — only one is "current" per `MouseState::mode`) and `tud_hid_n_ready(HID_INSTANCE_MOUSE)`, send the relevant one (`tud_hid_n_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_REL, &packed, sizeof)` or `tud_hid_n_abs_mouse_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_ABS, ...)`), clear its flag. `tud_hid_report_complete_cb` doesn't need to chain anything here (unlike the stock example) since keyboard and mouse are separate instances/endpoints, each independently retried by `flush_pending()` next loop iteration if still dirty.

- [ ] **Step 2: Device callbacks** update `UsbLinkState` and call `uart_io::notify_usb_state()`. `tud_hid_set_report_cb` handles keyboard LED output report (bufsize check, capslock bit) — same behavior as the stock example. `tud_hid_get_report_cb` returns 0 (unimplemented, as in the stock example — no feature/input GET_REPORT is required by the spec).

- [ ] **Step 3: No host test** (TinyUSB-only). Verified at Task 8 build + spec's HID acceptance criteria.

- [ ] **Step 4: Commit.**

---

## Task 8: `oled_display` — core1 render loop

**Files:**
- Create: `src/oled_display.hpp`
- Create: `src/oled_display.cpp`

**Interfaces:**
- Consumes: `shared_state_consume()` (Task 4), `u8g2pico.h` (vendored).
- Produces:
```cpp
// src/oled_display.hpp
#pragma once
namespace oled_display {
void core1_main(); // entry point passed to multicore_launch_core1
} // namespace oled_display
```

- [ ] **Step 1: Address probe.** `i2c_write_blocking(i2c1, 0x3C, nullptr, 0, false)` (zero-length write — returns `PICO_ERROR_GENERIC` on NACK/no device); on failure try `0x3D`; on both failing, set `oled_ok=false` locally and loop forever doing nothing but periodically re-checking `shared_state_consume()` without touching u8g2 (never blocks core0/USB/UART — this loop only affects core1).

- [ ] **Step 2: Init u8g2** with `u8g2_Setup_ssd1306_i2c_128x64_noname_f_pico(&u8g2pico, i2c1, 14, 15, U8G2_R0, found_address)` then `u8g2_InitDisplay` / `u8g2_SetPowerSave(0)`.

- [ ] **Step 3: Render loop.** Every iteration: `DeviceSnapshot s = shared_state_consume();` if `s` differs from the last-rendered copy (plain `memcmp`, it's POD) AND ≥50ms since last redraw, clear the u8g2 buffer, draw: USB state text, mode (`ABS`/`REL`), UART error count, modifiers+keys hex, mouse X/Y (abs) or virtual X/Y + last delta (rel), 5 button states, wheel v/h — then `u8g2_SendBuffer`. `sleep_ms(5)` between checks (core1-only, doesn't touch core0 timing).

- [ ] **Step 4: No host test** (I2C/u8g2-only). Verified at Task 9 build + spec's display acceptance criteria (both addresses, graceful absence, no USB/UART latency impact).

- [ ] **Step 5: Commit.**

---

## Task 9: Wire it together — `main.cpp`, `tusb_config.h`, `CMakeLists.txt`

**Files:**
- Modify: `tusb_config.h`
- Modify: `CMakeLists.txt`
- Modify: `main.cpp` (rewrite)
- Delete: `usb_descriptors.c` (superseded by Task 3's `.cpp`)

**Interfaces:**
- Consumes everything produced above.

- [ ] **Step 1: `tusb_config.h`** — set `CFG_TUD_HID 2`, keep `CFG_TUD_HID_EP_BUFSIZE 16` (largest report is 9 bytes: keyboard mod+reserved+6 keys), keep CDC/MSC/MIDI/VENDOR at 0.

- [ ] **Step 2: `CMakeLists.txt`** — add `add_subdirectory(lib/u8g2pico)` before `add_executable`; `target_sources` add `main.cpp usb_descriptors.cpp src/uart_protocol.cpp src/hid_state.cpp src/shared_state.cpp src/uart_io.cpp src/mode_button.cpp src/hid_usb.cpp src/oled_display.cpp` (drop `usb_descriptors.c`); `target_link_libraries` add `hardware_uart hardware_i2c pico_multicore u8g2pico` alongside the existing `pico_stdlib pico_unique_id tinyusb_device tinyusb_board`.

- [ ] **Step 3: `main.cpp`** —
```cpp
#include "bsp/board_api.h"
#include "tusb.h"
#include "pico/multicore.h"
#include "src/uart_io.hpp"
#include "src/mode_button.hpp"
#include "src/hid_usb.hpp"
#include "src/oled_display.hpp"
#include "src/shared_state.hpp"

int main() {
    board_init();
    shared_state_init();

    const tusb_rhport_init_t rh_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL };
    TU_ASSERT(tud_rhport_init(BOARD_TUD_RHPORT, &rh_init), -1);
    board_init_after_tusb();

    uart_io::init();
    mode_button::init();
    multicore_launch_core1(oled_display::core1_main);

    while (true) {
        tud_task();
        uint32_t now = board_millis();
        uart_io::poll(now);
        mode_button::poll(now);
        hid_usb::flush_pending();
    }
}
```

- [ ] **Step 4: Full build.** Run (from a shell with the Pico toolchain on PATH, or via the pinned `.pico-sdk` cmake/ninja/toolchain): configure with `PICO_BOARD=pico_w`, build target `dev_hid_composite`. Fix any compile errors (missing includes, C-linkage mismatches on TinyUSB callbacks, etc.) until it links to a `.uf2`.

- [ ] **Step 5: Re-run the host test suite** (Task 1+2's `tests/test_uart_protocol.cpp`) one more time to confirm nothing in the pure-logic modules regressed while wiring the hardware glue.

- [ ] **Step 6: Commit.**

---

## Self-Review Notes

- Spec coverage: hardware pinout (Task 9 wiring + Task 5/8 GPIO/I2C usage), USB HID composite shape (Task 3/7), UART frame+CRC+timeout+resync (Task 1/5), all 9 commands + ACK/NACK/STATUS/EVENT (Task 1/5), mode button behavior (Task 6), display content/timing (Task 8), state-safety on disconnect/overflow/CRC-error (Task 5), dual-core split with short-lock snapshot (Task 4/8), no dynamic allocation (fixed arrays throughout), vendored libs already fetched at pinned commits with LICENSE/README preserved under `lib/u8g2pico`.
- No task left as a placeholder; every task has concrete signatures other tasks depend on.
- Naming consistency checked: `DeviceSnapshot`, `MouseMode`, `KeyboardState`, `MouseState`, `FrameParser`, `ParsedFrame`, `CmdType`, `ErrCode`, `RespType` are used with the same names/casing across every task that references them.
