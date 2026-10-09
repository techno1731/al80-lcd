/* Copyright 2026 Ennio
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * OS mode: Mac, or Windows/Linux. The mode picks the base layer, the OS icon on the
 * LCD and the USB identity. macOS only honours the Fn/Globe key from a keyboard that
 * identifies as Apple's, while VIA and the vendor tools look for the factory identity,
 * so each mode enumerates as what its host expects. Changing mode therefore restarts
 * the keyboard.
 */
#include "quantum.h"
#include "usb_descriptor.h"
#include "al80.h"

#define AL80_APPLE_VID 0x05AC
#define AL80_APPLE_PID 0x029C /* Magic Keyboard */

/* Stored in byte 2 of the EECONFIG_USER dword: high nibble is a tag, low nibble the mode.
 * Byte 0 is the layout stamp (al80.c) and byte 1 the wireless boot mode (al80_wireless.c). */
#define AL80_OS_SHIFT 16
#define AL80_OS_MASK 0x00FF0000u
#define AL80_OS_TAG 0xC0

#ifndef AL80_OS_DEFAULT
#    define AL80_OS_DEFAULT AL80_OS_WIN
#endif

static al80_os_t os_mode    = AL80_OS_DEFAULT;
static int8_t    os_pending = -1; /* mode to switch to, applied off the key path */

#ifdef AL80_OS_SWITCH
extern const USB_Descriptor_Device_t DeviceDescriptor;
static USB_Descriptor_Device_t       apple_descriptor;

const USB_Descriptor_Device_t *usb_device_descriptor_kb(void) {
    return os_mode == AL80_OS_MAC ? &apple_descriptor : NULL;
}
#endif

al80_os_t al80_os_mode(void) {
    return os_mode;
}

void al80_os_init(void) {
#ifdef AL80_OS_SWITCH
    const uint8_t b = (uint8_t)((eeconfig_read_user() & AL80_OS_MASK) >> AL80_OS_SHIFT);
    if ((b & 0xF0) == AL80_OS_TAG && (b & 0x0F) <= AL80_OS_MAC) {
        os_mode = (al80_os_t)(b & 0x0F);
    }
    apple_descriptor           = DeviceDescriptor;
    apple_descriptor.VendorID  = AL80_APPLE_VID;
    apple_descriptor.ProductID = AL80_APPLE_PID;
#endif
}

void al80_os_apply(void) {
#ifdef AL80_OS_SWITCH
    default_layer_set(1UL << (os_mode == AL80_OS_MAC ? AL80_LAYER_MAC : AL80_LAYER_WIN));
    if (os_mode == AL80_OS_MAC) {
        keymap_config.nkro = false; /* Fn/Globe rides in the 6-key report */
    }
#endif
}

void al80_os_request(al80_os_t mode) {
    if (mode != os_mode) os_pending = (int8_t)mode;
}

void al80_os_task(void) {
#ifdef AL80_OS_SWITCH
    if (os_pending < 0) return;
    const uint32_t ecu = eeconfig_read_user();
    eeconfig_update_user((ecu & ~AL80_OS_MASK) | ((uint32_t)(AL80_OS_TAG | (uint8_t)os_pending) << AL80_OS_SHIFT));
    clear_keyboard();
    wait_ms(50);
    soft_reset_keyboard();
#endif
}
