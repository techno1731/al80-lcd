/* Copyright 2026 Ennio
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Apple key behaviour for the AL80.
 *
 * Over USB the board identifies as an Apple keyboard and reports the real Fn/Globe
 * key (AppleVendor Top Case usage 0x03, carried in the reserved byte of the boot
 * keyboard report). macOS then does what it does for a Magic Keyboard: the function
 * row follows the system setting, Fn+arrows navigate and Globe shortcuts work.
 *
 * Over Bluetooth or 2.4G the radio module owns the HID descriptor, so there is no
 * Fn usage to send. The firmware substitutes the same behaviour itself.
 */
#include "quantum.h"
#include "al80.h"

#ifndef AL80_GLOBE_TAP_MS
#    define AL80_GLOBE_TAP_MS 400 /* a shorter Fn press with no other key is a Globe tap */
#endif

static bool     fn_held = false; /* Fn is physically down */
static bool     fn_sent = false; /* the host has been told */
static bool     fn_used = false; /* something happened while it was down */
static uint32_t fn_down = 0;

#ifdef AL80_MAC_LAYOUT
/* Keys substituted in firmware and still held: bits 0-11 function row, 12-16 navigation. */
static uint32_t substituted = 0;
#endif

typedef struct {
    bool     system; /* generic desktop page, else consumer page */
    uint16_t usage;
} al80_usage_t;

/* What a Magic Keyboard function row does. */
static const al80_usage_t fkey_usage[12] = {
    {false, 0x0070}, /* F1  brightness down */
    {false, 0x006F}, /* F2  brightness up */
    {false, 0x029F}, /* F3  Mission Control */
    {false, 0x0221}, /* F4  Spotlight */
    {false, 0x00CF}, /* F5  Dictation */
    {true, 0x009B},  /* F6  Do Not Disturb */
    {false, 0x00B6}, /* F7  previous track */
    {false, 0x00CD}, /* F8  play/pause */
    {false, 0x00B5}, /* F9  next track */
    {false, 0x00E2}, /* F10 mute */
    {false, 0x00EA}, /* F11 volume down */
    {false, 0x00E9}, /* F12 volume up */
};

#ifdef AL80_MAC_LAYOUT
/* Fn + these keys, as macOS maps them on an Apple keyboard. */
static const uint8_t nav_from[5] = {KC_LEFT, KC_RIGHT, KC_UP, KC_DOWN, KC_BSPC};
static const uint8_t nav_to[5]   = {KC_HOME, KC_END, KC_PGUP, KC_PGDN, KC_DEL};
#endif

static void send_usage(al80_usage_t u, bool pressed) {
    if (u.system) {
        host_system_send(pressed ? u.usage : 0);
    } else {
        host_consumer_send(pressed ? u.usage : 0);
    }
}

/* True when the host sees the Apple USB descriptor and handles Fn itself. */
static bool host_handles_fn(void) {
#ifdef AL80_APPLE_USB
#    ifdef AL80_WIRELESS_ENABLE
    return al80_wireless_mode() == AL80_WL_USB;
#    else
    return true;
#    endif
#else
    return false;
#endif
}

static void fn_report(bool on) {
    keyboard_report->reserved = on ? 1 : 0;
    send_keyboard_report();
}

/* Keys the host can see. Everything else (LCD views, radio slots, lighting) is local. */
static bool host_visible(uint16_t keycode) {
    return keycode != KC_NO && keycode <= QK_MODS_MAX;
}

void al80_apple_fn_used(void) {
    fn_used = true;
}

bool al80_apple_process(uint16_t keycode, keyrecord_t *record) {
    const bool pressed = record->event.pressed;

    switch (keycode) {
        case AL80_KC_APPLE_FN:
            if (pressed) {
                fn_held = true;
                fn_sent = false;
                fn_used = false;
                fn_down = timer_read32();
                layer_on(AL80_FN_LAYER);
            } else {
                layer_off(AL80_FN_LAYER);
                fn_held = false;
                if (fn_sent) {
                    fn_report(false);
                } else if (host_handles_fn() && !fn_used && timer_elapsed32(fn_down) < AL80_GLOBE_TAP_MS) {
                    /* Nothing else was pressed: this was a Globe tap. */
                    fn_report(true);
                    wait_ms(20);
                    fn_report(false);
                }
                fn_sent = false;
            }
            return false;
        case AL80_KC_DICTATION:
            send_usage(fkey_usage[4], pressed);
            return false;
        case AL80_KC_DND:
            send_usage(fkey_usage[5], pressed);
            return false;
        case AL80_KC_SPOTLIGHT:
            send_usage(fkey_usage[3], pressed);
            return false;
        default:
            break;
    }

#ifdef AL80_MAC_LAYOUT
    if (!host_handles_fn()) {
        /* Function row: media first, Fn gives the F-key. */
        if (keycode >= KC_F1 && keycode <= KC_F12) {
            const uint8_t  i   = (uint8_t)(keycode - KC_F1);
            const uint32_t bit = 1ul << i;
            if (pressed && !fn_held) {
                substituted |= bit;
                send_usage(fkey_usage[i], true);
                return false;
            }
            if (!pressed && (substituted & bit)) {
                substituted &= ~bit;
                send_usage(fkey_usage[i], false);
                return false;
            }
        }
        /* Fn + arrows and Fn + Backspace. */
        for (uint8_t i = 0; i < 5; i++) {
            const uint32_t bit = 1ul << (12 + i);
            if (keycode != nav_from[i]) continue;
            if (pressed && fn_held) {
                substituted |= bit;
                register_code(nav_to[i]);
                return false;
            }
            if (!pressed && (substituted & bit)) {
                substituted &= ~bit;
                unregister_code(nav_to[i]);
                return false;
            }
        }
    }
#endif

    if (pressed && fn_held) {
        /* The host only hears about Fn when a key it can see is pressed with it. Local
         * keys stay local, so Fn + an LCD or radio key never reads as a Globe tap. */
        if (host_visible(keycode) && host_handles_fn() && !fn_sent) {
            fn_sent = true;
            fn_report(true);
        }
        fn_used = true;
    }
    return true;
}
