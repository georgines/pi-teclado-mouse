#include "uart_protocol.hpp"
#include "crc16.hpp"

ParseResult FrameParser::feed_byte(uint8_t b, uint32_t now_ms) {
    switch (state_) {
        case State::Sync0:
            if (b == 0xA5) {
                state_ = State::Sync1;
                frame_start_ms_ = now_ms;
                raw_len_ = 0;
            }
            return ParseResult::NeedMoreData;

        case State::Sync1:
            if (b == 0x5A) {
                state_ = State::Version;
            } else if (b == 0xA5) {
                // Treat this byte as a fresh sync candidate instead of dropping to Sync0.
                frame_start_ms_ = now_ms;
            } else {
                state_ = State::Sync0;
            }
            return ParseResult::NeedMoreData;

        case State::Version:
            frame_.version = b;
            raw_[raw_len_++] = b;
            state_ = State::Type;
            return ParseResult::NeedMoreData;

        case State::Type:
            frame_.type = b;
            raw_[raw_len_++] = b;
            state_ = State::SeqLo;
            return ParseResult::NeedMoreData;

        case State::SeqLo:
            frame_.seq = b;
            raw_[raw_len_++] = b;
            state_ = State::SeqHi;
            return ParseResult::NeedMoreData;

        case State::SeqHi:
            frame_.seq = static_cast<uint16_t>(frame_.seq | (static_cast<uint16_t>(b) << 8));
            raw_[raw_len_++] = b;
            state_ = State::LenLo;
            return ParseResult::NeedMoreData;

        case State::LenLo:
            frame_.len = b;
            raw_[raw_len_++] = b;
            state_ = State::LenHi;
            return ParseResult::NeedMoreData;

        case State::LenHi:
            frame_.len = static_cast<uint16_t>(frame_.len | (static_cast<uint16_t>(b) << 8));
            raw_[raw_len_++] = b;
            if (frame_.len > MAX_PAYLOAD) {
                reset();
                return ParseResult::LengthError;
            }
            payload_idx_ = 0;
            state_ = (frame_.len == 0) ? State::CrcLo : State::Payload;
            return ParseResult::NeedMoreData;

        case State::Payload:
            frame_.payload[payload_idx_++] = b;
            raw_[raw_len_++] = b;
            if (payload_idx_ >= frame_.len) {
                state_ = State::CrcLo;
            }
            return ParseResult::NeedMoreData;

        case State::CrcLo:
            recv_crc_ = b;
            state_ = State::CrcHi;
            return ParseResult::NeedMoreData;

        case State::CrcHi: {
            recv_crc_ = static_cast<uint16_t>(recv_crc_ | (static_cast<uint16_t>(b) << 8));
            uint16_t calc = crc16_ccitt_false(raw_, raw_len_);
            uint8_t version = frame_.version;
            reset();
            if (calc != recv_crc_) return ParseResult::CrcError;
            if (version != PROTOCOL_VERSION) return ParseResult::VersionError;
            return ParseResult::FrameReady;
        }
    }
    return ParseResult::NeedMoreData;
}

bool FrameParser::timed_out(uint32_t now_ms) const {
    return state_ != State::Sync0 && (now_ms - frame_start_ms_) > 20;
}

void FrameParser::reset() {
    state_ = State::Sync0;
    raw_len_ = 0;
    payload_idx_ = 0;
}

size_t encode_frame(uint8_t* out, uint8_t type, uint16_t seq, const uint8_t* payload, uint16_t len) {
    out[0] = 0xA5;
    out[1] = 0x5A;
    out[2] = PROTOCOL_VERSION;
    out[3] = type;
    out[4] = static_cast<uint8_t>(seq & 0xFF);
    out[5] = static_cast<uint8_t>(seq >> 8);
    out[6] = static_cast<uint8_t>(len & 0xFF);
    out[7] = static_cast<uint8_t>(len >> 8);
    for (uint16_t i = 0; i < len; ++i) out[8 + i] = payload[i];
    uint16_t crc = crc16_ccitt_false(out + 2, static_cast<size_t>(6 + len));
    out[8 + len] = static_cast<uint8_t>(crc & 0xFF);
    out[9 + len] = static_cast<uint8_t>(crc >> 8);
    return static_cast<size_t>(10 + len);
}

size_t encode_ack_nack(uint8_t* out, RespType type, uint16_t seq, ErrCode err_if_nack) {
    uint8_t payload[1];
    uint16_t len = 0;
    if (type == RespType::Nack) {
        payload[0] = static_cast<uint8_t>(err_if_nack);
        len = 1;
    }
    return encode_frame(out, static_cast<uint8_t>(type), seq, payload, len);
}

size_t encode_event(uint8_t* out, const uint8_t* payload, uint16_t len) {
    return encode_frame(out, static_cast<uint8_t>(RespType::EventStatus), 0, payload, len);
}

size_t encode_status(uint8_t* out, uint16_t seq, const uint8_t* payload, uint16_t len) {
    return encode_frame(out, static_cast<uint8_t>(RespType::Status), seq, payload, len);
}
