#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// Lean lwIP config for the WiFi (W) images. Bare-metal, **poll mode**: we call
// cyw43_arch_poll() from the command loop, so lwIP is never serviced from an IRQ
// (the reentrancy that causes threadsafe_background lockups — see docs/epics/E7).
// Scope for E7: IPv4 + DHCP + UDP + IGMP multicast. No TCP/IPv6/DNS yet.

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define SYS_LIGHTWEIGHT_PROT        0

// Poll mode is single-threaded, so libc malloc is safe and simpler than lwIP's heap.
#define MEM_LIBC_MALLOC             1
#define MEMP_MEM_MALLOC             1
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)

#define MEMP_NUM_UDP_PCB            8
#define PBUF_POOL_SIZE              24
#define PBUF_POOL_BUFSIZE           1600

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ICMP                   1
#define LWIP_RAW                    0

#define LWIP_DHCP                   1
#define LWIP_DNS                    0
#define LWIP_UDP                    1
#define LWIP_TCP                    0

#define LWIP_IGMP                   1   // IPv4 multicast (S7.3)
#define LWIP_NETIF_MULTICAST        1

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define LWIP_CHKSUM_ALGORITHM       3

// Lean: no stats.
#define LWIP_STATS                  0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

#endif /* LWIPOPTS_H */
