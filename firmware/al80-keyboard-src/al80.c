/* Copyright 2026 snackdriven
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * YUNZII AL80 board support:
 *   - SWJ disable (matrix uses PA13/14/15, PB3/4)
 *   - aw20216s LED position map (recovered from RIPPLE.bin @ flash 0x080116A0)
 *   - LCD pass-through: forward raw-HID 0x40/0x41/0x42 payloads over USART3
 */
#include "quantum.h"
#include "raw_hid.h"
#include "hal.h"
#include "eeconfig.h"
#include "dynamic_keymap.h"
#include "al80.h"
#include "al80_logic.h"
#include <string.h>
#include "usb_device_state.h"

/* Set while an LCD transfer (0x40..0x42) is in flight. aw20216s_flush() checks this and
 * skips its SPI writes so they can't preempt the interrupt-driven USART3 TX and put gaps in
 * the byte stream (which shears the image). Watchdog clears it if a transfer stalls. */
volatile bool g_screen_busy = false;
static uint16_t screen_busy_wd = 0;

/* ---- battery telemetry (ported from b75Pro smart_kb16: battery.c / adc.c / keyboard_screen.c) ----
 * The homepage battery gauge is drawn by the display module but FED by the keyboard: stock sends
 * PK_BATT_QUANTITY (announce type 0x06, one % byte) + PK_BATT_STATUS (0x07, charge state) over
 * USART3. A pure passthrough never sends these, so the gauge reads empty. We read ADC1 ch9 (B1),
 * convert (mv = adc*1764/vref), map to % with b75Pro's piecewise thresholds (al80_logic.h), and emit
 * the same A5 5A packets the module expects (CRC16-MODBUS over [type,flag,len], as al80-studio). */
/* USB plug detect (high = cable in). Same pin as the vendor firmware. */
#define AL80_PLUG_PIN B9

/* Last battery sample. raw/vref are kept for the 0x4C diagnostic. */
static al80_batt_t al80_batt = {0, 0, 0, 100, AL80_BATT_CHARGING, false};

/* ADC1 is driven directly: the cell (ch9 = B1) is measured against the internal
 * reference (ch17), which needs TSVREFE, and QMK's analog driver clears that bit
 * on every conversion. */
static void al80_adc_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_ADCPRE) | RCC_CFGR_ADCPRE_DIV8; /* 9 MHz, under the 14 MHz limit */
    palSetLineMode(B1, PAL_MODE_INPUT_ANALOG);
    setPinInput(AL80_PLUG_PIN);

    ADC1->CR1   = 0;
    ADC1->CR2   = ADC_CR2_ADON | ADC_CR2_TSVREFE | ADC_CR2_EXTTRIG | ADC_CR2_EXTSEL; /* software start */
    ADC1->SMPR1 = 7u << 21; /* ch17: longest sample time */
    ADC1->SMPR2 = 7u << 27; /* ch9 */
    ADC1->SQR1  = 0;        /* one conversion */
    wait_ms(1);
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    for (uint16_t t = 10000; (ADC1->CR2 & ADC_CR2_RSTCAL) && t; t--) {}
    ADC1->CR2 |= ADC_CR2_CAL;
    for (uint16_t t = 10000; (ADC1->CR2 & ADC_CR2_CAL) && t; t--) {}
}

/* Mean of 8 conversions, 0 if the converter never finishes. */
static uint16_t al80_adc_read(uint8_t channel) {
    uint32_t sum = 0;
    ADC1->SQR3 = channel;
    for (uint8_t i = 0; i < 8; i++) {
        uint16_t t = 20000;
        ADC1->CR2 |= ADC_CR2_SWSTART;
        while (!(ADC1->SR & ADC_SR_EOC) && --t) {}
        if (!t) return 0;
        sum += ADC1->DR & 0x0FFF;
    }
    return (uint16_t)(sum / 8);
}

static void al80_batt_sample(void) {
    const uint16_t raw = al80_adc_read(9);
    al80_batt          = al80_batt_eval(raw, al80_adc_read(17), readPin(AL80_PLUG_PIN));
}

/* CRC16-MODBUS (init 0xFFFF, poly 0xA001) — al80-studio's announce checksum "ga". */
static uint16_t al80_crc16(const uint8_t *d, uint8_t n) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

/* One PK announce + 1 data byte to the module: A5 5A <type> 00 01 <crcHi> <crcLo> <val>. */
static void al80_screen_send_u8(uint8_t type, uint8_t val) {
    uint8_t  hdr[3] = { type, 0x00, 0x01 };
    uint16_t crc    = al80_crc16(hdr, 3);
    uint8_t  pkt[8] = { 0xA5, 0x5A, type, 0x00, 0x01, (uint8_t)(crc >> 8), (uint8_t)crc, val };
    sdWrite(&SD3, pkt, sizeof(pkt));
}

/* One PK_GO view announce (home 0x0B / picture 0x0D / gif 0x0F) to the module. Unlike a status
 * packet, a view announce carries ZERO data: len byte 0x00, so the packet is 7 bytes (no trailing
 * value), not 8. These bytes are identical to protocol.js buildView(type) -- the same wire the host
 * relays via raw_hid_receive_kb (which sdWrites only data[7..]). CRC16-MODBUS over [type,0,0]. */
static void al80_screen_send_view(uint8_t type) {
    uint8_t  hdr[3] = { type, 0x00, 0x00 };
    uint16_t crc    = al80_crc16(hdr, 3);
    uint8_t  pkt[7] = { 0xA5, 0x5A, type, 0x00, 0x00, (uint8_t)(crc >> 8), (uint8_t)crc };
    sdWrite(&SD3, pkt, sizeof(pkt));
}

/* Switch the on-device LCD view over USART3, host-free. Wrapped in the same g_screen_busy
 * discipline as al80_battery_push so the announce can't interleave with the RGB SPI stream.
 * Called only from housekeeping_task_kb (deferred via view_request) when !g_screen_busy. */
static void al80_screen_view(uint8_t type) {
    g_screen_busy = true;
    al80_screen_send_view(type);
    wait_us(500);
    g_screen_busy = false;
    screen_busy_wd = 0;
}

/* Keyboard -> host panel-switch signal: an UNSOLICITED raw-HID report [0x4B, panelId, 0...] on the
 * interface the host already reads (device.js _onData). A 0x4B first byte routes to cycler.jumpTo.
 * Mirrors al80_screen_send_u8's shape but on the USB raw-HID path, not USART3. Fired directly from
 * the key handler (raw_hid_send is not the USART3 subsystem, so it has no byte-shear concern; if it
 * ever collides with an in-flight image ACK stream on-device, gate it on !g_screen_busy). */
static void al80_panel_req(uint8_t id) {
    uint8_t buf[RAW_EPSIZE] = {0};
    buf[0] = AP_PANEL_REQ;
    buf[1] = id;
    raw_hid_send(buf, RAW_EPSIZE);
}

/* Read the battery and push PK_BATT_QUANTITY (%) + PK_BATT_STATUS to the module. Caller must
 * ensure no image transfer is in flight (checks g_screen_busy) so the bytes don't interleave. */
static void al80_battery_push(void) {
    g_screen_busy = true;                             /* pause RGB SPI so the tiny packets don't jitter */
    al80_screen_send_u8(0x07, al80_batt.status);      /* PK_BATT_STATUS */
    wait_us(500);                                     /* let the module commit before the next packet */
    al80_screen_send_u8(0x06, al80_batt.pct);         /* PK_BATT_QUANTITY */
    wait_us(500);
    g_screen_busy = false;
    screen_busy_wd = 0;
}

/* Push the full homepage widget set the display module expects at boot (ported from b75Pro
 * keyboard_screen.c screen_boot_step: conn type, OS type, lock states, battery). The module
 * initializes its homepage gauges from this batch; a lone battery packet may have no widget to
 * fill. Re-sent periodically so it self-heals after a main-page image push clears the homepage. */
static void al80_homepage_init(void) {
    uint8_t  leds = host_keyboard_leds();
#if defined(AL80_WIRELESS_ENABLE)
    uint8_t  conn = (uint8_t)al80_wireless_mode();   /* 0 USB, 1-3 BT slot, 4 2.4G */
#else
    uint8_t  conn = 0;
#endif
    g_screen_busy = true;
    al80_screen_send_u8(0x01, conn);            wait_us(500); /* PK_CONN_TYPE                   */
    al80_screen_send_u8(0x02, al80_os_mode());  wait_us(500); /* PK_OS_TYPE: 0 Windows, 1 Mac   */
    al80_screen_send_u8(0x03, (leds >> 1) & 1); wait_us(500); /* PK_CAPS_STATUS                 */
    al80_screen_send_u8(0x04, leds & 1);        wait_us(500); /* PK_NUMLOCK_STATUS              */
    al80_screen_send_u8(0x05, keymap_config.no_gui ? 1 : 0); wait_us(500); /* PK_WINLOCK_STATUS    */
    al80_screen_send_u8(0x07, al80_batt.status);wait_us(500); /* PK_BATT_STATUS                 */
    al80_screen_send_u8(0x06, al80_batt.pct);   wait_us(500); /* PK_BATT_QUANTITY               */
    g_screen_busy = false;
    screen_busy_wd = 0;
}

/* ---- homepage caps/num-lock icons ----
 * The display module draws the caps/num lock icons from PK_CAPS_STATUS (0x03) / PK_NUMLOCK_STATUS
 * (0x04) status packets. Stock and the b75Pro sibling push these on every real LED change
 * (b75Pro keyboard_screen.c reads host_keyboard_leds() and sets PK_CAPS/PK_NUMLOCK flags). The
 * passthrough here only sent them in the boot batch + the 30s self-heal, so a Caps/Num toggle
 * lagged up to 30s and looked dead.
 *
 * The blocking USART3 send below runs in the single-threaded main loop (~1ms: 2 packets + two
 * 500us settles), so it MUST only run on an actual caps/num transition. QMK's led_task() is
 * change-gated, but led_set() (-> led_update_kb) is ALSO called unconditionally on every layer
 * action (quantum/action.c) and in command mode, passing an UNCHANGED host_keyboard_leds(). v24
 * sent on every such call, so every layer key / layer-tap did a blocking send and stalled typing +
 * the encoder. We track last_locks and early-return when caps/num are unchanged, so the wire only
 * moves on a genuine toggle (a handful of bytes, once). If a transfer is mid-flight we defer via
 * locks_dirty. */
static volatile bool locks_dirty = false;
static uint8_t       last_locks  = 0xFF;   /* caps<<1|num; 0xFF = unknown -> first report syncs */

/* Deferred host-free view-switch request. process_record_kb sets this to a PK_GO view type
 * (0x0B home / 0x0D picture / 0x0F gif) on a view-key press; housekeeping_task_kb flushes it via
 * al80_screen_view when !g_screen_busy, so the 7-byte announce never interleaves with an in-flight
 * image passthrough or the RGB SPI stream (same deferral discipline as locks_dirty). Multiple
 * presses coalesce to the last. 0 = nothing pending. */
static volatile uint8_t view_request = 0;

/* LCD rotation: the keyboard alternates the home page and the GIF page by itself, no host
 * needed. Stored in byte 3 of the EECONFIG_USER dword (tag in the high nibble). Choosing a
 * view by hand stops it. */
#define AL80_ROTATE_SHIFT 24
#define AL80_ROTATE_MASK 0xFF000000u
#define AL80_ROTATE_TAG 0xD0
static bool     rotate_on   = false;
static bool     rotate_gif  = false; /* the GIF page is the one showing */
static uint32_t rotate_time = 0;

static void al80_rotate_set(bool on) {
    if (on == rotate_on) return;
    rotate_on   = on;
    rotate_gif  = false;
    rotate_time = timer_read32();
    eeconfig_update_user((eeconfig_read_user() & ~AL80_ROTATE_MASK) | ((uint32_t)(AL80_ROTATE_TAG | (on ? 1 : 0)) << AL80_ROTATE_SHIFT));
}

static void al80_rotate_load(void) {
    const uint8_t b = (uint8_t)((eeconfig_read_user() & AL80_ROTATE_MASK) >> AL80_ROTATE_SHIFT);
    rotate_on       = (b & 0xF0) == AL80_ROTATE_TAG && (b & 0x01);
    rotate_time     = timer_read32();
}

/* ---- custom keycode handler (NEW: this build shipped no process_record of any kind) ----
 * View keys switch the LCD on-device via the deferred view_request; PANEL_* keys additionally
 * signal the host cycler over raw-HID 0x4B. Press-edge only -- QMK does not re-invoke held custom
 * keycodes, so a held key fires exactly once (no repeat storm). Every case returns false to consume
 * the keycode: no HID keystroke reaches the OS/focused app. The handler sets one byte (view_request)
 * and/or fires raw_hid_send; it never does a blocking USART3 sdWrite/wait_us in the key path (that
 * was the v24 typing-stall regression -- USART3 work is deferred to housekeeping_task_kb). */
static void al80_bar_step(bool up);

/* Destructive keys act only after being held this long. */
#define AL80_HOLD_MS 3000
static uint16_t          hold_key   = KC_NO; /* held destructive key, KC_NO when none */
static uint32_t          hold_since = 0;
static volatile bool     homepage_dirty = false; /* a status value changed: push the homepage again */

/* The stm32duino bootloader stays in DFU when it finds this flag in backup register 10. */
void bootloader_jump(void) {
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    PWR->CR |= PWR_CR_DBP;
    BKP->DR10 = 0x424C;
    PWR->CR &= ~PWR_CR_DBP;
    NVIC_SystemReset();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!al80_apple_process(keycode, record)) return false;
    switch (keycode) {
        case AL80_KC_RESET:
        case AL80_KC_BOOT:
            hold_key   = record->event.pressed ? keycode : KC_NO;
            hold_since = timer_read32();
            return false;
        case AL80_KC_OS_WIN:
            if (record->event.pressed) al80_os_request(AL80_OS_WIN);
            return false;
        case AL80_KC_OS_MAC:
            if (record->event.pressed) al80_os_request(AL80_OS_MAC);
            return false;
        case AL80_KC_WINLOCK:
            if (record->event.pressed) {
                keymap_config.no_gui = !keymap_config.no_gui;
                eeconfig_update_keymap(&keymap_config);
                homepage_dirty = true;
            }
            return false;
        case AL80_KC_BAR_UP:
        case AL80_KC_BAR_DOWN:
            if (record->event.pressed) al80_bar_step(keycode == AL80_KC_BAR_UP);
            return false;
        case AL80_KC_VIEW_HOME:
            if (record->event.pressed) { view_request = 0x0B; al80_rotate_set(false); }
            return false;
        case AL80_KC_VIEW_PICTURE:
            if (record->event.pressed) { view_request = 0x0D; al80_rotate_set(false); }  /* PK_TOGGLE_PIC advances the ring */
            return false;
        case AL80_KC_VIEW_GIF:
            if (record->event.pressed) { view_request = 0x0F; al80_rotate_set(false); }
            return false;
        case AL80_KC_VIEW_ROTATE:
            if (record->event.pressed) {
                al80_rotate_set(!rotate_on);
                view_request = 0x0B; /* either way, start from the home page */
            }
            return false;
        /* Host-only, NO local view (was view_request=0x0D). 0x0D is PK_TOGGLE_PIC — it ADVANCES the
         * picture ring (KB R3), not "show picture page". The host's PK_ADD_PIC already commits AND
         * displays the panel card and brings the LCD to the picture view on its own, so a local
         * advance only (a) desyncs the host's delete-before-add ring hygiene and (b) flashes an old
         * slot before the card lands. Trade-off (per hotkey-SPARC FR2/§77): we lose the instant-view
         * gap-filler + no-host fallback; the card now appears when the host repaints (~1-1.5s). */
        case AL80_KC_PANEL_NOWPLAYING:
            if (record->event.pressed) al80_panel_req(0x00);
            return false;
        case AL80_KC_PANEL_WEATHER:
            if (record->event.pressed) al80_panel_req(0x01);
            return false;
        case AL80_KC_PANEL_CLOCK:
            if (record->event.pressed) { view_request = 0x0B; al80_panel_req(0x02); }
            return false;
        case AL80_KC_CYCLE_TOGGLE:
            if (record->event.pressed) al80_panel_req(0xF0);  /* pause/resume rotation, no local view */
            return false;
        case AL80_KC_PANEL_NEXT:
            if (record->event.pressed) al80_panel_req(0xF1);  /* advance one panel, no local view */
            return false;

#if defined(AL80_WIRELESS_ENABLE)
        /* Tap = connect to that slot, hold = re-pair it. Matching stock's feel:
         * you only want pairing mode deliberately, not every time you switch back
         * to a device you already own.
         *
         * These only set a flag. The switch itself blocks ~400ms (wake bytes plus
         * the module's settle) and runs from housekeeping -- doing it here would
         * stall typing exactly the way v24 did. */
        case AL80_KC_BT1:
        case AL80_KC_BT2:
        case AL80_KC_BT3:
        case AL80_KC_24G: {
            static uint32_t wl_down = 0;
            const al80_wl_mode_t mode = (al80_wl_mode_t)(keycode - QK_KB_0);
            if (record->event.pressed) {
                wl_down = timer_read32();
            } else {
                al80_wireless_request(mode, timer_elapsed32(wl_down) >= AL80_WL_PAIR_HOLD_MS);
            }
            return false;
        }
        case AL80_KC_USB:
            if (record->event.pressed) al80_wireless_request(AL80_WL_USB, false);
            return false;
#endif

        default:
            return process_record_user(keycode, record);
    }
}

/* Push caps + num lock to the module. Main-loop context (like al80_battery_push), so the two 500us
 * settles are fine. Bit map matches al80_homepage_init: caps = leds bit1, num = leds bit0. */
static void al80_locks_push(void) {
    uint8_t leds = host_keyboard_leds();
    g_screen_busy = true;                          /* pause RGB SPI so the tiny packets don't jitter */
    al80_screen_send_u8(0x03, (leds >> 1) & 1);    /* PK_CAPS_STATUS    */
    wait_us(500);
    al80_screen_send_u8(0x04, leds & 1);           /* PK_NUMLOCK_STATUS */
    wait_us(500);
    g_screen_busy = false;
    screen_busy_wd = 0;
}

/* Called by QMK whenever it (re)binds host LED state - on a real change AND on every layer action.
 * Only touch the wire when caps/num actually transitioned; otherwise return immediately so plain
 * typing / layer keys / the encoder never eat a blocking USART3 send. */
bool led_update_kb(led_t led_state) {
    bool res = led_update_user(led_state);
    if (res) {
        uint8_t cur = (led_state.caps_lock ? 2 : 0) | (led_state.num_lock ? 1 : 0);
        if (cur != last_locks) {          /* real caps/num edge - everything else early-returns */
            last_locks = cur;
            if (g_screen_busy) {
                locks_dirty = true;       /* wire busy: flushed by housekeeping_task_kb when free */
            } else {
                al80_locks_push();
            }
        }
    }
    return res;
}

/* ---- user-editable RGB palette store ----
 * al80_palette is the live RAM mirror the PALETTE_CYCLE effect reads. It is
 * seeded at keyboard_post_init_kb: from EEPROM if a valid magic byte is
 * present, otherwise from the compiled default (and NOT written back, so a
 * fresh board leaves flash untouched). raw-HID 0x44 edits it live; 0x45
 * commits it to a dedicated wear-leveling KB datablock in one flash write. */
al80_palette_color_t al80_palette[AL80_PALETTE_LEN];

/* Compiled default palette: teal -> magenta -> amber. Keep AL80_PALETTE_LEN
 * (config.h) and this initializer in sync when growing the palette. */
static const al80_palette_color_t AL80_PALETTE_DEFAULT[AL80_PALETTE_LEN] = {
    {128, 255},  // teal
    {213, 255},  // magenta
    { 28, 255},  // amber
};

/* EEPROM layout of the KB datablock: a magic byte (VIA-style validity marker)
 * followed by the palette. If magic != AL80_PALETTE_MAGIC we treat the block
 * as unset and fall back to the compiled default. */
#define AL80_PALETTE_MAGIC 0x5A
typedef struct {
    uint8_t              magic;
    al80_palette_color_t colors[AL80_PALETTE_LEN];
} al80_palette_store_t;

static void al80_palette_load(void) {
#if (EECONFIG_KB_DATA_SIZE) > 0
    al80_palette_store_t store;
    /* read_kb_datablock zero-fills when the block has never been written, so a
       fresh board reads magic == 0 and falls through to the default below. */
    eeconfig_read_kb_datablock(&store, 0, sizeof(store));
    if (store.magic == AL80_PALETTE_MAGIC) {
        memcpy(al80_palette, store.colors, sizeof(al80_palette));
        return;
    }
#endif
    memcpy(al80_palette, AL80_PALETTE_DEFAULT, sizeof(al80_palette));
}

static void al80_palette_save(void) {
#if (EECONFIG_KB_DATA_SIZE) > 0
    al80_palette_store_t store;
    store.magic = AL80_PALETTE_MAGIC;
    memcpy(store.colors, al80_palette, sizeof(al80_palette));
    /* single flash write: stamps the datablock version + magic + palette. */
    eeconfig_update_kb_datablock(&store, 0, sizeof(store));
#endif
}

/* ---- independent side-LED-bar color ----
 * The side bar is RGB-matrix indices 76..78 (aw20216s driver 1). By default it
 * follows the keys (keylight flag). When bar_independent is true, the RGB-matrix
 * indicators hook overrides those three LEDs with bar_hsv every frame. The state
 * is statically initialized (valid before the first render — no boot garbage),
 * seeded from a SEPARATE EEPROM sub-block at post_init, edited live over raw-HID
 * (0x47) and persisted (0x48). It lives at its own offset so it never disturbs
 * the palette block (which keeps its own magic and layout). */
static uint8_t bar_h           = 128;   /* cyan/teal on the 0..255 wheel */
static uint8_t bar_s           = 255;
static uint8_t bar_v           = 255;
static bool    bar_independent = true;   /* default: bar shows its own color */

#define AL80_BAR_MAGIC 0x5B              /* distinct from AL80_PALETTE_MAGIC 0x5A */
/* Offset of the bar sub-block: right after the palette store (config.h reserves
 * AL80_PALETTE_STORE_SIZE + AL80_BAR_STORE_SIZE bytes total). */
#define AL80_BAR_STORE_OFFSET (sizeof(al80_palette_store_t))

typedef struct {
    uint8_t magic;
    uint8_t h;
    uint8_t s;
    uint8_t v;
    uint8_t independent;
} al80_bar_store_t;

static void al80_bar_load(void) {
#if (EECONFIG_KB_DATA_SIZE) > 0
    al80_bar_store_t store;
    eeconfig_read_kb_datablock(&store, AL80_BAR_STORE_OFFSET, sizeof(store));
    if (store.magic == AL80_BAR_MAGIC) {
        bar_h           = store.h;
        bar_s           = store.s;
        bar_v           = store.v;
        bar_independent = store.independent ? true : false;
    }
    /* else: keep the compiled defaults; leave flash untouched on a fresh board. */
#endif
}

static void al80_bar_save(void) {
#if (EECONFIG_KB_DATA_SIZE) > 0
    al80_bar_store_t store = {AL80_BAR_MAGIC, bar_h, bar_s, bar_v, bar_independent ? 1 : 0};
    eeconfig_update_kb_datablock(&store, AL80_BAR_STORE_OFFSET, sizeof(store));
#endif
}

/* Side bar brightness in eight steps, stored like any other bar change. */
static void al80_bar_step(bool up) {
    const int16_t v = (int16_t)bar_v + (up ? 32 : -32);
    bar_v           = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    al80_bar_save();
}

/* ---- per-key live LED stream (host audio-reactive) ----
 * The host streams the whole 82-LED field save-less over raw-HID 0x49 in <=20-LED chunks. Each
 * chunk memcpys into g_live_rgb; the indicators hook below repaints the buffer every render. No
 * double-buffer -- a torn frame (chunks from frame N and N-1) lasts <=1 render (~16 ms), invisible
 * for VU motion. Zero EEPROM writes. When frames stop, matrix_scan_kb clears g_live_active after
 * AL80_LIVE_IDLE_MS and the user's prior effect resumes untouched (we only override in the hook;
 * rgb_matrix_config is never modified). Buffer is .bss (zero-init), ~246 B of the 20 KB RAM. */
static uint8_t           g_live_rgb[RGB_MATRIX_LED_COUNT * 3];
static volatile bool     g_live_active = false;
static volatile uint32_t g_live_last   = 0;

/* Per-render override. When a live audio field is streaming, paint g_live_rgb across the whole
 * slice (live owns the board, side bar included). Otherwise fall back to the independent side-bar
 * override (76..78). The RGB-matrix core calls this once per render with [led_min, led_max), so
 * bounds-check against the slice AND RGB_MATRIX_LED_COUNT. */
bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    if (g_live_active) {
        for (uint8_t i = led_min; i < led_max && i < RGB_MATRIX_LED_COUNT; i++) {
            uint8_t r = g_live_rgb[i * 3], g = g_live_rgb[i * 3 + 1], b = g_live_rgb[i * 3 + 2];
#if AL80_LIVE_MAX_VAL < 255
            r = (uint8_t)((uint16_t)r * AL80_LIVE_MAX_VAL >> 8);
            g = (uint8_t)((uint16_t)g * AL80_LIVE_MAX_VAL >> 8);
            b = (uint8_t)((uint16_t)b * AL80_LIVE_MAX_VAL >> 8);
#endif
            rgb_matrix_set_color(i, r, g, b);
        }
    } else if (bar_independent) {
        HSV hsv = {bar_h, bar_s, bar_v};
        RGB rgb = hsv_to_rgb(hsv);
        for (uint8_t i = 76; i <= 78; i++) {
            if (i >= led_min && i < led_max && i < RGB_MATRIX_LED_COUNT) {
                rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
            }
        }
    }
    return rgb_matrix_indicators_advanced_user(led_min, led_max);
}

/* ---- aw20216s LED map ----
 * {driver, R, G, B}. Macro naming reconciled with drivers/led/aw20216s.h,
 * which spells positions SW<row>_CS<col> (the params file used CS<col>_SW<row>;
 * converted 1:1 by swapping the pair). idx 82/83 = {0,0,0,0} unused.
 */
const aw20216s_led_t PROGMEM g_aw20216s_leds[AW20216S_LED_COUNT] = {
    {0, SW1_CS1, SW1_CS2, SW1_CS3}, {0, SW2_CS1, SW2_CS2, SW2_CS3}, {0, SW3_CS1, SW3_CS2, SW3_CS3}, {0, SW4_CS1, SW4_CS2, SW4_CS3},
    {0, SW5_CS1, SW5_CS2, SW5_CS3}, {0, SW6_CS1, SW6_CS2, SW6_CS3}, {0, SW7_CS1, SW7_CS2, SW7_CS3}, {0, SW8_CS1, SW8_CS2, SW8_CS3},
    {1, SW1_CS1, SW1_CS2, SW1_CS3}, {1, SW2_CS1, SW2_CS2, SW2_CS3}, {1, SW3_CS1, SW3_CS2, SW3_CS3}, {1, SW4_CS1, SW4_CS2, SW4_CS3},
    {1, SW5_CS1, SW5_CS2, SW5_CS3}, {1, SW6_CS1, SW6_CS2, SW6_CS3}, {0, SW1_CS4, SW1_CS5, SW1_CS6}, {0, SW2_CS4, SW2_CS5, SW2_CS6},
    {0, SW3_CS4, SW3_CS5, SW3_CS6}, {0, SW4_CS4, SW4_CS5, SW4_CS6}, {0, SW5_CS4, SW5_CS5, SW5_CS6}, {0, SW6_CS4, SW6_CS5, SW6_CS6},
    {0, SW7_CS4, SW7_CS5, SW7_CS6}, {0, SW8_CS4, SW8_CS5, SW8_CS6}, {1, SW1_CS4, SW1_CS5, SW1_CS6}, {1, SW2_CS4, SW2_CS5, SW2_CS6},
    {1, SW3_CS4, SW3_CS5, SW3_CS6}, {1, SW4_CS4, SW4_CS5, SW4_CS6}, {1, SW5_CS4, SW5_CS5, SW5_CS6}, {1, SW6_CS4, SW6_CS5, SW6_CS6},
    {1, SW7_CS4, SW7_CS5, SW7_CS6}, {0, SW1_CS7, SW1_CS8, SW1_CS9}, {0, SW2_CS7, SW2_CS8, SW2_CS9}, {0, SW3_CS7, SW3_CS8, SW3_CS9},
    {0, SW4_CS7, SW4_CS8, SW4_CS9}, {0, SW5_CS7, SW5_CS8, SW5_CS9}, {0, SW6_CS7, SW6_CS8, SW6_CS9}, {0, SW7_CS7, SW7_CS8, SW7_CS9},
    {0, SW8_CS7, SW8_CS8, SW8_CS9}, {1, SW1_CS7, SW1_CS8, SW1_CS9}, {1, SW2_CS7, SW2_CS8, SW2_CS9}, {1, SW3_CS7, SW3_CS8, SW3_CS9},
    {1, SW4_CS7, SW4_CS8, SW4_CS9}, {1, SW5_CS7, SW5_CS8, SW5_CS9}, {1, SW6_CS7, SW6_CS8, SW6_CS9}, {1, SW7_CS7, SW7_CS8, SW7_CS9},
    {0, SW1_CS10, SW1_CS11, SW1_CS12}, {0, SW2_CS10, SW2_CS11, SW2_CS12}, {0, SW3_CS10, SW3_CS11, SW3_CS12}, {0, SW4_CS10, SW4_CS11, SW4_CS12},
    {0, SW5_CS10, SW5_CS11, SW5_CS12}, {0, SW6_CS10, SW6_CS11, SW6_CS12}, {0, SW7_CS10, SW7_CS11, SW7_CS12}, {0, SW8_CS10, SW8_CS11, SW8_CS12},
    {1, SW1_CS10, SW1_CS11, SW1_CS12}, {1, SW2_CS10, SW2_CS11, SW2_CS12}, {1, SW3_CS10, SW3_CS11, SW3_CS12}, {1, SW4_CS10, SW4_CS11, SW4_CS12},
    {1, SW6_CS10, SW6_CS11, SW6_CS12}, {0, SW1_CS13, SW1_CS14, SW1_CS15}, {0, SW3_CS13, SW3_CS14, SW3_CS15}, {0, SW4_CS13, SW4_CS14, SW4_CS15},
    {0, SW5_CS13, SW5_CS14, SW5_CS15}, {0, SW6_CS13, SW6_CS14, SW6_CS15}, {0, SW7_CS13, SW7_CS14, SW7_CS15}, {0, SW8_CS13, SW8_CS14, SW8_CS15},
    {1, SW1_CS13, SW1_CS14, SW1_CS15}, {1, SW2_CS13, SW2_CS14, SW2_CS15}, {1, SW3_CS13, SW3_CS14, SW3_CS15}, {1, SW4_CS13, SW4_CS14, SW4_CS15},
    {1, SW5_CS13, SW5_CS14, SW5_CS15}, {1, SW6_CS13, SW6_CS14, SW6_CS15}, {0, SW1_CS16, SW1_CS17, SW1_CS18}, {0, SW2_CS16, SW2_CS17, SW2_CS18},
    {0, SW3_CS16, SW3_CS17, SW3_CS18}, {0, SW7_CS16, SW7_CS17, SW7_CS18}, {0, SW8_CS16, SW8_CS17, SW8_CS18}, {1, SW1_CS16, SW1_CS17, SW1_CS18},
    {1, SW2_CS16, SW2_CS17, SW2_CS18}, {1, SW3_CS16, SW3_CS17, SW3_CS18}, {1, SW4_CS16, SW4_CS17, SW4_CS18}, {1, SW5_CS16, SW5_CS17, SW5_CS18},
    {1, SW6_CS16, SW6_CS17, SW6_CS18}, {1, SW7_CS16, SW7_CS17, SW7_CS18},
    /* slots 82/83 exist in silicon but are unwired; RGB_MATRIX_LED_COUNT is 82
       so they are intentionally omitted here. */
};

/* ---- LCD pass-through over USART3 (SD3) ---- */
#if defined(AL80_LCD_ENABLE)
/* 460800 8N1, cr1/cr2/cr3 = 0 (STM32 SerialConfig) */
static const SerialConfig lcd_serial_config = {AL80_LCD_BAUD, 0, 0, 0};

static void al80_lcd_init(void) {
    /* AFIO clock: ChibiOS _pal_lld_init already enables AFIOEN, but assert it
       explicitly so the MAPR writes below can never be silently dropped. */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
    (void)RCC->APB2ENR;

    /* USART3 partial remap: TX=PC10, RX=PC11.
       Single write that sets the remap AND (re)asserts the SWJ-disable in one
       shot. A plain `MAPR |= REMAP` read-modify-write reads the write-only
       SWJ_CFG field back as 0b000 and would re-enable full JTAG/SWD, reclaiming
       the matrix pins PA14/PA15/PB3/PB4. Writing the whole field keeps SWJ off
       while the remap bit sticks. This matches the stock ripple init order
       (remap set with SWJ disabled). */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG_Msk)
               | AFIO_MAPR_SWJ_CFG_DISABLE
               | AFIO_MAPR_USART3_REMAP_PARTIALREMAP;

    palSetLineMode(C10, PAL_MODE_STM32_ALTERNATE_PUSHPULL);
    palSetLineMode(C11, PAL_MODE_INPUT);
    sdStart(&SD3, &lcd_serial_config);
}
#endif

/* LCD module power (high = on). */
#define AL80_LCD_POWER_PIN C9

/* Lights and LCD are off: the host sleeps, or the board sits unused on battery. */
static bool al80_idle = false;

static bool al80_should_idle(void) {
#if defined(AL80_WIRELESS_ENABLE)
    if (al80_wireless_mode() != AL80_WL_USB) {
        if (al80_wireless_host_suspended()) return last_input_activity_elapsed() > 2000;
        return !readPin(AL80_PLUG_PIN) && last_input_activity_elapsed() > AL80_IDLE_MS;
    }
#endif
    return usb_device_state_get_configure_state() == USB_DEVICE_STATE_SUSPEND;
}

/* Returns true while idle, so the caller skips LCD traffic. */
static bool al80_idle_task(uint8_t *boot_inits) {
    static uint32_t awake_seen = 0;
    bool            want       = al80_should_idle();
    if (!want) {
        awake_seen = timer_read32();
    } else if (!al80_idle && timer_elapsed32(awake_seen) < 1000) {
        want = false; /* a suspend blip during enumeration is not sleep */
    }
    if (want != al80_idle) {
        al80_idle = want;
        rgb_matrix_set_suspend_state(want);
        writePin(AL80_LCD_POWER_PIN, !want);
        if (!want) *boot_inits = 0; /* the module lost its homepage: push it again */
    }
    return al80_idle;
}

/* First byte of the raw-HID report multiplexes: VIA/VialRGB own their own
 * command IDs and never reach here (via.c dispatches those first). Only the
 * three LCD report IDs land in raw_hid_receive_kb. Layout of the report:
 *   [0] cmd (0x40/0x41/0x42)
 *   [3] data_len
 *   [6] ack byte we write back (0x55 ok / 0x0F busy)
 *   [7..] payload forwarded verbatim to the LCD module
 */
void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    switch (data[0]) {
        case AP_W_SCREEN_INFO:   // 0x40
        case AP_W_SCREEN_DATA: { // 0x41
            uint8_t data_len = data[3];
            if (data_len > length - 7) {
                data_len = length - 7;
            }
#if defined(AL80_LCD_ENABLE)
            /* No byte-swap: the display module reads RGB565 big-endian, same as al80-studio
               sends. Confirmed on-device 2026-07-05 (forced 0xE007 -> red; swapped-to-LE -> blue). */
            sdWrite(&SD3, &data[7], data_len);
            /* Match the stock/b75Pro handler: a short settle after each block so
               the self-parsing module keeps up with back-to-back writes. */
            if (data[0] == AP_W_SCREEN_DATA) {
                wait_us(5);
            }
#endif
            g_screen_busy = true; screen_busy_wd = 200; /* pause RGB SPI during transfer */
            data[6] = 0x55; // ACK
            break;
        }
        case AP_GIVE_SCREEN_SEM: // 0x42
            g_screen_busy = false; screen_busy_wd = 0; /* transfer done - resume RGB */
            data[6] = 0x55;      // release semaphore -> ACK
            break;

        /* ---- user-editable palette protocol ----
         * The report buffer is echoed back by via.c on return, so responses
         * are written in place. See config.h for the opcode + ACK layout. */
        case AP_PALETTE_GET: { // 0x43 -> [0x43, count, h0,s0, h1,s1, ...]
            data[1] = AL80_PALETTE_LEN;
            for (uint8_t i = 0; i < AL80_PALETTE_LEN; i++) {
                data[2 + i * 2] = al80_palette[i].h;
                data[3 + i * 2] = al80_palette[i].s;
            }
            break;
        }
        case AP_PALETTE_SET: { // 0x44: data[1]=index, data[2]=h, data[3]=s (RAM only)
            uint8_t idx = data[1];
            if (idx < AL80_PALETTE_LEN) {
                al80_palette[idx].h = data[2];
                al80_palette[idx].s = data[3];
                data[6] = 0x55; // ACK (index/h/s left echoed in place)
            } else {
                data[6] = 0x0F; // out-of-range index
            }
            break;
        }
        case AP_PALETTE_SAVE: // 0x45: commit RAM mirror to EEPROM
            al80_palette_save();
            data[6] = 0x55;   // ACK
            break;

        /* ---- independent side-LED-bar protocol (mirrors the palette above) ---- */
        case AP_BAR_GET: { // 0x46 -> [0x46, h, s, v, independent]
            data[1] = bar_h;
            data[2] = bar_s;
            data[3] = bar_v;
            data[4] = bar_independent ? 1 : 0;
            break;
        }
        case AP_BAR_SET: { // 0x47: data[1]=h, data[2]=s, data[3]=v, data[4]=independent (RAM only)
            bar_h           = data[1];
            bar_s           = data[2];
            bar_v           = data[3];
            bar_independent = data[4] ? true : false;
            data[6] = 0x55; // ACK (h/s/v/independent left echoed in place)
            break;
        }
        case AP_BAR_SAVE: // 0x48: commit bar state to EEPROM
            al80_bar_save();
            data[6] = 0x55; // ACK
            break;

        /* ---- per-key live LED stream (host audio-reactive), RAM only ----
         * [0x49, offset, count, r,g,b x count]. Mirrors the 0x47 side-bar SET: memcpy the chunk
         * into g_live_rgb, mark the stream active + timestamp it (matrix_scan_kb idles it out when
         * frames stop). count max = (RAW_EPSIZE-3)/3 = 20; 82 LEDs => 5 reports. No EEPROM. */
        case AP_LIVE_LEDS: { // 0x49
            uint8_t off = data[1], cnt = data[2];
            /* bound BOTH the destination (off+cnt <= 82) AND the source read from the report:
             * cnt <= (length-3)/3 == 20, so a malformed cnt can't memcpy past the 64-byte report. */
            if ((uint16_t)off + cnt <= RGB_MATRIX_LED_COUNT && cnt <= (uint8_t)((length - 3) / 3)) {
                memcpy(&g_live_rgb[off * 3], &data[3], (uint16_t)cnt * 3);
                g_live_active = true;
                g_live_last   = timer_read32();
                data[6] = 0x55; // ACK in place (echoed by via.c raw_hid_send)
            } else {
                data[6] = 0x0F; // out of range
            }
            break;
        }

#if defined(AL80_WIRELESS_ENABLE)
        /* ---- wireless diagnostic ----
         * [0x4C] -> [0x4C, mode, connected, tx_hi, tx_lo, rx_hi, rx_lo, last4...]
         * The radio is a black box and this is the first firmware to talk to it,
         * so the byte counters are the only way to tell "we never transmitted"
         * apart from "we transmitted and it ignored us". */
        case 0x4C:
            al80_wireless_debug(&data[1]);
            data[52] = (uint8_t)(al80_batt.raw >> 8);  data[53] = (uint8_t)al80_batt.raw;
            data[54] = (uint8_t)(al80_batt.vref >> 8); data[55] = (uint8_t)al80_batt.vref;
            data[56] = (uint8_t)(al80_batt.mv >> 8);   data[57] = (uint8_t)al80_batt.mv;
            data[58] = al80_batt.pct;
            data[59] = al80_batt.status;
            data[60] = (uint8_t)((readPin(AL80_PLUG_PIN) ? 0x01 : 0) | (al80_idle ? 0x02 : 0));
            data[61] = AL80_FW_BUILD;
            break;
#endif

#if defined(AL80_DEV_HID_BOOT)
        /* Development builds only: [0x4E, 'B','O','O','T'] enters the bootloader without a key press. */
        case 0x4E:
            if (data[1] == 'B' && data[2] == 'O' && data[3] == 'O' && data[4] == 'T') {
                hold_key   = AL80_KC_BOOT;
                hold_since = timer_read32() - AL80_HOLD_MS - 1;
                data[6]    = 0x55;
            }
            break;
#endif

        default:
            break;
    }
    /* via.c calls raw_hid_send(data, length) for us on return. */
}

/* Watchdog: resume RGB if an LCD transfer stalls (0x42 lost), so lighting can't freeze. */
void matrix_scan_kb(void) {
    if (screen_busy_wd && --screen_busy_wd == 0) g_screen_busy = false;
    /* Live-LED idle fallback: if the host stops streaming, drop back to the prior RGB effect. */
    if (g_live_active && timer_elapsed32(g_live_last) > AL80_LIVE_IDLE_MS) g_live_active = false;
    matrix_scan_user();
}

/* Push the battery to the module's homepage gauge every 10s (first push ~2s after boot), but
 * never while an image transfer is in flight (would interleave bytes on USART3). */
void housekeeping_task_kb(void) {
    static uint32_t batt_timer = 0;
    static uint32_t init_timer = 0;
    static uint8_t  boot_inits = 0;   /* run the homepage init a few times over the first ~6s */
    static uint32_t sample_timer = 0;
    if (!g_screen_busy && timer_elapsed32(sample_timer) > 10000) {
        sample_timer = timer_read32();
        al80_batt_sample();
#if defined(AL80_WIRELESS_ENABLE)
        static uint8_t radio_batt = 0;
        if (++radio_batt >= 6) {      /* once a minute is plenty for the host's battery widget */
            radio_batt = 0;
            al80_wireless_battery_push(al80_batt.pct);
        }
#endif
    }
    if (hold_key != KC_NO && timer_elapsed32(hold_since) > AL80_HOLD_MS) {
        const uint16_t key = hold_key;
        hold_key           = KC_NO;
        clear_keyboard();
        if (key == AL80_KC_BOOT) {
            bootloader_jump();
        } else {
            eeconfig_init(); /* the layout stamp goes with it, so the next boot starts clean */
            soft_reset_keyboard();
        }
    }
    al80_os_task();
    if (homepage_dirty) {
        homepage_dirty = false;
        boot_inits     = 3; /* one more homepage push on the next pass */
    }
    if (!al80_idle_task(&boot_inits) && !g_screen_busy) {
        if (rotate_on && !view_request && timer_elapsed32(rotate_time) > (rotate_gif ? AL80_ROTATE_GIF_MS : AL80_ROTATE_HOME_MS)) {
            rotate_gif   = !rotate_gif;
            rotate_time  = timer_read32();
            view_request = rotate_gif ? 0x0F : 0x0B;
        }
        if (view_request) {                /* a host-free view key was pressed; flush the announce */
            uint8_t v    = view_request;
            view_request = 0;
            al80_screen_view(v);           /* 7-byte PK_GO home/picture/gif over USART3 */
        }
        if (locks_dirty) {                 /* a Caps/Num toggle landed mid-transfer; flush it now */
            locks_dirty = false;
            al80_locks_push();
        }
        if (boot_inits < 4) {
            if (timer_elapsed32(init_timer) > 1500) {
                init_timer = timer_read32();
                boot_inits++;
                al80_homepage_init();
            }
        } else if (timer_elapsed32(init_timer) > 30000) {   /* self-heal the widgets */
            init_timer = timer_read32();
            al80_homepage_init();
        } else if (timer_elapsed32(batt_timer) > 3000) {   /* keep the battery fresh */
            batt_timer = timer_read32();
            al80_battery_push();
        }
    }
#if defined(AL80_WIRELESS_ENABLE)
    /* Outside the !g_screen_busy block on purpose: RX polling is non-blocking and
     * should keep draining even mid-image, while the blocking mode switch inside
     * al80_wireless_task() gates itself on the flag we pass in. */
    al80_wireless_task(g_screen_busy);
#endif
    housekeeping_task_user();
}

void keyboard_pre_init_kb(void) {
    /* A bootloader request that was served, or ignored, must not linger. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
    if (BKP->DR10 == 0x424C) {
        PWR->CR |= PWR_CR_DBP;
        BKP->DR10 = 0;
        PWR->CR &= ~PWR_CR_DBP;
    }
    al80_os_init();
#if defined(AL80_WIRELESS_ENABLE)
    /* FIRST, before anything else -- this is what stock does.
     *
     * board.h puts PA9 in alternate-function push-pull at halInit, so until
     * USART1 is actually enabled the pin is driven by a disabled peripheral:
     * not a guaranteed idle-high UART line. Stock closes that window
     * immediately (serial_init(460800) is the first call in its board init, at
     * 0x08009FBE, ahead of the B7 pulse and the LCD). Our old ordering left it
     * floating through the B7 reset pulse, matrix init, AW20216S init and the
     * whole LCD bring-up -- hundreds of milliseconds that the module can read
     * as a sustained break on its RX. */
    al80_wireless_init();
#endif
    /* Display-module reset pulse. B7 is shared: the aw20216s driver later
       holds it HIGH to enable the LED matrix (AW20216S_EN_PIN B7, confirmed
       on-device: B7 low = no keys). But ripple's screen-init first drives B7
       LOW as a display-module reset/enable (disasm ~0x8009FC2: GPIOB bit7
       output-PP, then BRR bit7) and our firmware never did, so the module
       never reset and stayed dark. Pulse it low here, BEFORE aw driver init
       (which runs during keyboard_init, after this). The aw driver then drives
       B7 back HIGH -> RGB matrix enabled, module already reset. */
    setPinOutput(B7);
    writePinLow(B7);      // display-module reset pulse (matches ripple screen-init)
    wait_ms(20);
    keyboard_pre_init_user();
}

/* ---- first boot after flashing over another firmware ----
 * QMK never erases the emulated EEPROM on a flash, so settings written by the firmware that
 * was there before (stock, or an older build with a different layout) are read back as if
 * they were ours: a default layer that does not exist here, a keymap at other offsets.
 * A stamp in byte 0 of the EECONFIG_USER dword marks the store as written by this layout.
 * When it is missing, everything is reset to compiled defaults and the board restarts once.
 * Byte 1 of the same dword is the stored wireless mode (al80_wireless.c). */
#define AL80_EEPROM_STAMP 0x5E /* change when the stored layout changes */

static void al80_claim_eeprom(void) {
    if ((eeconfig_read_user() & 0xFFu) == AL80_EEPROM_STAMP) {
        return;
    }
    eeconfig_init();
    eeconfig_update_user(AL80_EEPROM_STAMP);
    soft_reset_keyboard();
}

void keyboard_post_init_kb(void) {
    /* Before anything reads stored settings. */
    al80_claim_eeprom();

    /* Free PA13/14/15 + PB3/4 from SWD/JTAG so the matrix can use them */
    AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG_Msk);
    AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_DISABLE;

    /* LCD module control rails (from the cert mk856.c). */
    setPinOutput(A8);
    writePinHigh(A8);   // module power/EN
    setPinOutput(C9);
    writePinHigh(C9);
    setPinInput(B9);    // plug/detect (also the unverified WS2812 bar candidate)

#if defined(AL80_LCD_ENABLE)
    al80_lcd_init();
#endif

    al80_adc_init();
    al80_batt_sample();
    al80_os_apply();
    al80_rotate_load();

    /* Seed the live palette mirror (EEPROM if a valid magic byte is stored,
       else the compiled default without touching flash). */
    al80_palette_load();

    /* Seed the independent side-bar color from its own EEPROM sub-block. */
    al80_bar_load();


    keyboard_post_init_user();
}
