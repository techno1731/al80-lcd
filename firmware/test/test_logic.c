/* Device-free tests for al80_logic.h.
 *   run:  cc -Wall -Wextra -Werror -I al80-keyboard-src test/test_logic.c -o build/test_logic && build/test_logic
 */
#include <stdio.h>
#include <string.h>

#include "al80_logic.h"

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                \
        }                                                              \
    } while (0)

/* Feed a byte string, return how many frames completed and keep the last one. */
static int feed(al80_wl_parser_t *p, const uint8_t *bytes, size_t n, uint8_t last[3]) {
    int frames = 0;
    for (size_t i = 0; i < n; i++) {
        if (al80_wl_parse(p, bytes[i])) {
            frames++;
            memcpy(last, &p->buf[2], 3);
        }
    }
    return frames;
}

static void test_battery_curve(void) {
    CHECK(al80_batt_pct(0) == 0);
    CHECK(al80_batt_pct(3200) == 0);
    CHECK(al80_batt_pct(3300) == 5);
    CHECK(al80_batt_pct(3470) == 10);
    CHECK(al80_batt_pct(3630) == 40);
    CHECK(al80_batt_pct(3760) == 60);
    CHECK(al80_batt_pct(3930) == 80);
    CHECK(al80_batt_pct(3980) == 85);
    CHECK(al80_batt_pct(4150) == 100);
    CHECK(al80_batt_pct(5000) == 100);
    CHECK(al80_batt_pct(3550) == 25); /* halfway between 10% and 40% */
    for (uint16_t mv = 3000; mv < 4300; mv++) {
        CHECK(al80_batt_pct(mv) <= al80_batt_pct(mv + 1)); /* never goes down as voltage rises */
    }
}

static void test_battery_eval(void) {
    /* vref count 1489 is 1.2 V on a 3.3 V rail at 12 bits. */
    al80_batt_t b = al80_batt_eval(3207, 1489, false); /* 3207*1764/1489 = 3799 mV */
    CHECK(b.valid && b.mv == 3799 && b.status == AL80_BATT_DISCHARGING);
    CHECK(b.pct == 64);

    b = al80_batt_eval(3207, 1489, true);
    CHECK(b.status == AL80_BATT_CHARGING && b.pct == 64);

    b = al80_batt_eval(10, 1489, true); /* charger released a full cell */
    CHECK(b.status == AL80_BATT_FULL && b.pct == 100);

    b = al80_batt_eval(10, 1489, false); /* same reading on battery is an empty cell */
    CHECK(b.status == AL80_BATT_DISCHARGING && b.pct == 0);

    b = al80_batt_eval(3207, 0, false); /* converter did not answer */
    CHECK(!b.valid && b.pct == 100 && b.status == AL80_BATT_DISCHARGING);
}

static void test_battery_follow(void) {
    /* Plugging in makes the reading jump from 23 to 98: the shown level climbs a point per sample. */
    uint8_t shown = 23;
    shown = al80_batt_follow(shown, 98, AL80_BATT_CHARGING);
    CHECK(shown == 24);
    /* A charging cell never appears to lose charge. */
    CHECK(al80_batt_follow(60, 40, AL80_BATT_CHARGING) == 60);
    /* Unplugging drops the reading: the shown level walks down, never up. */
    CHECK(al80_batt_follow(98, 23, AL80_BATT_DISCHARGING) == 97);
    CHECK(al80_batt_follow(40, 55, AL80_BATT_DISCHARGING) == 40);
    /* Limits hold, and a full cell is full. */
    CHECK(al80_batt_follow(100, 100, AL80_BATT_CHARGING) == 100);
    CHECK(al80_batt_follow(0, 0, AL80_BATT_DISCHARGING) == 0);
    CHECK(al80_batt_follow(71, 100, AL80_BATT_FULL) == 100);
}

static void test_parser(void) {
    al80_wl_parser_t p = {{0}, 0};
    uint8_t          f[3] = {0xFF, 0xFF, 0xFF};

    const uint8_t link_up[] = {0x55, 0x03, 0x00, 0x01, 0x00};
    CHECK(feed(&p, link_up, sizeof link_up, f) == 1);
    CHECK(f[0] == 0x00 && f[1] == 0x01 && al80_wl_link_up(f[2]));

    const uint8_t link_down[] = {0x55, 0x03, 0x00, 0x04, 0x01};
    CHECK(feed(&p, link_down, sizeof link_down, f) == 1);
    CHECK(f[1] == 0x04 && !al80_wl_link_up(f[2]));

    const uint8_t leds[] = {0x55, 0x03, 0x01, 0x02, 0x02}; /* caps lock on, slot 2 */
    CHECK(feed(&p, leds, sizeof leds, f) == 1);
    CHECK(f[0] == 0x01 && f[2] == 0x02);

    const uint8_t suspend[] = {0x55, 0x03, 0x02, 0x04, 0xAA};
    CHECK(feed(&p, suspend, sizeof suspend, f) == 1);
    CHECK(f[0] == 0x02 && f[2] == 0xAA);

    /* Noise, a wrong length, a bad command and a bad mode are all dropped. */
    const uint8_t junk[] = {0x00, 0x12, 0x55, 0x09, 0x01, 0x55, 0x03, 0x07, 0x55, 0x03, 0x00, 0x09};
    CHECK(feed(&p, junk, sizeof junk, f) == 0);

    /* The parser recovers on the next good frame, including after a repeated sync byte. */
    const uint8_t recover[] = {0x55, 0x55, 0x03, 0x00, 0x02, 0x00};
    CHECK(feed(&p, recover, sizeof recover, f) == 1);
    CHECK(f[1] == 0x02);

    /* A data byte of 0x55 is data, not a new frame. */
    const uint8_t data55[] = {0x55, 0x03, 0x01, 0x01, 0x55, 0x55, 0x03, 0x00, 0x01, 0x00};
    CHECK(feed(&p, data55, sizeof data55, f) == 2);
}

static void test_switch(void) {
    CHECK(al80_switch_decode(true, true) == AL80_SW_WIRED);
    CHECK(al80_switch_decode(false, true) == AL80_SW_BT);
    CHECK(al80_switch_decode(true, false) == AL80_SW_24G);
    CHECK(al80_switch_decode(false, false) == AL80_SW_INVALID);
}

int main(void) {
    test_battery_curve();
    test_battery_eval();
    test_battery_follow();
    test_parser();
    test_switch();
    printf(failures ? "%d check(s) failed\n" : "all logic checks passed\n", failures);
    return failures ? 1 : 0;
}
