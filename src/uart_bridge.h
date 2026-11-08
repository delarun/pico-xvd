#pragma once

// UART-мост USB CDC-ACM ↔ UART1 (консоль S9)
#ifndef BRIDGE_UART_ID
#define BRIDGE_UART_ID   uart1
#endif
#ifndef BRIDGE_PIN_TX
#define BRIDGE_PIN_TX    4    // Pico TX → RX целевой платы
#endif
#ifndef BRIDGE_PIN_RX
#define BRIDGE_PIN_RX    5    // Pico RX ← TX целевой платы
#endif
#define BRIDGE_DEFAULT_BAUD 115200

void uart_bridge_init(void);
void uart_bridge_task(void);   // вызывать в основном цикле
