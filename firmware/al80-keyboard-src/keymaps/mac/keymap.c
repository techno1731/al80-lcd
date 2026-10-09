/* Copyright 2026 Ennio
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mac layout: Control, Option, Command on the left, Fn/Globe and Right Command on
 * the right. The function row sends plain F1-F12: over USB macOS applies its own
 * Magic Keyboard mapping, over radio the firmware does (see al80_apple.c).
 */
#include QMK_KEYBOARD_H

#define AP_FN AL80_KC_APPLE_FN
#define BT1 AL80_KC_BT1
#define BT2 AL80_KC_BT2
#define BT3 AL80_KC_BT3
#define RF24 AL80_KC_24G
#define WIRED AL80_KC_USB
#define V_HOME AL80_KC_VIEW_HOME
#define V_PIC AL80_KC_VIEW_PICTURE
#define V_GIF AL80_KC_VIEW_GIF

/* Knob: volume, or backlight brightness with Fn held. */
bool encoder_update_user(uint8_t index, bool clockwise) {
    if (index != 0) {
        return false;
    }
    if (get_highest_layer(layer_state) == AL80_FN_LAYER) {
        al80_apple_fn_used();
        if (clockwise) {
            rgb_matrix_increase_val();
        } else {
            rgb_matrix_decrease_val();
        }
    } else {
        tap_code16(clockwise ? KC_VOLU : KC_VOLD);
    }
    return false;
}

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        KC_ESC,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  KC_DEL,  KC_MUTE,
        KC_GRV,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC, KC_PGUP,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS, KC_PGDN,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
        KC_LSFT, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH,          KC_RSFT, KC_UP,
        KC_LCTL, KC_LALT, KC_LGUI,                   KC_SPC,                   AP_FN,   KC_RGUI, KC_LEFT, KC_DOWN,          KC_RGHT
    ),
    /* Fn: radio on 1-5, LCD views on 8/9/0, lighting on the right-hand cluster. */
    [1] = LAYOUT(
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, RM_TOGG, _______,
        _______, BT1,     BT2,     BT3,     RF24,    WIRED,   _______, _______, V_HOME,  V_PIC,   V_GIF,   _______, _______, _______, RM_VALU,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, RM_SPDD, RM_SPDU, RM_NEXT, RM_VALD,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          RM_HUEU,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______, _______,
        _______, _______, _______,                   _______,                   _______, _______, _______, _______,          _______
    ),
    [2] = LAYOUT(
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______, _______,
        _______, _______, _______,                   _______,                   _______, _______, _______, _______,          _______
    ),
    [3] = LAYOUT(
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______, _______,
        _______, _______, _______,                   _______,                   _______, _______, _______, _______,          _______
    )
};
