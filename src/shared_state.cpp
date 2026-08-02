#include "shared_state.hpp"
#include "pico/critical_section.h"

namespace {
critical_section_t g_lock;
DeviceSnapshot g_snapshot;
volatile bool g_oled_ok = true;
} // namespace

void shared_state_init() {
    critical_section_init(&g_lock);
}

void shared_state_publish(const DeviceSnapshot& s) {
    critical_section_enter_blocking(&g_lock);
    g_snapshot = s;
    critical_section_exit(&g_lock);
}

DeviceSnapshot shared_state_consume() {
    critical_section_enter_blocking(&g_lock);
    DeviceSnapshot copy = g_snapshot;
    critical_section_exit(&g_lock);
    return copy;
}

void shared_state_set_oled_ok(bool ok) { g_oled_ok = ok; }
bool shared_state_get_oled_ok() { return g_oled_ok; }
