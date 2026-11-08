// UART-мост: USB CDC-ACM ↔ UART1.
//
// UART → USB: приём по прерыванию в кольцевой буфер, чтобы не терять байты,
//             пока основной цикл занят длинным JTAG-сдвигом.
// USB → UART: забираем из FIFO TinyUSB, только пока в UART есть место
//             (USB сам притормозит хост — данные не теряются).
// Скорость и формат берутся из настроек порта на ПК (stty, minicom, picocom).

#include "uart_bridge.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define RING_SIZE 4096  // степень двойки

static uint8_t           ring[RING_SIZE];
static volatile uint32_t ring_head;  // пишет прерывание
static volatile uint32_t ring_tail;  // читает основной цикл
static volatile uint32_t ring_overflow;

static void on_uart_rx(void) {
    while (uart_is_readable(BRIDGE_UART_ID)) {
        uint8_t c = (uint8_t)uart_get_hw(BRIDGE_UART_ID)->dr;
        uint32_t next = (ring_head + 1) & (RING_SIZE - 1);
        if (next == ring_tail) {
            ring_overflow++;
            continue;
        }
        ring[ring_head] = c;
        ring_head = next;
    }
}

void uart_bridge_init(void) {
    uart_init(BRIDGE_UART_ID, BRIDGE_DEFAULT_BAUD);
    gpio_set_function(BRIDGE_PIN_TX, GPIO_FUNC_UART);
    gpio_set_function(BRIDGE_PIN_RX, GPIO_FUNC_UART);
    gpio_pull_up(BRIDGE_PIN_RX);  // линия не висит в воздухе, если плата выключена
    uart_set_hw_flow(BRIDGE_UART_ID, false, false);
    uart_set_fifo_enabled(BRIDGE_UART_ID, true);

    int irq = (BRIDGE_UART_ID == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, on_uart_rx);
    irq_set_enabled(irq, true);
    uart_set_irq_enables(BRIDGE_UART_ID, true, false);
}

void uart_bridge_task(void) {
    // UART → USB
    if (tud_ready()) {
        uint32_t head = ring_head;
        uint32_t tail = ring_tail;
        bool wrote = false;
        while (tail != head) {
            uint32_t chunk = (head > tail ? head : RING_SIZE) - tail;
            uint32_t space = tud_cdc_write_available();
            if (space == 0) break;
            if (chunk > space) chunk = space;
            uint32_t n = tud_cdc_write(&ring[tail], chunk);
            tail = (tail + n) & (RING_SIZE - 1);
            wrote = true;
            if (n < chunk) break;
        }
        ring_tail = tail;
        if (wrote) tud_cdc_write_flush();
    }

    // USB → UART
    while (tud_cdc_available() && uart_is_writable(BRIDGE_UART_ID)) {
        uint8_t c;
        if (tud_cdc_read(&c, 1) != 1) break;
        uart_get_hw(BRIDGE_UART_ID)->dr = c;
    }
}

// Хост поменял скорость/формат порта
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    (void)itf;
    if (coding->bit_rate == 0) return;

    // Как в Arduino: 1200 бод → перезагрузка в BOOTSEL (stty -F /dev/ttyACM0 1200)
    if (coding->bit_rate == 1200) {
        reset_usb_boot(0, 0);
    }

    uint data_bits = coding->data_bits;
    if (data_bits < 5 || data_bits > 8) data_bits = 8;
    uint stop_bits = (coding->stop_bits == 2) ? 2 : 1;  // 1.5 стоп-бита RP2040 не умеет
    uart_parity_t parity = UART_PARITY_NONE;
    if (coding->parity == 1) parity = UART_PARITY_ODD;
    else if (coding->parity == 2) parity = UART_PARITY_EVEN;

    uart_set_baudrate(BRIDGE_UART_ID, coding->bit_rate);
    uart_set_format(BRIDGE_UART_ID, data_bits, stop_bits, parity);
}
