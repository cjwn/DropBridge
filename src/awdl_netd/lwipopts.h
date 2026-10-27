#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* User-space IPv6 stack for the AWDL data plane. The socket API mirrors the
 * blocking Windows sockets used by receiver.py, but all packets come from
 * awdld over IPC, never from the Windows IPv6 stack. */
#define NO_SYS 0
#define LWIP_TIMERS 1
#define LWIP_SOCKET 1
#define LWIP_NETCONN 1
#define LWIP_NETIF_API 1
#define LWIP_SO_RCVTIMEO 1
#define MEMP_NUM_NETCONN 8
#define TCPIP_THREAD_STACKSIZE 65536
#define DEFAULT_THREAD_STACKSIZE 65536
#define TCPIP_THREAD_PRIO 3

#define LWIP_IPV4 0
#define LWIP_IPV6 1
#define LWIP_IPV6_ND 1
#define LWIP_IPV6_MLD 1
#define LWIP_IPV6_FRAG 0
#define IPV6_FRAG_COPYHEADER 1
#define LWIP_ICMP6 1

#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_RAW 0
#define LWIP_DNS 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_ARP 0
#define LWIP_ETHERNET 0

#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_REMOVE_CALLBACK 1
#define LWIP_NUM_NETIF_CLIENT_DATA 0
#define LWIP_SINGLE_NETIF 1

#define MEM_SIZE (512 * 1024)
#define MEMP_NUM_PBUF 64
#define MEMP_NUM_SYS_TIMEOUT 128
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_SEG 128
#define PBUF_POOL_SIZE 256
#define PBUF_POOL_BUFSIZE 1600
#define TCP_MSS 1360
#define LWIP_WND_SCALE 1
#define TCP_RCV_SCALE 2
#define TCP_WND (240 * 1024)
#define TCP_SND_BUF (32 * 1024)
#define TCP_SND_QUEUELEN 64

#define LWIP_ND6_NUM_NEIGHBORS 8
#define LWIP_ND6_NUM_DESTINATIONS 8
#define LWIP_ND6_QUEUEING 1
#define LWIP_IPV6_NUM_ADDRESSES 3
#define LWIP_IPV6_SCOPES 1

#endif /* LWIPOPTS_H */
