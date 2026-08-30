#include "timed_actions.hpp"

TimedActions g_timed_actions;

namespace {

// Comparação de instantes tolerante ao wrap de 32 bits do relógio de ms
// (board_millis() dá a volta em ~49 dias).
inline bool reached(uint32_t now_ms, uint32_t deadline_ms) {
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

inline bool is_contact(TimedKind k) { return k == TimedKind::ContactPulse; }

} // namespace

bool TimedActions::schedule(TimedKind kind, uint8_t target, uint16_t duration_ms,
                            uint16_t interval_ms, uint32_t now_ms) {
    Entry* slot = nullptr;
    for (auto& e : entries_) {
        if (e.active && is_contact(e.kind) == is_contact(kind) && e.target == target) {
            slot = &e; // substitui o vigente sobre o mesmo alvo
            break;
        }
    }
    if (!slot) {
        for (auto& e : entries_) {
            if (!e.active) { slot = &e; break; }
        }
    }
    if (!slot) return false;

    slot->active = true;
    slot->kind = kind;
    slot->target = target;
    slot->end_ms = now_ms + duration_ms;
    slot->pressed = true; // quem aceitou o comando já afundou/fechou o alvo
    // Meio período: o martelo alterna a cada metade do intervalo, de modo que
    // dois toques ficam espaçados por um intervalo inteiro.
    slot->half_ms = (kind == TimedKind::KeyHammer)
                        ? static_cast<uint16_t>(interval_ms / 2 ? interval_ms / 2 : 1)
                        : 0;
    slot->next_ms = now_ms + slot->half_ms;
    return true;
}

bool TimedActions::schedule_key_hold(uint8_t usage, uint16_t duration_ms, uint32_t now_ms) {
    return schedule(TimedKind::KeyHold, usage, duration_ms, 0, now_ms);
}

bool TimedActions::schedule_key_hammer(uint8_t usage, uint16_t duration_ms, uint16_t interval_ms,
                                       uint32_t now_ms) {
    return schedule(TimedKind::KeyHammer, usage, duration_ms, interval_ms, now_ms);
}

bool TimedActions::schedule_contact_pulse(uint8_t contact, uint16_t duration_ms, uint32_t now_ms) {
    return schedule(TimedKind::ContactPulse, contact, duration_ms, 0, now_ms);
}

size_t TimedActions::cancel_where(bool contacts, bool keys, uint8_t target, bool by_target,
                                  TimedEvent* out, size_t max_out) {
    size_t n = 0;
    for (auto& e : entries_) {
        if (!e.active) continue;
        bool domain = is_contact(e.kind) ? contacts : keys;
        if (!domain) continue;
        if (by_target && e.target != target) continue;
        if (e.pressed && n < max_out) out[n++] = TimedEvent{e.kind, e.target, false};
        e.active = false;
        e.pressed = false;
    }
    return n;
}

size_t TimedActions::cancel_key(uint8_t usage, TimedEvent* out, size_t max_out) {
    return cancel_where(false, true, usage, true, out, max_out);
}

size_t TimedActions::cancel_contact(uint8_t contact, TimedEvent* out, size_t max_out) {
    return cancel_where(true, false, contact, true, out, max_out);
}

size_t TimedActions::cancel_keys(TimedEvent* out, size_t max_out) {
    return cancel_where(false, true, 0, false, out, max_out);
}

size_t TimedActions::cancel_contacts(TimedEvent* out, size_t max_out) {
    return cancel_where(true, false, 0, false, out, max_out);
}

size_t TimedActions::cancel_all(TimedEvent* out, size_t max_out) {
    return cancel_where(true, true, 0, false, out, max_out);
}

size_t TimedActions::tick(uint32_t now_ms, TimedEvent* out, size_t max_out) {
    size_t n = 0;
    for (auto& e : entries_) {
        if (!e.active) continue;
        if (n >= max_out) break; // sem espaço: a entrada fica para a próxima varredura

        if (reached(now_ms, e.end_ms)) {
            // Fim da janela: a janela sempre termina com o alvo solto/aberto.
            if (e.pressed) out[n++] = TimedEvent{e.kind, e.target, false};
            e.active = false;
            e.pressed = false;
            continue;
        }

        if (e.kind == TimedKind::KeyHammer && reached(now_ms, e.next_ms)) {
            e.pressed = !e.pressed;
            out[n++] = TimedEvent{e.kind, e.target, e.pressed};
            e.next_ms += e.half_ms;
            // Se a varredura atrasou mais que um meio período, reancora em vez
            // de disparar uma rajada de recuperação.
            if (reached(now_ms, e.next_ms)) e.next_ms = now_ms + e.half_ms;
        }
    }
    return n;
}

size_t TimedActions::active_count() const {
    size_t n = 0;
    for (const auto& e : entries_) {
        if (e.active) n++;
    }
    return n;
}

bool TimedActions::has_key(uint8_t usage) const {
    for (const auto& e : entries_) {
        if (e.active && !is_contact(e.kind) && e.target == usage) return true;
    }
    return false;
}
