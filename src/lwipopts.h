#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// Без ОС, только raw API
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_RAW                        0

#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (24 * 1024)
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_TCP_PCB                4
#define MEMP_NUM_TCP_SEG                32
#define PBUF_POOL_SIZE                  16

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DHCP                       0
#define LWIP_SINGLE_NETIF               1
#define ETH_PAD_SIZE                    0
#define ETHARP_SUPPORT_STATIC_ENTRIES   1

// DHCP-сервер должен получать широковещательные запросы до назначения адреса хосту
#define LWIP_IP_ACCEPT_UDP_PORT(p)      ((p) == PP_NTOHS(67))

#define TCP_MSS                         (1500 - 20 - 20)
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (6 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define LWIP_TCP_KEEPALIVE              1

#define LWIP_STATS                      0
#define LWIP_NETIF_HOSTNAME             0
#define LWIP_CHECKSUM_CTRL_PER_NETIF    0

#endif
