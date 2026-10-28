#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* Local (Windows) sockets used by the proxy. Kept in a separate translation
 * unit because lwIP's sockets.h and winsock2.h define conflicting types. */
intptr_t local_tcp_connect(int port);
int local_tcp_send_some(intptr_t handle, const void *data, int len);
int local_tcp_recv(intptr_t handle, void *data, int len);
void local_tcp_shutdown_send(intptr_t handle);
void local_tcp_close(intptr_t handle);

intptr_t local_udp_bind(int port);
int local_udp_recv(intptr_t handle, void *data, int len, uint32_t *peer_ip,
                   uint16_t *peer_port);
int local_udp_send(intptr_t handle, const void *data, int len, uint32_t peer_ip,
                   uint16_t peer_port);
void local_udp_close(intptr_t handle);

#ifdef __cplusplus
}
#endif
