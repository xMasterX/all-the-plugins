#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <notification/notification_messages.h>
#include <cli/cli.h>
#include <toolbox/cli/cli_command.h>
#include <toolbox/pipe.h>
#include <storage/storage.h>
#include <toolbox/crc32_calc.h>
#include <stdlib.h>
#include <string.h>

#include "sws.h"

#define CLI_CMD "sws"

typedef struct {
    const char* name;
    const GpioPin* pin;
    FuriHalAdcChannel adc;
} PinDef;

static const PinDef pins[] = {
    {"c0", &gpio_ext_pc0, FuriHalAdcChannel1},
    {"c1", &gpio_ext_pc1, FuriHalAdcChannel2},
    {"c3", &gpio_ext_pc3, FuriHalAdcChannel4},
};
#define PIN_COUNT (sizeof(pins) / sizeof(pins[0]))

/* live contact state shown on screen / LED */
typedef enum {
    LinkNone, /* line floats: wire not touching anything */
    LinkLow, /* line held low */
    LinkPad, /* something pulls the line up but SWS does not answer (chip asleep?) */
    LinkGood, /* SWS answers */
} LinkState;

typedef struct {
    FuriMutex* lock;
    uint8_t slave_div;
    uint32_t act_ms;
    char status[32];
    /* contact monitor (GUI thread) */
    bool auto_halt;
    volatile LinkState link;
    volatile bool busy; /* a CLI command holds the bus */
    uint16_t chip_id;
    bool halted;
    uint32_t good_probes, total_probes;
} App;

/* ---------- helpers ---------- */

static int tokenize(char* s, char** argv, int max) {
    int n = 0;
    while(*s && n < max) {
        while(*s == ' ')
            s++;
        if(!*s) break;
        argv[n++] = s;
        while(*s && *s != ' ')
            s++;
        if(*s) *s++ = 0;
    }
    return n;
}

static uint32_t num(const char* s) {
    return (uint32_t)strtoul(s, NULL, 0);
}

static const PinDef* find_pin(const char* name) {
    for(size_t i = 0; i < PIN_COUNT; i++)
        if(strcasecmp(name, pins[i].name) == 0) return &pins[i];
    return NULL;
}

static const char* pin_name(const GpioPin* p) {
    for(size_t i = 0; i < PIN_COUNT; i++)
        if(pins[i].pin == p) return pins[i].name;
    return "?";
}

static void hexdump(uint32_t base, const uint8_t* d, size_t n) {
    for(size_t i = 0; i < n; i += 16) {
        printf("%06lX:", (unsigned long)(base + i));
        for(size_t j = i; j < i + 16 && j < n; j++)
            printf(" %02X", d[j]);
        printf("\r\n");
    }
}

static void all_pins_hiz(void) {
    for(size_t i = 0; i < PIN_COUNT; i++)
        sws_pin_hiz(pins[i].pin);
}

static void power_set(bool on) {
    if(on) {
        furi_hal_power_enable_external_3_3v();
    } else {
        all_pins_hiz(); /* avoid back-powering the target through pull-ups */
        furi_hal_power_disable_external_3_3v();
    }
}

/* ---------- contact monitor ---------- */

/* Electrical check of the SWS line when the protocol gets no answer.
 * Discharge with the pull-down, release, and time the rise: a floating wire
 * keeps its charge for a long time, the chip's pad pull-up recharges it fast. */
static LinkState line_check(void) {
    const GpioPin* p = sws_get_pin();
    LinkState st;
    furi_hal_gpio_init(p, GpioModeInput, GpioPullUp, GpioSpeedLow);
    furi_delay_us(200);
    if(!furi_hal_gpio_read(p)) {
        st = LinkLow;
    } else {
        furi_hal_gpio_init(p, GpioModeInput, GpioPullDown, GpioSpeedLow);
        furi_delay_us(500);
        furi_hal_gpio_init(p, GpioModeInput, GpioPullNo, GpioSpeedLow);
        uint32_t t0 = DWT->CYCCNT;
        st = LinkNone;
        while(DWT->CYCCNT - t0 < 2000u * 64u) { /* 2 ms */
            if(furi_hal_gpio_read(p)) {
                st = LinkPad;
                break;
            }
        }
    }
    sws_pin_idle();
    return st;
}

/* one probe of the target; caller holds app->lock */
static LinkState link_probe(App* app) {
    uint8_t id[3];
    /* the slave RX is ratio based, so this write lands whatever the target's
     * current divider is; firmware may have set a reply speed we cannot sample */
    sws_write_u8(0x00b2, app->slave_div);
    if(sws_read(0x007d, id, 3) != 3 && app->auto_halt) {
        /* chip may be waking up (button press): try to halt it */
        uint32_t t0 = furi_get_tick();
        while(furi_get_tick() - t0 < 20) {
            sws_cpu_stop();
            sws_write_u8(0x00b2, app->slave_div);
        }
        sws_read(0x007d, id, 3);
    }
    uint32_t ok, fail;
    sws_link_ticks(&ok, &fail);
    if(ok >= fail && furi_get_tick() - ok < 50) {
        app->chip_id = id[1] | (id[2] << 8);
        uint8_t v = 0;
        app->halted = sws_read(0x0602, &v, 1) == 1 && v == 0x05;
        if(!app->halted && app->auto_halt) {
            sws_cpu_stop();
            app->halted = sws_read(0x0602, &v, 1) == 1 && v == 0x05;
        }
        return LinkGood;
    }
    return line_check();
}

/* ---------- commands ---------- */

/* Flash size from the JEDEC capacity byte, and the sectors holding the
 * factory MAC address + RF frequency calibration, never touched by the
 * flasher: 0x76000-0x77FFF on 512 KB parts, the last 8 KB (0xFE000 MAC
 * 0xFF000 on 1 MB) on bigger ones. */
typedef struct {
    uint32_t size;
    uint32_t prot_start;
    uint32_t prot_end;
} FlashGeom;

static bool flash_geom(FlashGeom* g, uint8_t id[3]) {
    /* the first JEDEC read after activation can be garbage: retry */
    for(int i = 0; i < 3; i++) {
        if(sws_flash_jedec(id) && id[2] >= 0x13 && id[2] <= 0x16) {
            g->size = 1u << id[2];
            if(g->size == 0x80000) {
                g->prot_start = 0x76000u;
                g->prot_end = 0x77FFFu;
            } else {
                g->prot_start = g->size - 0x2000u;
                g->prot_end = g->size - 1u;
            }
            return true;
        }
    }
    printf("JEDEC read failed (%02X %02X %02X) - target not halted?\r\n", id[0], id[1], id[2]);
    return false;
}

static bool range_overlaps_protected(const FlashGeom* g, uint32_t a, uint32_t b) {
    return a <= g->prot_end && b >= g->prot_start;
}

/* block protect bits set (SR1 BP0..BP4, or SR2 CMP) make erase/program
 * silently do nothing: refuse instead */
static bool flash_writable(void) {
    uint8_t s1 = 0, s2 = 0;
    if(!sws_flash_read_status(&s1)) {
        printf("flash status read failed\r\n");
        return false;
    }
    if(!sws_flash_read_status2(&s2) || s2 == 0xff) s2 = 0; /* no SR2 */
    if((s1 & 0x7c) || (s2 & 0x40)) {
        printf(
            "flash is write protected (SR1 %02X SR2 %02X): clear it with `sws fwsr 0`\r\n",
            s1,
            s2);
        return false;
    }
    return true;
}

static void cmd_pins(void) {
    printf("Pull test (no driving). %% of samples reading HIGH:\r\n");
    for(size_t i = 0; i < PIN_COUNT; i++) {
        const GpioPin* p = pins[i].pin;
        int hi[3] = {0};
        int trans = 0;
        const GpioPull pulls[3] = {GpioPullDown, GpioPullUp, GpioPullNo};
        for(int k = 0; k < 3; k++) {
            furi_hal_gpio_init(p, GpioModeInput, pulls[k], GpioSpeedLow);
            furi_delay_ms(5);
            bool prev = furi_hal_gpio_read(p);
            for(int s = 0; s < 1000; s++) {
                bool v = furi_hal_gpio_read(p);
                hi[k] += v;
                if(k == 2 && v != prev) trans++;
                prev = v;
                furi_delay_us(20);
            }
        }
        const char* verdict;
        int pd = hi[0] / 10, pu = hi[1] / 10;
        if(pd < 5 && pu > 95)
            verdict = "follows pull -> floating or weakly pulled";
        else if(pd > 95 && pu > 95)
            verdict = "HIGH regardless -> driven/strong pull-up";
        else if(pd < 5 && pu < 5)
            verdict = "LOW regardless -> driven low / shorted to GND";
        else
            verdict = "mixed -> activity or marginal";
        printf(
            "  %s: pull-down %3d%%  pull-up %3d%%  no-pull %3d%% (%d edges)  %s\r\n",
            pins[i].name,
            pd,
            pu,
            hi[2] / 10,
            trans,
            verdict);
        sws_pin_hiz(p);
    }
    sws_pin_idle();
}

/* open-circuit voltage of each pin (analog mode, no pulls) */
static void cmd_adc(void) {
    FuriHalAdcHandle* adc = furi_hal_adc_acquire();
    furi_hal_adc_configure(adc);
    all_pins_hiz(); /* no pulls anywhere: nothing from us can back-power the target */
    furi_delay_ms(1000);
    for(size_t i = 0; i < PIN_COUNT; i++) {
        float mn = 9999, mx = 0, sum = 0;
        for(int s = 0; s < 64; s++) {
            float v = furi_hal_adc_convert_to_voltage(adc, furi_hal_adc_read(adc, pins[i].adc));
            if(v < mn) mn = v;
            if(v > mx) mx = v;
            sum += v;
            furi_delay_us(100);
        }
        printf(
            "  %s: avg %4d mV  min %4d  max %4d\r\n",
            pins[i].name,
            (int)(sum / 64),
            (int)mn,
            (int)mx);
    }
    furi_hal_adc_release(adc);
    sws_pin_idle();
}

/* charge pin with a pull, release, time how long until it flips.
 * A truly floating node holds its charge for a long time; an external
 * pull (e.g. target's pull-up) flips it quickly. */
static void cmd_decay(void) {
    for(size_t i = 0; i < PIN_COUNT; i++) {
        const GpioPin* p = pins[i].pin;
        uint32_t t_res[2];
        for(int k = 0; k < 2; k++) {
            bool start_high = (k == 1);
            furi_hal_gpio_init(
                p, GpioModeInput, start_high ? GpioPullUp : GpioPullDown, GpioSpeedLow);
            furi_delay_ms(5);
            furi_hal_gpio_init(p, GpioModeInput, GpioPullNo, GpioSpeedLow);
            uint32_t t0 = DWT->CYCCNT, us = 0;
            while(furi_hal_gpio_read(p) == start_high) {
                us = (DWT->CYCCNT - t0) / 64;
                if(us > 200000) break;
            }
            t_res[k] = us;
        }
        printf(
            "  %s: low->high after %s%lu us, high->low after %s%lu us\r\n",
            pins[i].name,
            t_res[0] > 200000 ? ">" : "",
            (unsigned long)t_res[0],
            t_res[1] > 200000 ? ">" : "",
            (unsigned long)t_res[1]);
        sws_pin_hiz(p);
    }
    sws_pin_idle();
}

static bool read_id(bool quiet) {
    uint8_t id[3] = {0};
    size_t n = sws_read(0x007d, id, 3);
    if(n != 3) {
        if(!quiet) printf("Chip ID read failed (%u/3 bytes)\r\n", (unsigned)n);
        return false;
    }
    uint16_t cid = id[1] | (id[2] << 8);
    printf(
        "Chip ID 0x%04X rev 0x%02X %s\r\n",
        cid,
        id[0],
        cid == 0x5562 ? "(TLSR825x)" :
        cid == 0x5325 ? "(TLSR8266)" :
        cid == 0x5326 ? "(TLSR8267)" :
                        "");
    return true;
}

/* power-cycle target and spam "CPU stop" so we catch it before it sleeps */
static bool activate(App* app, const GpioPin* p, uint32_t ms, uint32_t delay_ms) {
    power_set(false);
    furi_delay_ms(300);
    sws_set_pin(p);
    power_set(true);
    sws_set_pin(p);
    /* optionally let the target firmware run (init peripherals) before halting */
    if(delay_ms) furi_delay_ms(delay_ms);
    uint32_t t0 = furi_get_tick();
    while(furi_get_tick() - t0 < ms) {
        sws_cpu_stop();
    }
    sws_write_u8(0x00b2, app->slave_div);
    sws_cpu_stop();
    return read_id(false);
}

static void cmd_act(App* app, int argc, char** argv) {
    uint32_t ms = argc > 1 ? num(argv[1]) : app->act_ms;
    uint32_t delay_ms = argc > 2 ? num(argv[2]) : 0;
    if(argc > 0 && strcmp(argv[0], "all") == 0) {
        for(size_t i = 0; i < PIN_COUNT; i++) {
            printf("[%s] activate %lums: ", pins[i].name, (unsigned long)ms);
            if(activate(app, pins[i].pin, ms, delay_ms)) {
                printf("  -> SWS is on %s\r\n", pins[i].name);
            }
        }
        sws_pin_idle();
        return;
    }
    const GpioPin* p = sws_get_pin();
    if(argc > 0) {
        const PinDef* pd = find_pin(argv[0]);
        if(pd) p = pd->pin;
    }
    printf("[%s] activate %lums: ", pin_name(p), (unsigned long)ms);
    activate(app, p, ms, delay_ms);
}

static void analog_write(uint8_t addr, uint8_t v) {
    const uint8_t cmd[3] = {addr, v, 0x60}; /* START | RW(write) */
    sws_write(0x00b8, cmd, 3);
    sws_write_u8(0x00ba, 0x00);
}

/* print GPIO input levels whenever they change (all input buffers enabled) */
static void cmd_watch(PipeSide* pipe, int argc, char** argv) {
    uint32_t secs = argc > 0 ? num(argv[0]) : 30;
    if(argc > 1 && num(argv[1])) {
        sws_write_u8(0x0581, 0xff); /* PA ie */
        sws_write_u8(0x0599, 0xff); /* PD ie */
        sws_write_u8(0x05a1, 0xff); /* PE ie */
        analog_write(0xbd, 0xff); /* PB ie */
        analog_write(0xc0, 0xff); /* PC ie */
        printf("all input buffers enabled\r\n");
    }
    static const char port_names[] = "ABCDE";
    uint8_t prev[5] = {0}, cur[5];
    bool first = true;
    uint32_t t0 = furi_get_tick();
    while(furi_get_tick() - t0 < secs * 1000) {
        if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) break;
        bool ok = true;
        for(int i = 0; i < 5 && ok; i++)
            ok = sws_read(0x0580 + i * 8, &cur[i], 1) == 1;
        if(!ok) {
            printf("read failed\r\n");
            furi_delay_ms(100);
            continue;
        }
        if(first || memcmp(cur, prev, 5)) {
            printf("%6lums ", (unsigned long)(furi_get_tick() - t0));
            for(int i = 0; i < 5; i++)
                printf(" P%c=%02X", port_names[i], cur[i]);
            if(!first) {
                printf("  changed:");
                for(int i = 0; i < 5; i++)
                    for(int b = 0; b < 8; b++)
                        if((cur[i] ^ prev[i]) & (1 << b))
                            printf(" P%c%d->%d", port_names[i], b, (cur[i] >> b) & 1);
            }
            printf("\r\n");
            memcpy(prev, cur, 5);
            first = false;
        }
        furi_delay_ms(5);
    }
    printf("watch done\r\n");
}

/* analog register block read: [0xb8]=addr, [0xba]=START, data in [0xb9] */
static void cmd_ard(int argc, char** argv) {
    if(argc < 2) return;
    uint32_t addr = num(argv[0]) & 0xff, len = num(argv[1]);
    if(len > 256) len = 256;
    uint8_t d[256];
    size_t i;
    for(i = 0; i < len; i++) {
        const uint8_t cmd[3] = {(uint8_t)(addr + i), 0x00, 0x40};
        sws_write(0x00b8, cmd, 3);
        uint8_t r[2];
        if(sws_read(0x00b9, r, 2) != 2 || (r[1] & 0x01)) break;
        d[i] = r[0];
        sws_write_u8(0x00ba, 0x00);
    }
    hexdump(addr, d, i);
    if(i != len) printf("short read: %u/%lu\r\n", (unsigned)i, (unsigned long)len);
}

static void cmd_dbg(void) {
    const SwsRxDebug* d = sws_last_rx_debug();
    printf("last rx: %s, %u edges (us since release)\r\n", d->valid ? "ok" : "FAIL", d->edges);
    for(int i = 0; i < d->edges; i++) {
        uint32_t f = d->fall[i], r = d->rise[i];
        printf(
            "  %d: fall %4lu.%02lu rise %4lu.%02lu low %3lu.%02lu\r\n",
            i,
            f / 64,
            (f % 64) * 100 / 64,
            r / 64,
            (r % 64) * 100 / 64,
            (r - f) / 64,
            ((r - f) % 64) * 100 / 64);
    }
}

static void cmd_dump(App* app, PipeSide* pipe, int argc, char** argv) {
    UNUSED(app);
    if(argc < 3) {
        printf("usage: dump <addr> <len> <path> [verify=1]\r\n");
        return;
    }
    uint32_t addr = num(argv[0]), len = num(argv[1]);
    bool verify = argc > 3 ? num(argv[3]) != 0 : true;
    const size_t chunk = 256;
    uint8_t* a = malloc(chunk);
    uint8_t* b = malloc(chunk);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    if(!storage_file_open(f, argv[2], FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        printf("cannot open %s\r\n", argv[2]);
        goto out;
    }
    uint32_t crc = 0, retries = 0, t0 = furi_get_tick();
    uint32_t done = 0;
    while(done < len) {
        if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
            printf("\r\naborted\r\n");
            break;
        }
        size_t n = len - done < chunk ? len - done : chunk;
        bool ok = false;
        for(int attempt = 0; attempt < 6 && !ok; attempt++) {
            if(attempt) retries++;
            if(sws_flash_read(addr + done, a, n) != n) continue;
            if(!verify) {
                ok = true;
                break;
            }
            if(sws_flash_read(addr + done, b, n) != n) continue;
            ok = memcmp(a, b, n) == 0;
        }
        if(!ok) {
            /* contact lost (hand-held wire): wait until the chip answers again,
             * re-halting it in case it woke up and went back to sleep */
            printf(
                "\r\ncontact lost at 0x%06lX, waiting up to 10 min (Ctrl-C aborts)\r\n",
                (unsigned long)(addr + done));
            snprintf(
                app->status, sizeof(app->status), "LOST @%06lX", (unsigned long)(addr + done));
            uint32_t tw = furi_get_tick();
            bool back = false, abort = false;
            while(!back && furi_get_tick() - tw < 600000) {
                if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
                    abort = true;
                    break;
                }
                sws_cpu_stop();
                sws_write_u8(0x00b2, app->slave_div);
                uint8_t id[3];
                /* require a few consecutive good reads before resuming */
                back = true;
                for(int k = 0; k < 5 && back; k++)
                    back = sws_read(0x007d, id, 3) == 3 && id[1] == 0x62 && id[2] == 0x55;
                if(!back) furi_delay_ms(10);
            }
            if(!back) {
                printf(abort ? "aborted\r\n" : "gave up\r\n");
                break;
            }
            sws_write_u8(0x00b3, 0x00);
            sws_write_u8(0x00b2, app->slave_div);
            sws_cpu_stop();
            printf(
                "contact back after %lus, resuming\r\n",
                (unsigned long)((furi_get_tick() - tw) / 1000));
            continue;
        }
        storage_file_write(f, a, n);
        crc = crc32_calc_buffer(crc, a, n);
        done += n;
        if((done & 0xfff) == 0 || done == len) {
            uint32_t el = furi_get_tick() - t0;
            printf(
                "\r0x%06lX / 0x%06lX  %lus  retries %lu   ",
                (unsigned long)(addr + done),
                (unsigned long)(addr + len),
                (unsigned long)(el / 1000),
                (unsigned long)retries);
            snprintf(
                app->status, sizeof(app->status), "dump %lu%%", (unsigned long)(done * 100 / len));
        }
    }
    printf(
        "\r\nwrote %lu bytes to %s, crc32 %08lX\r\n",
        (unsigned long)done,
        argv[2],
        (unsigned long)crc);
    snprintf(app->status, sizeof(app->status), "dump done %luK", (unsigned long)(done / 1024));
    storage_file_close(f);
out:
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    free(a);
    free(b);
}

static void cmd_flash(App* app, PipeSide* pipe, int argc, char** argv) {
    if(argc < 2) {
        printf("usage: flash <path> <addr> [verify=1]\r\n");
        return;
    }
    uint32_t addr = num(argv[1]);
    bool verify = argc > 2 ? num(argv[2]) != 0 : true;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    if(!storage_file_open(f, argv[0], FSAM_READ, FSOM_OPEN_EXISTING)) {
        printf("cannot open %s\r\n", argv[0]);
        goto out;
    }
    uint64_t fsize64 = storage_file_size(f);
    if(fsize64 == 0 || fsize64 > 0x400000) {
        printf("bad size %llu\r\n", (unsigned long long)fsize64);
        goto out_close;
    }
    uint32_t size = (uint32_t)fsize64;
    uint8_t id[3] = {0};
    FlashGeom g;
    if(!flash_geom(&g, id)) goto out_close;
    if((addr + size - 1) >= g.size) {
        printf(
            "range 0x%06lX-0x%06lX exceeds %luK flash\r\n",
            (unsigned long)addr,
            (unsigned long)(addr + size - 1),
            (unsigned long)(g.size >> 10));
        goto out_close;
    }
    if(range_overlaps_protected(&g, addr, addr + size - 1)) {
        printf(
            "REFUSED: range 0x%06lX-0x%06lX overlaps protected area 0x%06lX-0x%06lX\r\n"
            "(factory MAC address + RF calibration)\r\n",
            (unsigned long)addr,
            (unsigned long)(addr + size - 1),
            (unsigned long)g.prot_start,
            (unsigned long)g.prot_end);
        goto out_close;
    }
    if(!flash_writable()) goto out_close;
    printf(
        "flashing %lu bytes at 0x%06lX, JEDEC %02X %02X %02X, verify %d\r\n",
        (unsigned long)size,
        (unsigned long)addr,
        id[0],
        id[1],
        id[2],
        verify);

    const size_t chunk = 256;
    uint8_t* a = malloc(chunk);
    uint8_t* b = malloc(chunk);
    uint32_t crc = 0, t0 = furi_get_tick();
    uint32_t done = 0;
    bool ok = true;

    /* 1. erase all covered sectors */
    uint32_t es = addr & ~0xfffu;
    uint32_t ee = (addr + size - 1) | 0xfffu;
    uint32_t es_count = ((ee - es) >> 12) + 1;
    uint32_t es_done = 0;
    for(uint32_t s = es; s <= ee && ok; s += 0x1000, es_done++) {
        if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
            printf("\r\naborted\r\n");
            ok = false;
            break;
        }
        if(!sws_flash_erase_sector(s)) {
            printf("\r\nerase failed at 0x%06lX\r\n", (unsigned long)s);
            ok = false;
        }
        printf("\rerasing %lu/%lu   ", (unsigned long)(es_done + 1), (unsigned long)es_count);
    }

    /* 2. write + verify page by page */
    while(ok && done < size) {
        if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
            printf("\r\naborted\r\n");
            break;
        }
        size_t n = size - done < chunk ? size - done : chunk;
        if(storage_file_read(f, a, n) != n) {
            printf("\r\nfile read failed\r\n");
            break;
        }
        /* skip all-0xff pages (already erased) to save time */
        bool ff = true;
        for(size_t i = 0; i < n; i++)
            if(a[i] != 0xff) {
                ff = false;
                break;
            }
        bool page_ok = false;
        for(int attempt = 0; attempt < 4 && !page_ok; attempt++) {
            if(ff) {
                page_ok = true;
                break;
            }
            if(!sws_flash_write_page(addr + done, a, n)) continue;
            page_ok = true;
            if(verify) {
                if(sws_flash_read(addr + done, b, n) != n || memcmp(a, b, n) != 0) page_ok = false;
            }
        }
        if(!page_ok) {
            printf("\r\nwrite failed at 0x%06lX\r\n", (unsigned long)(addr + done));
            break;
        }
        crc = crc32_calc_buffer(crc, a, n);
        done += n;
        if((done & 0x3fff) == 0 || done == size) {
            uint32_t el = furi_get_tick() - t0;
            printf(
                "\r0x%06lX / 0x%06lX  %lus  crc %08lX   ",
                (unsigned long)(addr + done),
                (unsigned long)(addr + size),
                (unsigned long)(el / 1000),
                (unsigned long)crc);
            snprintf(
                app->status,
                sizeof(app->status),
                "flash %lu%%",
                (unsigned long)(done * 100 / size));
        }
    }
    printf("\r\nflashed %lu bytes, crc32 %08lX\r\n", (unsigned long)done, (unsigned long)crc);
    snprintf(app->status, sizeof(app->status), "flash done");
    free(a);
    free(b);
out_close:
    storage_file_close(f);
out:
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

static void cmd_erase(App* app, PipeSide* pipe, int argc, char** argv) {
    UNUSED(app);
    if(argc < 2) {
        printf("usage: erase <addr> <len>   (4K sector aligned)\r\n");
        return;
    }
    uint32_t addr = num(argv[0]), len = num(argv[1]);
    uint32_t s = addr & ~0xfffu, e = (addr + len - 1) | 0xfffu;
    uint8_t id[3] = {0};
    FlashGeom g;
    if(!flash_geom(&g, id)) return;
    if(e >= g.size) {
        printf("range exceeds %luK flash\r\n", (unsigned long)(g.size >> 10));
        return;
    }
    if(range_overlaps_protected(&g, s, e)) {
        printf(
            "REFUSED: overlaps protected area 0x%06lX-0x%06lX\r\n",
            (unsigned long)g.prot_start,
            (unsigned long)g.prot_end);
        return;
    }
    if(!flash_writable()) return;
    uint32_t cnt = ((e - s) >> 12) + 1, done = 0;
    for(uint32_t x = s; x <= e; x += 0x1000, done++) {
        if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
            printf("\r\naborted\r\n");
            return;
        }
        if(!sws_flash_erase_sector(x)) {
            printf("\r\nerase failed at 0x%06lX\r\n", (unsigned long)x);
            return;
        }
        printf(
            "\rerasing 0x%06lX (%lu/%lu)   ",
            (unsigned long)x,
            (unsigned long)(done + 1),
            (unsigned long)cnt);
    }
    printf("\r\nerased %lu sectors\r\n", (unsigned long)cnt);
}

static void print_help(void) {
    printf("sws commands (pins: c0 c1 c3):\r\n"
           "  pins                    pull-up/down float test on all pins\r\n"
           "  adc                     open-circuit voltage of each pin\r\n"
           "  decay                   charge/release timing (float vs weak pull)\r\n"
           "  pin <c0|c1|c3>          select SWS pin\r\n"
           "  unit <ns>               master unit time (default 2000)\r\n"
           "  div <n>                 slave unit divider written to [0xb2] on act\r\n"
           "  power <on|off>          target 3.3V rail\r\n"
           "  act [pin|all] [ms] [delay]  power-cycle, wait delay ms, CPU stop spam, chip id\r\n"
           "  stop | run | reset      CPU stop / run / chip reset (run/reset: auto-halt off)\r\n"
           "  autohalt [on|off]       contact monitor halts the CPU when it answers\r\n"
           "  catch [ms]              spam CPU stop (no power cycle) until halted\r\n"
           "  id                      read chip id\r\n"
           "  rd <addr> <len>         read regs/RAM\r\n"
           "  wr <addr> <b0> [b1..]   write regs/RAM\r\n"
           "  ard <addr> <len>        read analog regs\r\n"
           "  watch [secs] [all_ie]   print GPIO input changes\r\n"
           "  dbg                     edge timing of last read byte\r\n"
           "  jedec                   flash JEDEC id\r\n"
           "  frd <addr> <len>        hexdump flash\r\n"
           "  fstat                   flash status registers (SR1, SR2)\r\n"
           "  fwsr <sr1> [sr2]        write flash status (fwsr 0 = unprotect)\r\n"
           "  erase <addr> <len>      erase 4K sectors (refuses MAC/calibration)\r\n"
           "  flash <path> <addr> [v]  SD file -> flash, erase+write+verify\r\n"
           "  dump <addr> <len> <path> [verify]  flash -> SD file\r\n");
}

static void sws_cli(PipeSide* pipe, FuriString* args, void* context) {
    App* app = context;
    furi_mutex_acquire(app->lock, FuriWaitForever);

    char buf[128];
    strlcpy(buf, furi_string_get_cstr(args), sizeof(buf));
    char* argv[20];
    int argc = tokenize(buf, argv, 20);
    if(argc == 0 || strcmp(argv[0], "help") == 0) {
        print_help();
        printf(
            "state: pin %s, unit %luns, div %u\r\n",
            pin_name(sws_get_pin()),
            (unsigned long)sws_get_unit_ns(),
            app->slave_div);
        goto out;
    }
    const char* c = argv[0];
    int n = argc - 1;
    char** a = argv + 1;
    snprintf(app->status, sizeof(app->status), "%s", c);

    if(!strcmp(c, "pins")) {
        cmd_pins();
    } else if(!strcmp(c, "adc")) {
        cmd_adc();
    } else if(!strcmp(c, "decay")) {
        cmd_decay();
    } else if(!strcmp(c, "pin") && n >= 1) {
        const PinDef* p = find_pin(a[0]);
        if(p) {
            sws_set_pin(p->pin);
            printf("SWS pin = %s\r\n", p->name);
        } else {
            printf("unknown pin\r\n");
        }
    } else if(!strcmp(c, "unit") && n >= 1) {
        sws_set_unit_ns(num(a[0]));
        printf("unit = %luns\r\n", (unsigned long)sws_get_unit_ns());
    } else if(!strcmp(c, "div") && n >= 1) {
        app->slave_div = num(a[0]) & 0x7f;
        sws_write_u8(0x00b2, app->slave_div);
        printf("slave div = %u\r\n", app->slave_div);
    } else if(!strcmp(c, "power") && n >= 1) {
        bool on = !strcmp(a[0], "on") || !strcmp(a[0], "1");
        power_set(on);
        if(on) sws_pin_idle();
        printf("3.3V %s\r\n", on ? "on" : "off");
    } else if(!strcmp(c, "act")) {
        cmd_act(app, n, a);
    } else if(!strcmp(c, "stop")) {
        sws_cpu_stop();
    } else if(!strcmp(c, "run")) {
        app->auto_halt = false; /* or the contact monitor halts it again */
        sws_cpu_run();
        printf("running, auto-halt off\r\n");
    } else if(!strcmp(c, "reset")) {
        /* software reset of the whole chip: boots whatever is in flash */
        app->auto_halt = false;
        sws_write_u8(0x006f, 0x20);
        printf("reset sent, auto-halt off\r\n");
    } else if(!strcmp(c, "autohalt")) {
        if(n >= 1) app->auto_halt = !strcmp(a[0], "on") || !strcmp(a[0], "1");
        printf("auto-halt %s\r\n", app->auto_halt ? "on" : "off");
    } else if(!strcmp(c, "id")) {
        read_id(false);
    } else if(!strcmp(c, "rd") && n >= 2) {
        uint32_t addr = num(a[0]), len = num(a[1]);
        if(len > 256) len = 256;
        uint8_t d[256];
        size_t got = sws_read(addr, d, len);
        hexdump(addr, d, got);
        if(got != len) printf("short read: %u/%lu\r\n", (unsigned)got, (unsigned long)len);
    } else if(!strcmp(c, "wr") && n >= 2) {
        uint8_t d[18];
        int cnt = 0;
        for(int i = 1; i < n && cnt < 18; i++)
            d[cnt++] = num(a[i]);
        sws_write(num(a[0]), d, cnt);
        printf("ok\r\n");
    } else if(!strcmp(c, "catch")) {
        /* spam CPU stop without power-cycling: catch a running/waking target */
        uint32_t ms = n > 0 ? num(a[0]) : 5000;
        uint32_t t0 = furi_get_tick();
        bool got = false;
        while(furi_get_tick() - t0 < ms && !got) {
            sws_cpu_stop();
            sws_write_u8(0x00b2, app->slave_div);
            uint8_t v;
            got = sws_read(0x0602, &v, 1) == 1 && v == 0x05;
        }
        if(got) {
            printf("caught after %lums: ", (unsigned long)(furi_get_tick() - t0));
            read_id(false);
        } else {
            printf("no response in %lums\r\n", (unsigned long)ms);
        }
    } else if(!strcmp(c, "watch")) {
        cmd_watch(pipe, n, a);
    } else if(!strcmp(c, "ard") && n >= 2) {
        cmd_ard(n, a);
    } else if(!strcmp(c, "dbg")) {
        cmd_dbg();
    } else if(!strcmp(c, "jedec")) {
        uint8_t id[3];
        if(sws_flash_jedec(id))
            printf(
                "JEDEC %02X %02X %02X -> %lu KB\r\n",
                id[0],
                id[1],
                id[2],
                (unsigned long)((1ul << id[2]) >> 10));
        else
            printf("JEDEC read failed\r\n");
    } else if(!strcmp(c, "frd") && n >= 2) {
        uint32_t addr = num(a[0]), len = num(a[1]);
        if(len > 256) len = 256;
        uint8_t d[256];
        size_t got = sws_flash_read(addr, d, len);
        hexdump(addr, d, got);
        if(got != len) printf("short read: %u/%lu\r\n", (unsigned)got, (unsigned long)len);
    } else if(!strcmp(c, "fstat")) {
        uint8_t st = 0, st2 = 0;
        bool ok2 = sws_flash_read_status2(&st2);
        if(sws_flash_read_status(&st))
            printf(
                "flash SR1 %02X (busy %d, WEL %d, BP %02X) SR2 %02X%s\r\n",
                st,
                st & 1,
                (st >> 1) & 1,
                (st >> 2) & 0x1f,
                st2,
                ok2 ? "" : " (read failed)");
        else
            printf("status read failed\r\n");
    } else if(!strcmp(c, "fwsr") && n >= 1) {
        uint8_t sr[2] = {(uint8_t)num(a[0]), n >= 2 ? (uint8_t)num(a[1]) : 0};
        uint8_t st = 0, st2 = 0;
        bool ok = sws_flash_write_status(sr, n >= 2 ? 2 : 1);
        sws_flash_read_status(&st);
        sws_flash_read_status2(&st2);
        printf("%s, SR1 now %02X SR2 %02X\r\n", ok ? "written" : "timeout", st, st2);
    } else if(!strcmp(c, "erase") && n >= 2) {
        cmd_erase(app, pipe, n, a);
    } else if(!strcmp(c, "flash")) {
        cmd_flash(app, pipe, n, a);
    } else if(!strcmp(c, "dump")) {
        cmd_dump(app, pipe, n, a);
    } else {
        print_help();
    }
out:
    furi_mutex_release(app->lock);
}

/* ---------- GUI ---------- */

static void draw_cb(Canvas* canvas, void* ctx) {
    App* app = ctx;
    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Telink SWS  pin %s", pin_name(sws_get_pin()));
    canvas_draw_str(canvas, 2, 9, line);

    static const char* const names[] = {
        [LinkNone] = "NO CONTACT",
        [LinkLow] = "LINE LOW",
        [LinkPad] = "PAD, NO REPLY",
        [LinkGood] = "CONTACT OK",
    };
    LinkState link = app->link;
    canvas_set_font(canvas, FontPrimary);
    if(link == LinkGood) {
        canvas_draw_box(canvas, 0, 13, 128, 15);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 64, 21, AlignCenter, AlignCenter, names[link]);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_frame(canvas, 0, 13, 128, 15);
        canvas_draw_str_aligned(canvas, 64, 21, AlignCenter, AlignCenter, names[link]);
    }

    canvas_set_font(canvas, FontSecondary);
    if(link == LinkGood && !app->busy)
        snprintf(
            line,
            sizeof(line),
            "chip %04X %s  %lu%%",
            app->chip_id,
            app->halted ? "halted" : "running",
            (unsigned long)(app->total_probes ? app->good_probes * 100 / app->total_probes : 0));
    else if(link == LinkPad)
        snprintf(line, sizeof(line), "asleep? press a button");
    else if(app->busy)
        snprintf(line, sizeof(line), "busy (CLI)");
    else
        line[0] = 0;
    canvas_draw_str(canvas, 2, 39, line);
    canvas_draw_str(canvas, 2, 50, app->status);
    snprintf(line, sizeof(line), "OK: auto-halt %s", app->auto_halt ? "ON" : "off");
    canvas_draw_str(canvas, 2, 61, line);
}

static void input_cb(InputEvent* ev, void* ctx) {
    furi_message_queue_put(ctx, ev, FuriWaitForever);
}

int32_t telink_sws_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    app->lock = furi_mutex_alloc(FuriMutexTypeNormal);
    app->slave_div = 0x40;
    app->act_ms = 500;
    app->auto_halt = true;
    app->link = LinkNone;
    strlcpy(app->status, "idle", sizeof(app->status));

    sws_set_unit_ns(2000);
    sws_set_pin(&gpio_ext_pc0);
    furi_hal_power_enable_external_3_3v();

    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_add_command_ex(cli, CLI_CMD, CliCommandFlagParallelSafe, sws_cli, app, 4096);

    FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_cb, app);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    NotificationApp* notif = furi_record_open(RECORD_NOTIFICATION);
    static const NotificationSequence seq_gain = {
        &message_note_c7, &message_delay_50, &message_sound_off, NULL};
    static const NotificationSequence seq_lost = {
        &message_note_c5, &message_delay_100, &message_sound_off, NULL};
    static const NotificationSequence* const leds[] = {
        [LinkNone] = &sequence_set_only_red_255,
        [LinkLow] = &sequence_set_only_red_255,
        [LinkPad] = &sequence_set_only_blue_255,
        [LinkGood] = &sequence_set_only_green_255,
    };
    LinkState shown = LinkNone;
    notification_message(notif, leds[shown]);

    InputEvent ev;
    while(true) {
        if(furi_message_queue_get(q, &ev, 50) == FuriStatusOk) {
            if(ev.type == InputTypeShort && ev.key == InputKeyBack) break;
            if(ev.type == InputTypeShort && ev.key == InputKeyOk) app->auto_halt = !app->auto_halt;
        }
        /* probe while the bus is free; while a command runs, follow its reads */
        if(furi_mutex_acquire(app->lock, 0) == FuriStatusOk) {
            app->busy = false;
            app->link = link_probe(app);
            furi_mutex_release(app->lock);
            app->total_probes++;
            if(app->link == LinkGood) app->good_probes++;
            if(app->total_probes >= 40) { /* contact quality over the last ~2-4 s */
                app->total_probes /= 2;
                app->good_probes /= 2;
            }
        } else {
            app->busy = true;
            uint32_t ok, fail;
            sws_link_ticks(&ok, &fail);
            if(ok >= fail && furi_get_tick() - ok < 1000)
                app->link = LinkGood;
            else if(fail > ok)
                app->link = LinkNone;
        }
        LinkState now = app->link;
        if(now != shown) {
            notification_message(notif, leds[now]);
            if(now == LinkGood)
                notification_message(notif, &seq_gain);
            else if(shown == LinkGood)
                notification_message(notif, &seq_lost);
            shown = now;
        }
        view_port_update(vp);
    }
    notification_message(notif, &sequence_reset_rgb);
    furi_record_close(RECORD_NOTIFICATION);

    /* wait for a running command to finish, then unregister */
    furi_mutex_acquire(app->lock, FuriWaitForever);
    cli_registry_delete_command(cli, CLI_CMD);
    furi_mutex_release(app->lock);
    furi_record_close(RECORD_CLI);

    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(q);

    all_pins_hiz();
    furi_hal_power_enable_external_3_3v();
    furi_mutex_free(app->lock);
    free(app);
    return 0;
}
