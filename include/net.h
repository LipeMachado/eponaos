#ifndef EPONA_NET_H
#define EPONA_NET_H

#include <stdint.h>

#define NET_IP(a, b, c, d) ((uint32_t) (a) | ((uint32_t) (b) << 8) | ((uint32_t) (c) << 16) | ((uint32_t) (d) << 24))

void net_init(void);
void net_poll(void);
void net_handle_packet(const void *data, int len);
void dns_query(const char *host);
int net_is_configured(void);
int net_http_ok(void);
uint32_t net_local_ip(void);
uint32_t net_dns_answer_ip(void);
const char *net_http_body_preview(void);
uint32_t net_http_body_len(void);
void net_http_reset(void);
int net_http_fetch(const char *host);
int net_tcp_busy(void);
void net_ping_send(uint32_t dst_ip);
int net_ping_replied(void);
void net_poll_wait(int ticks);

#endif
