#include "sws.h"

#define CPU_MHZ 64u

static const GpioPin* sws_pin = &gpio_ext_pc0;
static uint32_t unit_cyc = 2 * CPU_MHZ; /* 2 us */
static SwsRxDebug rx_dbg;
static volatile uint32_t link_ok_tick, link_fail_tick;

/* fast pin access */
static GPIO_TypeDef* g_port;
static uint32_t g_mask;
static uint32_t g_shift;

static inline uint32_t cyc(void) {
    return DWT->CYCCNT;
}
static inline void wait_until(uint32_t t) {
    while((int32_t)(DWT->CYCCNT - t) < 0) {
    }
}
static inline void pin_out(void) {
    g_port->MODER = (g_port->MODER & ~(3u << g_shift)) | (1u << g_shift);
}
static inline void pin_in(void) {
    g_port->MODER &= ~(3u << g_shift);
}
static inline void pin_lo(void) {
    g_port->BRR = g_mask;
}
static inline void pin_hi(void) {
    g_port->BSRR = g_mask;
}
static inline bool pin_rd(void) {
    return (g_port->IDR & g_mask) != 0;
}

void sws_set_pin(const GpioPin* pin) {
    sws_pin = pin;
    g_port = pin->port;
    g_mask = pin->pin;
    g_shift = 2u * (uint32_t)__builtin_ctz(pin->pin);
    sws_pin_idle();
}

const GpioPin* sws_get_pin(void) {
    return sws_pin;
}

void sws_set_unit_ns(uint32_t ns) {
    uint32_t c = ns * CPU_MHZ / 1000u;
    if(c < 8) c = 8;
    unit_cyc = c;
}

uint32_t sws_get_unit_ns(void) {
    return unit_cyc * 1000u / CPU_MHZ;
}

void sws_pin_idle(void) {
    if(!g_port) sws_set_pin(sws_pin);
    furi_hal_gpio_write(sws_pin, true);
    furi_hal_gpio_init(sws_pin, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
}

void sws_pin_hiz(const GpioPin* pin) {
    furi_hal_gpio_init(pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

/* --- low level ---------------------------------------------------------- */

static inline void tx_bit(bool b, uint32_t* t) {
    pin_lo();
    *t += (b ? 4u : 1u) * unit_cyc;
    wait_until(*t);
    pin_hi();
    *t += (b ? 1u : 4u) * unit_cyc;
    wait_until(*t);
}

/* Must be called with pin already driven (output high). */
static void tx_byte(uint8_t d, bool cmd) {
    FURI_CRITICAL_ENTER();
    uint32_t t = cyc();
    tx_bit(cmd, &t);
    for(int i = 7; i >= 0; i--)
        tx_bit((d >> i) & 1u, &t);
    /* end: 1u low, then idle high */
    pin_lo();
    t += unit_cyc;
    wait_until(t);
    pin_hi();
    t += unit_cyc;
    wait_until(t);
    FURI_CRITICAL_EXIT();
}

static void bus_begin(void) {
    FURI_CRITICAL_ENTER();
    pin_hi();
    pin_out();
    FURI_CRITICAL_EXIT();
}

static void bus_end(void) {
    FURI_CRITICAL_ENTER();
    pin_hi();
    pin_in();
    FURI_CRITICAL_EXIT();
}

/* Pin must be output-high on entry; left output-high on exit. */
static bool rx_byte(uint8_t* out) {
    bool ok = false;
    uint32_t fall[9], rise[9];
    uint8_t n = 0;

    FURI_CRITICAL_ENTER();
    uint32_t t = cyc();
    pin_lo();
    t += unit_cyc;
    wait_until(t);
    pin_hi();
    /* short active push to speed up rising edge, then release */
    t += CPU_MHZ / 4;
    wait_until(t);
    pin_in();

    uint32_t start = cyc();
    const uint32_t first_to = 400u * CPU_MHZ; /* 400 us for the slave to begin */
    const uint32_t edge_to = 200u * CPU_MHZ; /* max 200 us between edges */
    /* The released request pulse can still read low briefly. Wait for its
     * rising edge so that it is not decoded as an extra leading zero bit. */
    while(!pin_rd()) {
        if(cyc() - start > first_to) goto done;
    }
    uint32_t last = start;
    for(n = 0; n < 9; n++) {
        uint32_t to = (n == 0) ? first_to : edge_to;
        while(pin_rd()) {
            if(cyc() - last > to) goto done;
        }
        fall[n] = cyc();
        last = fall[n];
        while(!pin_rd()) {
            if(cyc() - last > edge_to) goto done;
        }
        rise[n] = cyc();
        last = rise[n];
    }
    ok = true;
done:
    pin_hi();
    pin_out();
    FURI_CRITICAL_EXIT();

    rx_dbg.edges = n;
    rx_dbg.valid = ok;
    for(uint8_t i = 0; i < n && i < 9; i++) {
        rx_dbg.fall[i] = fall[i] - start;
        rx_dbg.rise[i] = rise[i] - start;
    }
    if(!ok) return false;

    uint8_t v = 0;
    for(int i = 0; i < 8; i++) {
        uint32_t low = rise[i] - fall[i];
        uint32_t per = fall[i + 1] - fall[i];
        v = (uint8_t)((v << 1) | ((low * 2u > per) ? 1u : 0u));
    }
    *out = v;
    return true;
}

static void tx_header(uint32_t addr, uint8_t rw) {
    tx_byte(0x5a, true);
    tx_byte((addr >> 16) & 0xff, false);
    tx_byte((addr >> 8) & 0xff, false);
    tx_byte(addr & 0xff, false);
    tx_byte(rw, false);
}

/* --- public ------------------------------------------------------------- */

void sws_write(uint32_t addr, const uint8_t* data, size_t len) {
    bus_begin();
    tx_header(addr, 0x00);
    for(size_t i = 0; i < len; i++)
        tx_byte(data[i], false);
    tx_byte(0xff, true);
    bus_end();
}

void sws_write_u8(uint32_t addr, uint8_t v) {
    sws_write(addr, &v, 1);
}

void sws_write_fifo(uint32_t addr, const uint8_t* data, size_t len) {
    sws_write_u8(0x00b3, 0x80); /* fifo mode: address does not increment */
    sws_write(addr, data, len);
    sws_write_u8(0x00b3, 0x00);
}

size_t sws_read(uint32_t addr, uint8_t* data, size_t len) {
    size_t i;
    bus_begin();
    tx_header(addr, 0x80);
    for(i = 0; i < len; i++) {
        if(!rx_byte(&data[i])) break;
    }
    tx_byte(0xff, true);
    bus_end();
    if(len) {
        if(i == len)
            link_ok_tick = furi_get_tick();
        else
            link_fail_tick = furi_get_tick();
    }
    return i;
}

size_t sws_read_fifo(uint32_t addr, uint8_t* data, size_t len) {
    sws_write_u8(0x00b3, 0x80); /* fifo mode: address does not increment */
    size_t n = sws_read(addr, data, len);
    sws_write_u8(0x00b3, 0x00);
    return n;
}

const SwsRxDebug* sws_last_rx_debug(void) {
    return &rx_dbg;
}

void sws_link_ticks(uint32_t* last_ok, uint32_t* last_fail) {
    *last_ok = link_ok_tick;
    *last_fail = link_fail_tick;
}

/* --- TLSR825x helpers ----------------------------------------------------- */

#define REG_SPI_DATA 0x000c
#define REG_SPI_CTRL 0x000d
#define SPI_CS       0x01
#define SPI_SDO      0x02
#define SPI_RD       0x08

void sws_cpu_stop(void) {
    sws_write_u8(0x0602, 0x05);
}

void sws_cpu_run(void) {
    sws_write_u8(0x0602, 0x88);
}

static void spi_cmd_addr(uint8_t cmd, uint32_t addr) {
    /* CS high first: a CS-high write lost to a contact glitch must not turn
     * this command into the tail of the previous transfer */
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    sws_write_u8(REG_SPI_CTRL, 0x00); /* CS low */
    sws_write_u8(REG_SPI_DATA, cmd);
    sws_write_u8(REG_SPI_DATA, (addr >> 16) & 0xff);
    sws_write_u8(REG_SPI_DATA, (addr >> 8) & 0xff);
    sws_write_u8(REG_SPI_DATA, addr & 0xff);
}

static void spi_auto_read(void) {
    /* [0x0c]=0 launches first read, [0x0d]=RD|SDO enables auto read */
    const uint8_t v[2] = {0x00, SPI_RD | SPI_SDO};
    sws_write(REG_SPI_DATA, v, 2);
}

size_t sws_flash_read(uint32_t faddr, uint8_t* buf, size_t len) {
    spi_cmd_addr(0x03, faddr);
    spi_auto_read();
    size_t n = sws_read_fifo(REG_SPI_DATA, buf, len);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    return n;
}

bool sws_flash_jedec(uint8_t id[3]) {
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    sws_write_u8(REG_SPI_CTRL, 0x00);
    sws_write_u8(REG_SPI_DATA, 0x9f);
    spi_auto_read();
    size_t n = sws_read_fifo(REG_SPI_DATA, id, 3);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    return n == 3;
}

/* --- flash write/erase (same SPI master flow, CPU halted) --------------- */

/* read the flash status register (cmd 0x05) */
bool sws_flash_read_status(uint8_t* st) {
    sws_write_u8(REG_SPI_CTRL, 0x00);
    sws_write_u8(REG_SPI_DATA, 0x05);
    spi_auto_read();
    size_t n = sws_read_fifo(REG_SPI_DATA, st, 1);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    return n == 1;
}

bool sws_flash_read_status2(uint8_t* st) {
    sws_write_u8(REG_SPI_CTRL, 0x00);
    sws_write_u8(REG_SPI_DATA, 0x35);
    spi_auto_read();
    size_t n = sws_read_fifo(REG_SPI_DATA, st, 1);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    return n == 1;
}

bool sws_flash_busy(void) {
    uint8_t st = 0xff;
    return !sws_flash_read_status(&st) || (st & 0x01); /* WIP */
}

/* write enable (0x06) */
void sws_flash_write_enable(void) {
    sws_write_u8(REG_SPI_CTRL, 0x00); /* CS low */
    sws_write_u8(REG_SPI_DATA, 0x06);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
}

/* write status register (0x01): clears/sets the block protect bits */
bool sws_flash_write_status(const uint8_t* sr, size_t n) {
    sws_flash_write_enable();
    sws_write_u8(REG_SPI_CTRL, 0x00); /* CS low */
    sws_write_u8(REG_SPI_DATA, 0x01);
    for(size_t i = 0; i < n; i++)
        sws_write_u8(REG_SPI_DATA, sr[i]);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    return sws_flash_wait_ready(200);
}

/* poll the status register until not busy (or timeout in ms) */
bool sws_flash_wait_ready(uint32_t timeout_ms) {
    uint32_t t0 = furi_get_tick();
    /* a first short wait avoids a lot of useless polling */
    furi_delay_us(300);
    while(furi_get_tick() - t0 < timeout_ms) {
        if(!sws_flash_busy()) return true;
    }
    return false;
}

/* 4 KB sector erase (0x20) at a sector aligned address */
bool sws_flash_erase_sector(uint32_t faddr) {
    sws_flash_write_enable();
    spi_cmd_addr(0x20, faddr);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    return sws_flash_wait_ready(5000);
}

/* page program (0x02). len <= 256 and must not cross a 256 B page. */
bool sws_flash_write_page(uint32_t faddr, const uint8_t* data, size_t len) {
    if(len == 0 || len > 256 || (faddr & 0xff) + len > 256) return false;
    sws_flash_write_enable();
    spi_cmd_addr(0x02, faddr);
    if(len == 1)
        sws_write(REG_SPI_DATA, data, 1);
    else
        sws_write_fifo(REG_SPI_DATA, data, len);
    sws_write_u8(REG_SPI_CTRL, SPI_CS);
    return sws_flash_wait_ready(100);
}
