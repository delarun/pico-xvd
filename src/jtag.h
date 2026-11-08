#pragma once
#include <stdint.h>

// Пины по умолчанию (TDI и TMS обязаны идти подряд: TMS = TDI + 1)
#ifndef JTAG_PIN_TDI
#define JTAG_PIN_TDI 12
#endif
#define JTAG_PIN_TMS (JTAG_PIN_TDI + 1)
#ifndef JTAG_PIN_TCK
#define JTAG_PIN_TCK 14
#endif
#ifndef JTAG_PIN_TDO
#define JTAG_PIN_TDO 15
#endif

// Минимальный период TCK, нс (ограничение сверху на частоту для проводов-«дюпонтов»)
#ifndef JTAG_MIN_PERIOD_NS
#define JTAG_MIN_PERIOD_NS 100  // 10 МГц: на 80 нс (S9, провода ~15 см) TDO читается на такт раньше
#endif

void     jtag_init(void);
uint32_t jtag_set_period(uint32_t period_ns);  // возвращает реально установленный период
void     jtag_shift(uint32_t nbits, const uint8_t *tms, const uint8_t *tdi, uint8_t *tdo);
