#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* User-space IPv6 stack for the AWDL data plane. No OS sockets, no threads:
 * awdl_netd owns one event loop, feeds raw IPv6 packets from awdld, and
 * proxies AirDrop's TCP 8771 to the local receiver. */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_TIMERS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_NETIF_API 0

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
#define LWIP_NUM_NETIF_CLIENT_DATA 1
#define LWIP_SINGLE_NETIF 1

#define MEM_SIZE (128 * 1024)
#define MEMP_NUM_PBUF 32
#define MEMP_NUM_SYS_TIMEOUT 128
#define MEMP_NUM_TCP_PCB 8
#define MEMP_NUM_TCP_SEG 64
#define PBUF_POOL_SIZE 64
#define PBUF_POOL_BUFSIZE 1600
#define TCP_MSS 1440
#define TCP_WND (32 * 1024)
#define TCP_SND_BUF (32 * 1024)
#define TCP_SND_QUEUELEN 64

#define LWIP_ND6_NUM_NEIGHBORS 8
#define LWIP_ND6_NUM_DESTINATIONS 8
#define LWIP_ND6_QUEUEING 1
#define LWIP_IPV6_NUM_ADDRESSES 3
#define LWIP_IPV6_SCOPES 1

/* lwIP's mDNS responder supports IPv6 when MLD is on. */
#define LWIP_MDNS_RESPONDER 1
#define LWIP_MDNS_MAX_SERVICES 4
#define LWIP_MDNS_TXT_MAX_ITEMS 8
#define LWIP_MDNS_MAX_SERVICE_TXT 128

#endif /* LWIPOPTS_H */
