#pragma once

#include <furi.h>
#include <furi_hal.h>

/* Telink SWire (single wire) master, bit-banged on one Flipper GPIO.
 *
 * Bit encoding (one "unit" = u):
 *   0 -> 1u low, 4u high
 *   1 -> 4u low, 1u high
 * Byte frame: [cmd flag][d7..d0][1u low end]
 * Write: START(cmd,0x5a) A23..16 A15..8 A7..0 RW_ID(0x00) data... END(cmd,0xff)
 * Read:  START(cmd,0x5a) A23..16 A15..8 A7..0 RW_ID(0x80), then per byte the
 *        master sends 1u low and the slave answers 8 bits + 1u low (in its own
 *        unit timing, set by slave reg 0x00b2), finally END(cmd,0xff).
 */

typedef struct {
    uint32_t fall[9];
    uint32_t rise[9];
    uint8_t edges;
    bool valid;
} SwsRxDebug;

void sws_set_pin(const GpioPin* pin);
const GpioPin* sws_get_pin(void);
void sws_set_unit_ns(uint32_t ns);
uint32_t sws_get_unit_ns(void);

/* Pin becomes input with pull-up (idle state). */
void sws_pin_idle(void);
/* Pin becomes analog (high-Z, no pull). */
void sws_pin_hiz(const GpioPin* pin);

void sws_write(uint32_t addr, const uint8_t* data, size_t len);
/* Same, but keeps the SWS address fixed ([0x00b3] fifo mode). */
void sws_write_fifo(uint32_t addr, const uint8_t* data, size_t len);
/* Returns number of bytes successfully read. fifo=true keeps address fixed. */
size_t sws_read(uint32_t addr, uint8_t* data, size_t len);
size_t sws_read_fifo(uint32_t addr, uint8_t* data, size_t len);

void sws_write_u8(uint32_t addr, uint8_t v);

const SwsRxDebug* sws_last_rx_debug(void);
/* furi ticks of the last fully successful / failed sws_read() */
void sws_link_ticks(uint32_t* last_ok, uint32_t* last_fail);

/* Target (TLSR825x) helpers */
void sws_cpu_stop(void);
void sws_cpu_run(void);
size_t sws_flash_read(uint32_t faddr, uint8_t* buf, size_t len);
bool sws_flash_jedec(uint8_t id[3]);

/* Flash erase/write (SPI master flow, CPU halted) */
bool sws_flash_read_status(uint8_t* st);
/* status register 2 (0x35; GigaDevice/Winbond: bit6 CMP, bit1 QE) */
bool sws_flash_read_status2(uint8_t* st);
/* write status register(s) (0x01) with n = 1 or 2 bytes (SR1, SR2).
 * The ZT3L flash (GD C8 60 14) ignores the 2-byte form: use n = 1. */
bool sws_flash_write_status(const uint8_t* sr, size_t n);
bool sws_flash_busy(void);
void sws_flash_write_enable(void);
bool sws_flash_wait_ready(uint32_t timeout_ms);
bool sws_flash_erase_sector(uint32_t faddr);
/* len <= 256, must not cross a 256 B page boundary */
bool sws_flash_write_page(uint32_t faddr, const uint8_t* data, size_t len);
