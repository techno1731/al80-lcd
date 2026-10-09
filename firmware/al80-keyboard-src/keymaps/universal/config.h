/* Copyright 2026 Ennio
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

/* Same Vial identity as the vial keymap, so one definition serves both. */
#define VIAL_KEYBOARD_UID {0x34, 0x89, 0xDA, 0x56, 0x22, 0xF5, 0xA9, 0x8E}
#define VIAL_UNLOCK_COMBO_ROWS {0, 5}
#define VIAL_UNLOCK_COMBO_COLS {0, 0}

/* Mac and Windows/Linux modes switchable at runtime, each with its own USB identity. */
#define AL80_OS_SWITCH
/* Mode on a fresh board. */
#define AL80_OS_DEFAULT AL80_OS_MAC

/* Development: lets a host tool enter the bootloader over raw HID. Remove for a release. */
#define AL80_DEV_HID_BOOT
