/* Copyright 2026 Ennio
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Hardware-free logic of the AL80 firmware: battery maths, the radio module's
 * status-frame parser and the mode-switch decode. Kept free of QMK and ChibiOS
 * so firmware/test/test_logic.c can run it on the build machine.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ---- battery ---- */

/* PK_BATT_STATUS values the display module draws. */
#define AL80_BATT_DISCHARGING 0
#define AL80_BATT_CHARGING 1
#define AL80_BATT_FULL 2

/* Cell millivolts at each step of the vendor's discharge curve. */
#define AL80_BATT_MV_0 3200
#define AL80_BATT_MV_5 3300
#define AL80_BATT_MV_10 3470
#define AL80_BATT_MV_40 3630
#define AL80_BATT_MV_60 3760
#define AL80_BATT_MV_80 3930
#define AL80_BATT_MV_85 3980
#define AL80_BATT_MV_100 4150

typedef struct {
    uint16_t raw;    /* ADC count on the cell divider */
    uint16_t vref;   /* ADC count on the internal reference */
    uint16_t mv;     /* derived cell voltage */
    uint8_t  pct;    /* 0-100 */
    uint8_t  status; /* AL80_BATT_* */
    bool     valid;  /* the converter answered */
} al80_batt_t;

static inline uint8_t al80_batt_pct(uint16_t mv) {
    static const uint16_t mvs[]  = {AL80_BATT_MV_0, AL80_BATT_MV_5, AL80_BATT_MV_10, AL80_BATT_MV_40, AL80_BATT_MV_60, AL80_BATT_MV_80, AL80_BATT_MV_85, AL80_BATT_MV_100};
    static const uint8_t  pcts[] = {0, 5, 10, 40, 60, 80, 85, 100};
    if (mv >= AL80_BATT_MV_100) return 100;
    if (mv <= AL80_BATT_MV_0) return 0;
    for (uint8_t i = 1; i < 8; i++) {
        if (mv < mvs[i]) {
            return (uint8_t)(pcts[i - 1] + ((uint32_t)(mv - mvs[i - 1]) * (pcts[i] - pcts[i - 1])) / (mvs[i] - mvs[i - 1]));
        }
    }
    return 100;
}

/* Cell voltage is ratiometric: mv = raw * 1764 / vref (the vendor's divider constant).
 * With the cable in, a full cell reads near zero because the charger has let go of it. */
static inline al80_batt_t al80_batt_eval(uint16_t raw, uint16_t vref, bool plugged) {
    al80_batt_t b = {raw, vref, 0, 100, plugged ? AL80_BATT_CHARGING : AL80_BATT_DISCHARGING, vref != 0};
    if (!b.valid) return b;
    b.mv = (uint16_t)(((uint32_t)raw * 1764) / vref);
    if (plugged && b.mv <= 900) {
        b.status = AL80_BATT_FULL;
    } else {
        b.pct = al80_batt_pct(b.mv);
    }
    return b;
}

/* ---- radio module status frames: 55 03 <cmd 0..2> <mode 0..4> <data> ---- */

#define AL80_WL_SYNC 0x55

typedef struct {
    uint8_t buf[5];
    uint8_t have;
} al80_wl_parser_t;

/* Feed one byte. Returns true when buf holds a complete frame (cmd = buf[2], mode = buf[3], data = buf[4]). */
static inline bool al80_wl_parse(al80_wl_parser_t *p, uint8_t b) {
    if (p->have == 0 && b != AL80_WL_SYNC) return false;
    if ((p->have == 1 && b != 0x03) || (p->have == 2 && b > 0x02) || (p->have == 3 && b > 0x04)) {
        p->have = (b == AL80_WL_SYNC) ? 1 : 0; /* not a status frame: resync */
        if (p->have) p->buf[0] = b;
        return false;
    }
    p->buf[p->have++] = b;
    if (p->have < 5) return false;
    p->have = 0;
    return true;
}

/* Link state carried by a cmd 0 frame: the module sends 0 when the link is up. */
static inline bool al80_wl_link_up(uint8_t data) {
    return data == 0;
}

/* ---- mode switch: each wireless position pulls one pin low ---- */

typedef enum { AL80_SW_WIRED = 0, AL80_SW_BT = 1, AL80_SW_24G = 2, AL80_SW_INVALID = 3 } al80_switch_t;

static inline al80_switch_t al80_switch_decode(bool bt_pin_high, bool g24_pin_high) {
    if (!bt_pin_high && !g24_pin_high) return AL80_SW_INVALID; /* not a position the switch can produce */
    return !bt_pin_high ? AL80_SW_BT : (!g24_pin_high ? AL80_SW_24G : AL80_SW_WIRED);
}
