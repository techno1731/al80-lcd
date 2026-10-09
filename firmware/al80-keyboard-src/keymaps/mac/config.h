/* Copyright 2026 Ennio
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

/* Same Vial identity as the vial keymap, so one definition serves both. */
#define VIAL_KEYBOARD_UID {0x34, 0x89, 0xDA, 0x56, 0x22, 0xF5, 0xA9, 0x8E}
#define VIAL_UNLOCK_COMBO_ROWS {0, 5}
#define VIAL_UNLOCK_COMBO_COLS {0, 0}

/* Mac function row and Fn navigation, done in firmware wherever the host does not. */
#define AL80_MAC_LAYOUT

/* macOS honours the Fn/Globe key only from a keyboard that identifies as Apple's. */
#undef VENDOR_ID
#undef PRODUCT_ID
#define VENDOR_ID 0x05AC
#define PRODUCT_ID 0x029C
#define AL80_APPLE_USB

/* Mac icon on the LCD homepage. */
#undef AL80_OS_TYPE
#define AL80_OS_TYPE 1
