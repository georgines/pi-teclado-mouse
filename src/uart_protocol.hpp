#pragma once
#include <cstdint>
#include <cstddef>

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t MAX_PAYLOAD = 64;

enum class CmdType : uint8_t {
    Ping = 0x01,
    GetStatus = 0x02,
    KeyDown = 0x10,
    KeyUp = 0x11,
    KeyReleaseAll = 0x12,
    KeyHold = 0x14,
    KeyHammer = 0x15,
    MouseMove = 0x20,
    MouseButtons = 0x21,
    MouseWheel = 0x22,
    MouseReleaseAll = 0x23,
    SetMouseMode = 0x24,
    ContactPulse = 0x30,
    ContactDown = 0x31,
    ContactUp = 0x32,
    GetContacts = 0x33,
};

enum class RespType : uint8_t {
    Ack = 0x80,
    Nack = 0x81,
    Status = 0x82,
    Contacts = 0x83,
    EventStatus = 0x90,
};

enum class ErrCode : uint8_t {
    Crc = 1,
    Version = 2,
    UnknownCmd = 3,
    Length = 4,
    QueueFull = 5,
    BadPayload = 6,
    TooManyKeys = 7,
    AbsRange = 8,
    UsbNotReady = 9,
    FrameTimeout = 10,
};

// First payload byte of an EVENT_STATUS frame; remaining bytes (if any) are
// event-specific (currently only MouseModeChanged carries one: the new mode).
enum class EventCode : uint8_t {
    UsbConnected = 1,
    UsbDisconnected = 2,
    UsbSuspended = 3,
    MouseModeChanged = 4,
    DisplayFailure = 5,
    DisplayRecovered = 6,
    BufferOverflow = 7,
    QueueOverflow = 8,
    CrcError = 9,
    LengthError = 10,
    VersionError = 11,
    FrameTimeout = 12,
    UnknownCommand = 13,
};

struct ParsedFrame {
    uint8_t version = 0;
    uint8_t type = 0;
    uint16_t seq = 0;
    uint8_t payload[MAX_PAYLOAD] = {};
    uint16_t len = 0;
};

enum class ParseResult { NeedMoreData, FrameReady, CrcError, VersionError, LengthError };

// Byte-fed frame parser. Pure logic, no I/O — the caller owns reading bytes
// from wherever (UART ISR-fed ring buffer in production, a test vector on host).
class FrameParser {
public:
    ParseResult feed_byte(uint8_t b, uint32_t now_ms);

    // True once a frame is partially received (past the sync bytes) and more
    // than 20ms have elapsed since the first sync byte of that frame. Caller
    // must call reset() after observing a timeout.
    bool timed_out(uint32_t now_ms) const;

    void reset();

    const ParsedFrame& frame() const { return frame_; }

private:
    enum class State {
        Sync0, Sync1, Version, Type, SeqLo, SeqHi, LenLo, LenHi, Payload, CrcLo, CrcHi
    };

    State state_ = State::Sync0;
    ParsedFrame frame_{};
    uint16_t payload_idx_ = 0;
    uint16_t recv_crc_ = 0;
    uint32_t frame_start_ms_ = 0;

    // Raw bytes from `version` through the end of `payload` — exactly what the
    // CRC covers. Max size: 6 header bytes + MAX_PAYLOAD.
    uint8_t raw_[6 + MAX_PAYLOAD];
    size_t raw_len_ = 0;
};

// Builds a complete wire frame (sync + header + payload + crc) into `out`.
// `out` must have room for 10 + len bytes. Returns the frame length.
size_t encode_frame(uint8_t* out, uint8_t type, uint16_t seq, const uint8_t* payload, uint16_t len);
size_t encode_ack(uint8_t* out, uint16_t seq);
size_t encode_nack(uint8_t* out, uint16_t seq, ErrCode err);
size_t encode_event(uint8_t* out, const uint8_t* payload, uint16_t len);
size_t encode_status(uint8_t* out, uint16_t seq, const uint8_t* payload, uint16_t len);
