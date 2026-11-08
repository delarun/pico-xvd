#include "jtag.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/platform.h"

#include "jtag.pio.h"

// Циклов PIO на один бит (2 низких + 3 высоких; на границе слова чуть больше)
#define CYCLES_PER_BIT 7

static PIO      jtag_pio = pio0;
static uint     jtag_sm;
static uint16_t spread8[256];   // бит k → бит 2k

void jtag_init(void) {
    for (int i = 0; i < 256; i++) {
        uint16_t v = 0;
        for (int b = 0; b < 8; b++)
            if (i & (1 << b)) v |= 1u << (2 * b);
        spread8[i] = v;
    }

    jtag_sm = pio_claim_unused_sm(jtag_pio, true);
    uint offset = pio_add_program(jtag_pio, &xvc_jtag_program);

    pio_sm_config c = xvc_jtag_program_get_default_config(offset);
    sm_config_set_out_pins(&c, JTAG_PIN_TDI, 2);
    sm_config_set_sideset_pins(&c, JTAG_PIN_TCK);
    sm_config_set_in_pins(&c, JTAG_PIN_TDO);
    sm_config_set_out_shift(&c, true, false, 32);  // вправо, без autopull
    sm_config_set_in_shift(&c, true, true, 32);    // вправо, autopush по 32

    pio_gpio_init(jtag_pio, JTAG_PIN_TDI);
    pio_gpio_init(jtag_pio, JTAG_PIN_TMS);
    pio_gpio_init(jtag_pio, JTAG_PIN_TCK);
    pio_gpio_init(jtag_pio, JTAG_PIN_TDO);
    gpio_pull_up(JTAG_PIN_TDO);

    // Исходное состояние: TCK=0, TMS=1, TDI=0
    uint32_t outmask = (1u << JTAG_PIN_TDI) | (1u << JTAG_PIN_TMS) | (1u << JTAG_PIN_TCK);
    pio_sm_set_pins_with_mask(jtag_pio, jtag_sm, 1u << JTAG_PIN_TMS, outmask);
    pio_sm_set_pindirs_with_mask(jtag_pio, jtag_sm, outmask, outmask | (1u << JTAG_PIN_TDO));

    pio_sm_init(jtag_pio, jtag_sm, offset, &c);
    jtag_set_period(1000);  // 1 МГц до первой команды settck
    pio_sm_set_enabled(jtag_pio, jtag_sm, true);
}

uint32_t jtag_set_period(uint32_t period_ns) {
    if (period_ns < JTAG_MIN_PERIOD_NS) period_ns = JTAG_MIN_PERIOD_NS;

    uint32_t sys_hz = clock_get_hz(clk_sys);
    // div = period * sys_hz / (CYCLES_PER_BIT * 1e9), в формате 16.8
    uint64_t div256 = ((uint64_t)period_ns * sys_hz * 256u + (uint64_t)CYCLES_PER_BIT * 500000000u)
                      / ((uint64_t)CYCLES_PER_BIT * 1000000000u);
    if (div256 < 256) div256 = 256;
    if (div256 > 0xFFFFFFu) div256 = 0xFFFFFFu;

    pio_sm_set_clkdiv_int_frac(jtag_pio, jtag_sm, (uint16_t)(div256 >> 8), (uint8_t)(div256 & 0xFF));
    pio_sm_clkdiv_restart(jtag_pio, jtag_sm);

    return (uint32_t)((div256 * CYCLES_PER_BIT * 1000000000u) / ((uint64_t)sys_hz * 256u));
}

static inline uint32_t make_word(const uint8_t *tms, const uint8_t *tdi, uint32_t nbytes, uint32_t w) {
    uint32_t i0 = 2 * w, i1 = 2 * w + 1;
    uint32_t lo = spread8[tdi[i0]] | ((uint32_t)spread8[tms[i0]] << 1);
    uint32_t hi = 0;
    if (i1 < nbytes)
        hi = spread8[tdi[i1]] | ((uint32_t)spread8[tms[i1]] << 1);
    return lo | (hi << 16);
}

static inline void store_word(uint8_t *tdo, uint32_t nbytes, uint32_t w, uint32_t v) {
    uint32_t off = 4 * w;
    for (int b = 0; b < 4 && off < nbytes; b++, off++)
        tdo[off] = (uint8_t)(v >> (8 * b));
}

void __time_critical_func(jtag_shift)(uint32_t nbits, const uint8_t *tms, const uint8_t *tdi, uint8_t *tdo) {
    if (nbits == 0) return;

    const uint32_t nbytes = (nbits + 7) / 8;
    const uint32_t n_tx   = (nbits + 15) / 16;
    const uint32_t n_rx   = nbits / 32 + 1;
    const uint32_t rem    = nbits % 32;

    pio_sm_put_blocking(jtag_pio, jtag_sm, nbits - 1);

    uint32_t tx = 0, rx = 0;
    while (rx < n_rx) {
        if (tx < n_tx && !pio_sm_is_tx_fifo_full(jtag_pio, jtag_sm)) {
            pio_sm_put(jtag_pio, jtag_sm, make_word(tms, tdi, nbytes, tx));
            tx++;
        }
        if (!pio_sm_is_rx_fifo_empty(jtag_pio, jtag_sm)) {
            uint32_t v = pio_sm_get(jtag_pio, jtag_sm);
            if (rx < n_rx - 1) {
                store_word(tdo, nbytes, rx, v);
            } else if (rem) {
                store_word(tdo, nbytes, rx, v >> (32 - rem));
            }
            rx++;
        }
    }
}
