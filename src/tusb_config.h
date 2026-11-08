#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "lwipopts.h"

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define BOARD_TUD_RHPORT          0
#define CFG_TUSB_RHPORT0_MODE     OPT_MODE_DEVICE
#define CFG_TUD_ENABLED           1
#define CFG_TUD_MAX_SPEED         OPT_MODE_FULL_SPEED

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS               OPT_OS_PICO
#endif
#define CFG_TUSB_DEBUG            0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE    64

// CDC-NCM: сеть для XVC (Linux: драйвер cdc_ncm, Windows 11 / macOS — встроенные)
#define CFG_TUD_ECM_RNDIS         0
#define CFG_TUD_NCM               1
#define CFG_TUD_NCM_IN_NTB_MAX_SIZE  (2 * TCP_MSS + 100)
#define CFG_TUD_NCM_OUT_NTB_MAX_SIZE (2 * TCP_MSS + 100)
#define CFG_TUD_NCM_OUT_NTB_N     1
#define CFG_TUD_NCM_IN_NTB_N      1

// CDC-ACM: UART-мост к консоли целевой платы
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    1024
#define CFG_TUD_CDC_TX_BUFSIZE    4096  // пока порт закрыт — хранит последние 4 КБ вывода
#define CFG_TUD_CDC_EP_BUFSIZE    64

#endif
