#ifndef EPONA_NET_H
#define EPONA_NET_H

#include <stdint.h>

#define NET_IP(a, b, c, d) ((uint32_t) (a) | ((uint32_t) (b) << 8) | ((uint32_t) (c) << 16) | ((uint32_t) (d) << 24))

void net_init(void);
void net_poll(void);
void net_handle_packet(const void *data, int len);

#endif
