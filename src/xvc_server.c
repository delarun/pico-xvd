// XVC 1.0 (Xilinx Virtual Cable) поверх lwIP raw API, один клиент.
//
//   getinfo:                       → "xvcServer_v1.0:<max>\n"
//   settck:<u32 period_ns>         → <u32 period_ns>
//   shift:<u32 nbits><tms[n]><tdi[n]> → <tdo[n]>,  n = ceil(nbits/8)

#include "xvc_server.h"

#include <stdio.h>
#include <string.h>

#include "jtag.h"
#include "lwip/tcp.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#define RX_SIZE (10 + 2 * XVC_MAX_VECTOR + 16)
#define TX_SIZE (XVC_MAX_VECTOR)
#define PARSE_INCOMPLETE 0
#define PARSE_ERROR      ((size_t)-1)

typedef struct {
    struct tcp_pcb *pcb;
    uint8_t  rx[RX_SIZE];
    size_t   rx_len;
    uint8_t  tx[TX_SIZE];
    size_t   tx_len, tx_off;
} xvc_conn_t;

static xvc_conn_t conn;

static inline uint32_t get_u32le(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void conn_reset(void) {
    conn.pcb = NULL;
    conn.rx_len = conn.tx_len = conn.tx_off = 0;
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

// Закрыть соединение. Возвращает ERR_ABRT, если пришлось делать tcp_abort.
static err_t conn_close(void) {
    struct tcp_pcb *pcb = conn.pcb;
    err_t ret = ERR_OK;
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_err(pcb, NULL);
        if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
            ret = ERR_ABRT;
        }
    }
    conn_reset();
    printf("xvc: соединение закрыто\n");
    return ret;
}

// Разобрать одну команду из conn.rx. Ответ кладётся в conn.tx.
static size_t parse_one(void) {
    const uint8_t *rx = conn.rx;
    size_t len = conn.rx_len;
    if (len < 2) return PARSE_INCOMPLETE;

    if (rx[0] == 'g' && rx[1] == 'e') {
        if (len < 8) return PARSE_INCOMPLETE;
        if (memcmp(rx, "getinfo:", 8)) return PARSE_ERROR;
        conn.tx_len = (size_t)snprintf((char *)conn.tx, TX_SIZE, "xvcServer_v1.0:%u\n", XVC_MAX_VECTOR);
        return 8;
    }

    if (rx[0] == 's' && rx[1] == 'e') {
        if (len < 11) return PARSE_INCOMPLETE;
        if (memcmp(rx, "settck:", 7)) return PARSE_ERROR;
        uint32_t req = get_u32le(rx + 7);
        uint32_t act = jtag_set_period(req);
        printf("xvc: settck %lu нс → %lu нс\n", (unsigned long)req, (unsigned long)act);
        put_u32le(conn.tx, act);
        conn.tx_len = 4;
        return 11;
    }

    if (rx[0] == 's' && rx[1] == 'h') {
        if (len < 10) return PARSE_INCOMPLETE;
        if (memcmp(rx, "shift:", 6)) return PARSE_ERROR;
        uint32_t nbits  = get_u32le(rx + 6);
        uint32_t nbytes = (nbits + 7) / 8;
        if (nbytes > XVC_MAX_VECTOR) return PARSE_ERROR;
        size_t total = 10 + 2 * (size_t)nbytes;
        if (len < total) return PARSE_INCOMPLETE;
        jtag_shift(nbits, rx + 10, rx + 10 + nbytes, conn.tx);
        conn.tx_len = nbytes;
        return total;
    }

    // Расширения pico-xvc (Vivado их не шлёт):
    //   dbgpins: → u32 — уровни на всех GPIO (gpio_get_all)
    //   bootsel: → перезагрузка в BOOTSEL для прошивки через picotool
    if (rx[0] == 'd' && rx[1] == 'b') {
        if (len < 8) return PARSE_INCOMPLETE;
        if (memcmp(rx, "dbgpins:", 8)) return PARSE_ERROR;
        put_u32le(conn.tx, gpio_get_all());
        conn.tx_len = 4;
        return 8;
    }

    if (rx[0] == 'b' && rx[1] == 'o') {
        if (len < 8) return PARSE_INCOMPLETE;
        if (memcmp(rx, "bootsel:", 8)) return PARSE_ERROR;
        printf("xvc: перезагрузка в BOOTSEL\n");
        sleep_ms(10);
        reset_usb_boot(0, 0);
    }

    return PARSE_ERROR;
}

// Отправить, сколько влезет в буфер TCP
static err_t send_pending(void) {
    while (conn.tx_off < conn.tx_len) {
        u16_t space = tcp_sndbuf(conn.pcb);
        if (space == 0) break;
        size_t n = conn.tx_len - conn.tx_off;
        if (n > space) n = space;
        err_t e = tcp_write(conn.pcb, conn.tx + conn.tx_off, (u16_t)n, TCP_WRITE_FLAG_COPY);
        if (e == ERR_MEM) break;
        if (e != ERR_OK) return e;
        conn.tx_off += n;
    }
    return tcp_output(conn.pcb);
}

// Обработать накопленные команды, пока не упрёмся в неотправленный ответ
static err_t process(void) {
    while (conn.pcb && conn.tx_off == conn.tx_len) {
        conn.tx_len = conn.tx_off = 0;
        size_t used = parse_one();
        if (used == PARSE_INCOMPLETE) break;
        if (used == PARSE_ERROR) {
            printf("xvc: неизвестная команда\n");
            return conn_close();
        }
        conn.rx_len -= used;
        memmove(conn.rx, conn.rx + used, conn.rx_len);
        if (send_pending() != ERR_OK) return conn_close();
    }
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;
    if (!p || err != ERR_OK) {
        if (p) pbuf_free(p);
        return conn_close();
    }
    if (conn.rx_len + p->tot_len > RX_SIZE) {
        return ERR_MEM;  // lwIP повторит доставку позже
    }
    pbuf_copy_partial(p, conn.rx + conn.rx_len, p->tot_len, 0);
    conn.rx_len += p->tot_len;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return process();
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg; (void)pcb; (void)len;
    if (send_pending() != ERR_OK) return conn_close();
    return process();
}

static void on_err(void *arg, err_t err) {
    (void)arg;
    printf("xvc: ошибка соединения %d\n", err);
    conn_reset();  // pcb уже освобождён lwIP
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    if (conn.pcb) {
        printf("xvc: уже есть клиент, отклоняю\n");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    conn_reset();
    conn.pcb = newpcb;
    tcp_nagle_disable(newpcb);
    tcp_arg(newpcb, NULL);
    tcp_recv(newpcb, on_recv);
    tcp_sent(newpcb, on_sent);
    tcp_err(newpcb, on_err);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    printf("xvc: клиент подключён\n");
    return ERR_OK;
}

void xvc_server_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    conn_reset();

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb || tcp_bind(pcb, IP_ADDR_ANY, XVC_PORT) != ERR_OK) {
        panic("xvc: не удалось занять порт %d", XVC_PORT);
    }
    pcb = tcp_listen_with_backlog(pcb, 1);
    tcp_accept(pcb, on_accept);
    printf("xvc: слушаю порт %d, вектор до %d байт\n", XVC_PORT, XVC_MAX_VECTOR);
}
