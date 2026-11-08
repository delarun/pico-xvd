// Pico XVC: JTAG-программатор Xilinx Virtual Cable через USB-сеть (CDC-NCM).
//
// Pico: 192.168.7.1, хост получает адрес по DHCP (192.168.7.2..4).
// Vivado: open_hw_target -xvc_url 192.168.7.1:2542
// UART-мост: /dev/ttyACM*, UART1 (GP4 TX, GP5 RX)
//
// Структура взята из примера TinyUSB net_lwip_webserver (MIT, Peter Lawrence).

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

#include "dhserver.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"

#include "jtag.h"
#include "uart_bridge.h"
#include "xvc_server.h"

#define INIT_IP4(a, b, c, d) { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }

static struct netif netif_data;
static struct pbuf *received_frame;

// MAC хост-стороны (используется драйвером NCM и дескриптором). Генерируется из ID платы.
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

static const ip4_addr_t ipaddr  = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t netmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t gateway = INIT_IP4(0, 0, 0, 0);

static dhcp_entry_t entries[] = {
    {{0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60},
};

static const dhcp_config_t dhcp_config = {
    .router  = INIT_IP4(0, 0, 0, 0),   // без шлюза: не перехватываем маршрут по умолчанию
    .port    = 67,
    .dns     = INIT_IP4(0, 0, 0, 0),
    .domain  = "xvc",
    .num_entry = TU_ARRAY_SIZE(entries),
    .entries = entries,
};

static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
    (void)netif;
    for (;;) {
        if (!tud_ready()) return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        tud_task();
    }
}

static err_t netif_init_cb(struct netif *netif) {
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->name[0] = 'x';
    netif->name[1] = 'v';
    netif->linkoutput = linkoutput_fn;
    netif->output = etharp_output;
    return ERR_OK;
}

static void init_mac(void) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    // 02:xx:xx:xx:xx:x0 — локально администрируемый адрес, младший бит — хост/Pico
    tud_network_mac_address[0] = 0x02;
    for (int i = 1; i < 6; i++) tud_network_mac_address[i] = id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 6 + i];
    tud_network_mac_address[5] &= 0xFE;
}

static void init_lwip(void) {
    struct netif *netif = &netif_data;
    lwip_init();
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    netif->hwaddr[5] ^= 0x01;  // MAC Pico отличается от MAC хоста
    netif_add(netif, &ipaddr, &netmask, &gateway, NULL, netif_init_cb, ethernet_input);
    netif_set_default(netif);
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (received_frame) return false;
    if (size) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) {
            pbuf_take(p, src, size);
            received_frame = p;
        }
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    (void)arg;
    struct pbuf *p = (struct pbuf *)ref;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void) {
    if (received_frame) {
        pbuf_free(received_frame);
        received_frame = NULL;
    }
}

static void service_traffic(void) {
    if (received_frame) {
        if (netif_data.input(received_frame, &netif_data) != ERR_OK) pbuf_free(received_frame);
        received_frame = NULL;
        tud_network_recv_renew();
    }
    sys_check_timeouts();
}

// lwIP NO_SYS: источник времени
uint32_t sys_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

int main(void) {
    stdio_init_all();
    printf("\npico-xvc: старт, TDI=GP%d TMS=GP%d TCK=GP%d TDO=GP%d, UART TX=GP%d RX=GP%d\n",
           JTAG_PIN_TDI, JTAG_PIN_TMS, JTAG_PIN_TCK, JTAG_PIN_TDO, BRIDGE_PIN_TX, BRIDGE_PIN_RX);

    jtag_init();
    uart_bridge_init();
    init_mac();
    tud_init(BOARD_TUD_RHPORT);

    init_lwip();
    while (dhserv_init(&dhcp_config) != ERR_OK) tud_task();
    xvc_server_init();

    while (1) {
        tud_task();
        service_traffic();
        uart_bridge_task();
    }
}
