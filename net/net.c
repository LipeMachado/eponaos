#include "net.h"
#include "rtl8139.h"
#include "serial.h"
#include "string.h"
#include <stddef.h>

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_XID 0xE900C0DE
#define DHCP_MAGIC 0x63825363

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5

#define DNS_PORT 53
#define DNS_CLIENT_PORT 49153
#define DNS_XID 0xE901
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

#define TCP_HTTP_PORT 80
#define TCP_CLIENT_PORT 49154
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_RETRY_TICKS 200000
#define TCP_MAX_RETRIES 3
#define HTTP_BODY_BUF_SIZE 512

typedef struct {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t ttl;
    uint8_t proto;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed)) ipv4_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed)) udp_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed)) icmp_hdr_t;

typedef struct {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
} __attribute__((packed)) dhcp_pkt_t;

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_hdr_t;

static uint8_t g_mac[ETH_ALEN];
static uint8_t g_gateway_mac[ETH_ALEN];
static int g_gateway_known = 0;
static int g_dhcp_configured = 0;
static int g_dhcp_requested = 0;
static int g_dns_requested = 0;
static int g_tcp_state = 0;
static int g_http_ok = 0;
static uint16_t g_ip_id = 1;
static uint16_t g_icmp_seq = 1;
static char g_dns_host[64];

static uint32_t g_local_ip = 0;
static uint32_t g_gateway_ip = 0;
static uint32_t g_dns_ip = 0;
static uint32_t g_dns_answer_ip = 0;
static uint32_t g_dhcp_server_ip = 0;
static uint32_t g_offered_ip = 0;
static uint32_t g_tcp_remote_ip = 0;
static uint32_t g_tcp_seq = 0xE0020000;
static uint32_t g_tcp_ack = 0;
static uint32_t g_tcp_get_seq = 0;
static uint32_t g_tcp_get_len = 0;
static uint32_t g_tcp_wait_ticks = 0;
static int g_tcp_retries = 0;
static uint32_t g_http_bytes = 0;
static uint32_t g_http_chunks = 0;
static int g_http_headers_done = 0;
static int g_http_body_printed = 0;
static char g_http_body[HTTP_BODY_BUF_SIZE];
static uint32_t g_http_body_len = 0;
static char g_http_host[64];
static int g_ping_replied = 0;

static uint16_t checksum16(const void *data, int len) {
    const uint8_t *p = (const uint8_t *) data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint16_t) p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint16_t) p[0] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t) ~sum;
}

static void print_ip(uint32_t ip) {
    serial_print_dec(ip & 0xFF);
    serial_print(".");
    serial_print_dec((ip >> 8) & 0xFF);
    serial_print(".");
    serial_print_dec((ip >> 16) & 0xFF);
    serial_print(".");
    serial_print_dec((ip >> 24) & 0xFF);
}

static int mac_is_broadcast(const uint8_t mac[ETH_ALEN]) {
    for (int i = 0; i < ETH_ALEN; i++)
        if (mac[i] != 0xFF) return 0;
    return 1;
}

static void build_ipv4(ipv4_hdr_t *ip, uint32_t src, uint32_t dst, uint8_t proto, int payload_len) {
    int ip_len = (int) sizeof(ipv4_hdr_t) + payload_len;
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = bswap16((uint16_t) ip_len);
    ip->id = bswap16(g_ip_id++);
    ip->flags_frag = bswap16(0x4000);
    ip->ttl = 64;
    ip->proto = proto;
    ip->checksum = 0;
    ip->src = src;
    ip->dst = dst;
    ip->checksum = bswap16(checksum16(ip, sizeof(ipv4_hdr_t)));
}

static void send_udp(const uint8_t dst_mac[ETH_ALEN], uint32_t src_ip, uint32_t dst_ip,
                     uint16_t src_port, uint16_t dst_port, const void *payload, int payload_len) {
    uint8_t pkt[640];
    memset(pkt, 0, sizeof(pkt));

    eth_hdr_t *eth = (eth_hdr_t *) pkt;
    ipv4_hdr_t *ip = (ipv4_hdr_t *) (pkt + sizeof(eth_hdr_t));
    udp_hdr_t *udp = (udp_hdr_t *) ((uint8_t *) ip + sizeof(ipv4_hdr_t));
    uint8_t *udp_payload = (uint8_t *) udp + sizeof(udp_hdr_t);
    int udp_len = (int) sizeof(udp_hdr_t) + payload_len;
    int ip_len = (int) sizeof(ipv4_hdr_t) + udp_len;

    for (int i = 0; i < ETH_ALEN; i++) {
        eth->dst[i] = dst_mac[i];
        eth->src[i] = g_mac[i];
    }
    eth->type = bswap16(ETH_P_IP);

    build_ipv4(ip, src_ip, dst_ip, IP_PROTO_UDP, udp_len);
    udp->src_port = bswap16(src_port);
    udp->dst_port = bswap16(dst_port);
    udp->len = bswap16((uint16_t) udp_len);
    udp->checksum = 0; /* UDP checksum zero is valid for IPv4. */
    memcpy(udp_payload, payload, payload_len);

    rtl8139_send(pkt, sizeof(eth_hdr_t) + ip_len);
}

static uint16_t tcp_checksum_segment(const ipv4_hdr_t *ip, const void *tcp_segment, int tcp_len) {
    uint8_t pseudo[12 + 1500];
    if (tcp_len < (int) sizeof(tcp_hdr_t) || tcp_len > 1500)
        return 0;

    memcpy(pseudo, &ip->src, 4);
    memcpy(pseudo + 4, &ip->dst, 4);
    pseudo[8] = 0;
    pseudo[9] = IP_PROTO_TCP;
    pseudo[10] = (uint8_t) (tcp_len >> 8);
    pseudo[11] = (uint8_t) tcp_len;
    memcpy(pseudo + 12, tcp_segment, tcp_len);

    return checksum16(pseudo, 12 + tcp_len);
}

static void send_tcp_segment(uint32_t dst_ip, const uint8_t dst_mac[ETH_ALEN], uint32_t seq, uint32_t ack,
                             uint8_t flags, const void *payload, int payload_len) {
    uint8_t pkt[640];
    memset(pkt, 0, sizeof(pkt));

    eth_hdr_t *eth = (eth_hdr_t *) pkt;
    ipv4_hdr_t *ip = (ipv4_hdr_t *) (pkt + sizeof(eth_hdr_t));
    tcp_hdr_t *tcp = (tcp_hdr_t *) ((uint8_t *) ip + sizeof(ipv4_hdr_t));
    uint8_t *tcp_payload = (uint8_t *) tcp + sizeof(tcp_hdr_t);
    int tcp_len = (int) sizeof(tcp_hdr_t) + payload_len;
    int ip_len = (int) sizeof(ipv4_hdr_t) + tcp_len;

    for (int i = 0; i < ETH_ALEN; i++) {
        eth->dst[i] = dst_mac[i];
        eth->src[i] = g_mac[i];
    }
    eth->type = bswap16(ETH_P_IP);

    build_ipv4(ip, g_local_ip, dst_ip, IP_PROTO_TCP, tcp_len);

    tcp->src_port = bswap16(TCP_CLIENT_PORT);
    tcp->dst_port = bswap16(TCP_HTTP_PORT);
    tcp->seq = bswap32(seq);
    tcp->ack = bswap32(ack);
    tcp->data_offset = 5 << 4;
    tcp->flags = flags;
    tcp->window = bswap16(4096);
    tcp->checksum = 0;
    tcp->urgent = 0;
    if (payload_len > 0)
        memcpy(tcp_payload, payload, payload_len);
    tcp->checksum = bswap16(tcp_checksum_segment(ip, tcp, tcp_len));

    rtl8139_send(pkt, sizeof(eth_hdr_t) + ip_len);
}

static void tcp_http_start(uint32_t dst_ip) {
    if (!g_gateway_known || g_tcp_state != 0)
        return;

    g_tcp_remote_ip = dst_ip;
    g_tcp_seq = 0xE0020000;
    g_tcp_ack = 0;
    g_tcp_get_seq = 0;
    g_tcp_get_len = 0;
    g_tcp_wait_ticks = 0;
    g_tcp_retries = 0;
    g_http_bytes = 0;
    g_http_chunks = 0;
    g_tcp_state = 1;

    serial_print("[tcp] SYN -> ");
    print_ip(dst_ip);
    serial_print(":80\n");
    send_tcp_segment(dst_ip, g_gateway_mac, g_tcp_seq, 0, TCP_FLAG_SYN, NULL, 0);
}

static void tcp_http_send_get(void) {
    char req[320];
    char *p = req;
    memcpy(p, "GET / HTTP/1.0\r\nHost: ", 22); p += 22;
    int hl = (int) strlen(g_http_host);
    if (hl > 60) hl = 60;
    memcpy(p, g_http_host, (size_t) hl); p += hl;
    memcpy(p, "\r\nUser-Agent: EponaOS\r\n\r\n", 27); p += 27;
    int len = (int)(p - req);

    serial_print("[http] GET / (");
    serial_print(g_http_host);
    serial_print(")\n");
    g_tcp_get_seq = g_tcp_seq;
    g_tcp_get_len = (uint32_t) len;
    g_tcp_wait_ticks = 0;
    g_tcp_retries = 0;
    send_tcp_segment(g_tcp_remote_ip, g_gateway_mac, g_tcp_seq, g_tcp_ack, TCP_FLAG_PSH | TCP_FLAG_ACK, req, len);
    g_tcp_seq += (uint32_t) len;
    g_tcp_state = 3;
}

static void tcp_http_retry_get(void) {
    char req[320];
    char *p = req;
    memcpy(p, "GET / HTTP/1.0\r\nHost: ", 22); p += 22;
    int hl = (int) strlen(g_http_host);
    if (hl > 60) hl = 60;
    memcpy(p, g_http_host, (size_t) hl); p += hl;
    memcpy(p, "\r\nUser-Agent: EponaOS\r\n\r\n", 27); p += 27;
    int len = (int)(p - req);

    serial_print("[http] retry GET / (");
    serial_print(g_http_host);
    serial_print(")\n");
    send_tcp_segment(g_tcp_remote_ip, g_gateway_mac, g_tcp_get_seq, g_tcp_ack, TCP_FLAG_PSH | TCP_FLAG_ACK, req, len);
}

static void http_store_body(const uint8_t *data, int len) {
    if (len <= 0 || g_http_body_len >= HTTP_BODY_BUF_SIZE - 1)
        return;

    int space = (HTTP_BODY_BUF_SIZE - 1) - (int) g_http_body_len;
    if (len > space)
        len = space;

    memcpy(g_http_body + g_http_body_len, data, len);
    g_http_body_len += (uint32_t) len;
    g_http_body[g_http_body_len] = 0;
}

static void http_process_payload(const uint8_t *payload, int payload_len) {
    if (g_http_headers_done) {
        http_store_body(payload, payload_len);
        return;
    }

    for (int i = 0; i + 3 < payload_len; i++) {
        if (payload[i] == '\r' && payload[i + 1] == '\n' && payload[i + 2] == '\r' && payload[i + 3] == '\n') {
            g_http_headers_done = 1;
            http_store_body(payload + i + 4, payload_len - i - 4);
            return;
        }
    }
}

static void http_print_body_preview(void) {
    if (g_http_body_printed || g_http_body_len == 0)
        return;

    int n = (int) g_http_body_len;
    if (n > 180)
        n = 180;

    serial_print("[http] body preview: \"");
    for (int i = 0; i < n; i++) {
        char c = g_http_body[i];
        if (c == '\r' || c == '\n' || c == '\t')
            serial_print(" ");
        else
            serial_putc(c);
    }
    serial_print("\"\n");
    g_http_body_printed = 1;
}

static void tcp_poll_retransmit(void) {
    if (g_tcp_state != 1 && g_tcp_state != 3)
        return;

    if (++g_tcp_wait_ticks < TCP_RETRY_TICKS)
        return;
    g_tcp_wait_ticks = 0;

    if (g_tcp_retries >= TCP_MAX_RETRIES) {
        serial_print("[tcp] retry limit reached\n");
        g_tcp_state = 7;
        return;
    }
    g_tcp_retries++;

    if (g_tcp_state == 1) {
        serial_print("[tcp] retry SYN -> ");
        print_ip(g_tcp_remote_ip);
        serial_print(":80\n");
        send_tcp_segment(g_tcp_remote_ip, g_gateway_mac, g_tcp_seq, 0, TCP_FLAG_SYN, NULL, 0);
    } else if (g_tcp_state == 3) {
        tcp_http_retry_get();
    }
}

static uint8_t *dhcp_add_option(uint8_t *opt, uint8_t code, uint8_t len, const void *data) {
    *opt++ = code;
    *opt++ = len;
    memcpy(opt, data, len);
    return opt + len;
}

static void send_dhcp(int msg_type, uint32_t requested_ip, uint32_t server_ip) {
    uint8_t payload[360];
    uint8_t bcast[ETH_ALEN];
    memset(payload, 0, sizeof(payload));
    for (int i = 0; i < ETH_ALEN; i++)
        bcast[i] = 0xFF;

    dhcp_pkt_t *dhcp = (dhcp_pkt_t *) payload;
    uint8_t *opt = payload + sizeof(dhcp_pkt_t);

    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = ETH_ALEN;
    dhcp->xid = bswap32(DHCP_XID);
    dhcp->flags = bswap16(0x8000);
    for (int i = 0; i < ETH_ALEN; i++)
        dhcp->chaddr[i] = g_mac[i];
    dhcp->magic = bswap32(DHCP_MAGIC);

    uint8_t type = (uint8_t) msg_type;
    opt = dhcp_add_option(opt, 53, 1, &type);
    if (requested_ip)
        opt = dhcp_add_option(opt, 50, 4, &requested_ip);
    if (server_ip)
        opt = dhcp_add_option(opt, 54, 4, &server_ip);

    uint8_t params[] = { 1, 3, 6, 15, 51, 54 };
    opt = dhcp_add_option(opt, 55, sizeof(params), params);
    uint16_t max_size = bswap16(576);
    opt = dhcp_add_option(opt, 57, 2, &max_size);
    *opt++ = 255;

    int dhcp_len = (int) (opt - payload);
    if (dhcp_len < 300)
        dhcp_len = 300;

    serial_print(msg_type == DHCP_DISCOVER ? "[dhcp] discover\n" : "[dhcp] request\n");
    send_udp(bcast, 0, NET_IP(255, 255, 255, 255), DHCP_CLIENT_PORT, DHCP_SERVER_PORT, payload, dhcp_len);
}

static void send_arp_request(uint32_t target_ip) {
    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));

    eth_hdr_t *eth = (eth_hdr_t *) pkt;
    arp_pkt_t *arp = (arp_pkt_t *) (pkt + sizeof(eth_hdr_t));

    for (int i = 0; i < ETH_ALEN; i++) {
        eth->dst[i] = 0xFF;
        eth->src[i] = g_mac[i];
    }
    eth->type = bswap16(ETH_P_ARP);

    arp->hw_type = bswap16(1);
    arp->proto = bswap16(ETH_P_IP);
    arp->hw_len = ETH_ALEN;
    arp->proto_len = 4;
    arp->op = bswap16(1);
    for (int i = 0; i < ETH_ALEN; i++) {
        arp->sha[i] = g_mac[i];
        arp->tha[i] = 0;
    }
    arp->spa = g_local_ip;
    arp->tpa = target_ip;

    serial_print("[net] ARP who-has ");
    print_ip(target_ip);
    serial_print("\n");
    rtl8139_send(pkt, 42);
}

static void send_icmp_echo(uint32_t dst_ip, const uint8_t dst_mac[ETH_ALEN]) {
    uint8_t pkt[98];
    memset(pkt, 0, sizeof(pkt));

    eth_hdr_t *eth = (eth_hdr_t *) pkt;
    ipv4_hdr_t *ip = (ipv4_hdr_t *) (pkt + sizeof(eth_hdr_t));
    icmp_hdr_t *icmp = (icmp_hdr_t *) ((uint8_t *) ip + sizeof(ipv4_hdr_t));
    uint8_t *payload = (uint8_t *) icmp + sizeof(icmp_hdr_t);
    const char *msg = "EponaOS ping";
    int payload_len = 12;
    int icmp_len = (int) sizeof(icmp_hdr_t) + payload_len;
    int ip_len = (int) sizeof(ipv4_hdr_t) + icmp_len;

    for (int i = 0; i < ETH_ALEN; i++) {
        eth->dst[i] = dst_mac[i];
        eth->src[i] = g_mac[i];
    }
    eth->type = bswap16(ETH_P_IP);

    build_ipv4(ip, g_local_ip, dst_ip, IP_PROTO_ICMP, icmp_len);

    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = bswap16(0xE001);
    icmp->seq = bswap16(g_icmp_seq++);
    memcpy(payload, msg, payload_len);
    icmp->checksum = bswap16(checksum16(icmp, icmp_len));

    serial_print("[net] ICMP echo -> ");
    print_ip(dst_ip);
    serial_print("\n");
    rtl8139_send(pkt, sizeof(eth_hdr_t) + ip_len);
}

static int dns_write_name(uint8_t *out, int out_len, const char *host) {
    int pos = 0;
    int label_len = 0;
    int label_pos = 0;

    while (*host) {
        if (label_len == 0) {
            if (pos >= out_len)
                return -1;
            label_pos = pos++;
        }

        if (*host == '.') {
            if (label_len == 0 || label_len > 63)
                return -1;
            out[label_pos] = (uint8_t) label_len;
            label_len = 0;
            host++;
            continue;
        }

        if (pos >= out_len)
            return -1;
        out[pos++] = (uint8_t) *host++;
        label_len++;
        if (label_len > 63)
            return -1;
    }

    if (label_len == 0)
        return -1;
    out[label_pos] = (uint8_t) label_len;
    if (pos >= out_len)
        return -1;
    out[pos++] = 0;
    return pos;
}

static int dns_skip_name(const uint8_t *msg, int len, int off) {
    int jumps = 0;
    while (off < len) {
        uint8_t c = msg[off++];
        if (c == 0)
            return off;
        if ((c & 0xC0) == 0xC0) {
            if (off >= len)
                return -1;
            return off + 1;
        }
        if (c & 0xC0)
            return -1;
        off += c;
        if (off > len || ++jumps > 64)
            return -1;
    }
    return -1;
}

void dns_query(const char *host) {
    uint8_t payload[256];
    memset(payload, 0, sizeof(payload));

    if (!g_dhcp_configured || !g_gateway_known || !g_dns_ip)
        return;

    dns_hdr_t *dns = (dns_hdr_t *) payload;
    dns->id = bswap16(DNS_XID);
    dns->flags = bswap16(0x0100);
    dns->qdcount = bswap16(1);

    int off = (int) sizeof(dns_hdr_t);
    int name_len = dns_write_name(payload + off, (int) sizeof(payload) - off - 4, host);
    if (name_len < 0)
        return;
    off += name_len;

    payload[off++] = 0;
    payload[off++] = DNS_TYPE_A;
    payload[off++] = 0;
    payload[off++] = DNS_CLASS_IN;

    serial_print("[dns] query ");
    serial_print(host);
    serial_print(" -> ");
    print_ip(g_dns_ip);
    serial_print("\n");

    g_dns_requested = 1;
    strncpy(g_dns_host, host, sizeof(g_dns_host) - 1);
    g_dns_host[sizeof(g_dns_host) - 1] = 0;
    send_udp(g_gateway_mac, g_local_ip, g_dns_ip, DNS_CLIENT_PORT, DNS_PORT, payload, off);
}

static void handle_dns(const udp_hdr_t *udp, int udp_payload_len) {
    const uint8_t *msg = (const uint8_t *) udp + sizeof(udp_hdr_t);
    if (udp_payload_len < (int) sizeof(dns_hdr_t))
        return;

    const dns_hdr_t *dns = (const dns_hdr_t *) msg;
    if (dns->id != bswap16(DNS_XID))
        return;

    int qdcount = bswap16(dns->qdcount);
    int ancount = bswap16(dns->ancount);
    int off = (int) sizeof(dns_hdr_t);

    for (int i = 0; i < qdcount; i++) {
        off = dns_skip_name(msg, udp_payload_len, off);
        if (off < 0 || off + 4 > udp_payload_len)
            return;
        off += 4;
    }

    for (int i = 0; i < ancount; i++) {
        off = dns_skip_name(msg, udp_payload_len, off);
        if (off < 0 || off + 10 > udp_payload_len)
            return;

        uint16_t type = ((uint16_t) msg[off] << 8) | msg[off + 1];
        uint16_t klass = ((uint16_t) msg[off + 2] << 8) | msg[off + 3];
        uint16_t rdlen = ((uint16_t) msg[off + 8] << 8) | msg[off + 9];
        off += 10;

        if (off + rdlen > udp_payload_len)
            return;

        if (type == DNS_TYPE_A && klass == DNS_CLASS_IN && rdlen == 4) {
            uint32_t ip = 0;
            memcpy(&ip, msg + off, 4);
            g_dns_answer_ip = ip;
            serial_print("[dns] answer ");
            serial_print(g_dns_host);
            serial_print(" = ");
            print_ip(ip);
            serial_print("\n");
            strncpy(g_http_host, g_dns_host, sizeof(g_http_host) - 1);
            g_http_host[sizeof(g_http_host) - 1] = 0;
            tcp_http_start(ip);
            return;
        }
        off += rdlen;
    }

    serial_print("[dns] no A record in response\n");
}

static void handle_tcp(const ipv4_hdr_t *ip, int ihl, int ip_total) {
    if (ip->dst != g_local_ip || ip->src != g_tcp_remote_ip)
        return;
    if (ip_total < ihl + (int) sizeof(tcp_hdr_t))
        return;

    const tcp_hdr_t *tcp = (const tcp_hdr_t *) ((const uint8_t *) ip + ihl);
    int tcp_len = ip_total - ihl;
    uint16_t src_port = bswap16(tcp->src_port);
    uint16_t dst_port = bswap16(tcp->dst_port);
    if (src_port != TCP_HTTP_PORT || dst_port != TCP_CLIENT_PORT)
        return;

    int tcp_hlen = (tcp->data_offset >> 4) * 4;
    if (tcp_hlen < 20 || ip_total < ihl + tcp_hlen)
        return;

    if (tcp_checksum_segment(ip, tcp, tcp_len) != 0) {
        serial_print("[tcp] drop: bad checksum\n");
        return;
    }

    uint32_t seq = bswap32(tcp->seq);
    uint32_t ack = bswap32(tcp->ack);
    int payload_len = ip_total - ihl - tcp_hlen;
    const uint8_t *payload = (const uint8_t *) tcp + tcp_hlen;

    if (tcp->flags & TCP_FLAG_RST) {
        serial_print("[tcp] RST <- connection reset\n");
        g_tcp_state = 5;
        return;
    }

    if (g_tcp_state == 1 && (tcp->flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) && ack == g_tcp_seq + 1) {
        g_tcp_seq++;
        g_tcp_ack = seq + 1;
        g_tcp_state = 2;

        serial_print("[tcp] SYN-ACK <- ");
        print_ip(ip->src);
        serial_print("\n");
        send_tcp_segment(g_tcp_remote_ip, g_gateway_mac, g_tcp_seq, g_tcp_ack, TCP_FLAG_ACK, NULL, 0);
        serial_print("[tcp] ACK -> established\n");
        tcp_http_send_get();
        return;
    }

    if (g_tcp_state >= 3 && g_tcp_state <= 4 && payload_len > 0) {
        if (seq != g_tcp_ack) {
            send_tcp_segment(g_tcp_remote_ip, g_gateway_mac, g_tcp_seq, g_tcp_ack, TCP_FLAG_ACK, NULL, 0);
            return;
        }

        int n = payload_len;
        if (n > 120)
            n = 120;

        g_http_chunks++;
        g_http_bytes += (uint32_t) payload_len;
        http_process_payload(payload, payload_len);

        if (g_http_chunks <= 3) {
            serial_print("[http] chunk ");
            serial_print_dec(g_http_chunks);
            serial_print(": \"");
            for (int i = 0; i < n; i++) {
                char c = (char) payload[i];
                if (c == '\r' || c == '\n')
                    serial_print(" ");
                else
                    serial_putc(c);
            }
            serial_print("\"\n");
        }

        g_tcp_ack = seq + (uint32_t) payload_len;
        send_tcp_segment(g_tcp_remote_ip, g_gateway_mac, g_tcp_seq, g_tcp_ack, TCP_FLAG_ACK, NULL, 0);
        g_http_ok = 1;
        g_tcp_state = 4;
        http_print_body_preview();
    }

    if (tcp->flags & TCP_FLAG_FIN) {
        g_tcp_ack = seq + (uint32_t) payload_len + 1;
        send_tcp_segment(g_tcp_remote_ip, g_gateway_mac, g_tcp_seq, g_tcp_ack, TCP_FLAG_ACK, NULL, 0);
        serial_print("[http] total bytes=");
        serial_print_dec(g_http_bytes);
        serial_print(" chunks=");
        serial_print_dec(g_http_chunks);
        serial_print("\n");
        http_print_body_preview();
        serial_print("[tcp] FIN <- closed\n");
        g_tcp_state = 6;
    }
}

static void handle_arp(const arp_pkt_t *arp, int len) {
    if (len < (int) (sizeof(eth_hdr_t) + sizeof(arp_pkt_t)))
        return;

    uint16_t op = bswap16(arp->op);
    if (op == 2 && arp->spa == g_gateway_ip) {
        for (int i = 0; i < ETH_ALEN; i++)
            g_gateway_mac[i] = arp->sha[i];
        g_gateway_known = 1;

        serial_print("[net] ARP reply from ");
        print_ip(arp->spa);
        serial_print("\n");
        send_icmp_echo(g_gateway_ip, g_gateway_mac);
        if (!g_dns_requested)
            dns_query("example.com");
    }
}

static void parse_dhcp_options(const uint8_t *opt, int len, int *msg_type, uint32_t *server_ip,
                               uint32_t *router_ip, uint32_t *dns_ip) {
    int i = 0;
    while (i < len) {
        uint8_t code = opt[i++];
        if (code == 255)
            break;
        if (code == 0)
            continue;
        if (i >= len)
            break;
        uint8_t olen = opt[i++];
        if (i + olen > len)
            break;

        if (code == 53 && olen >= 1)
            *msg_type = opt[i];
        else if (code == 54 && olen >= 4)
            memcpy(server_ip, opt + i, 4);
        else if (code == 3 && olen >= 4)
            memcpy(router_ip, opt + i, 4);
        else if (code == 6 && olen >= 4)
            memcpy(dns_ip, opt + i, 4);
        i += olen;
    }
}

static void handle_dhcp(const udp_hdr_t *udp, int udp_payload_len) {
    if (udp_payload_len < (int) sizeof(dhcp_pkt_t))
        return;

    const dhcp_pkt_t *dhcp = (const dhcp_pkt_t *) ((const uint8_t *) udp + sizeof(udp_hdr_t));
    if (dhcp->op != 2 || dhcp->xid != bswap32(DHCP_XID) || dhcp->magic != bswap32(DHCP_MAGIC))
        return;

    int msg_type = 0;
    uint32_t server_ip = 0;
    uint32_t router_ip = 0;
    uint32_t dns_ip = 0;
    const uint8_t *opt = (const uint8_t *) dhcp + sizeof(dhcp_pkt_t);
    int opt_len = udp_payload_len - (int) sizeof(dhcp_pkt_t);
    parse_dhcp_options(opt, opt_len, &msg_type, &server_ip, &router_ip, &dns_ip);

    if (msg_type == DHCP_OFFER && !g_dhcp_requested) {
        g_offered_ip = dhcp->yiaddr;
        g_dhcp_server_ip = server_ip;
        serial_print("[dhcp] offer ip=");
        print_ip(g_offered_ip);
        serial_print(" server=");
        print_ip(g_dhcp_server_ip);
        serial_print("\n");
        g_dhcp_requested = 1;
        send_dhcp(DHCP_REQUEST, g_offered_ip, g_dhcp_server_ip);
    } else if (msg_type == DHCP_ACK && !g_dhcp_configured) {
        g_local_ip = dhcp->yiaddr;
        g_gateway_ip = router_ip ? router_ip : NET_IP(10, 0, 2, 2);
        g_dns_ip = dns_ip;
        g_dhcp_configured = 1;

        serial_print("[dhcp] ack ip=");
        print_ip(g_local_ip);
        serial_print(" gw=");
        print_ip(g_gateway_ip);
        if (g_dns_ip) {
            serial_print(" dns=");
            print_ip(g_dns_ip);
        }
        serial_print("\n");

        send_arp_request(g_gateway_ip);
    }
}

static void handle_ipv4(const ipv4_hdr_t *ip, int len) {
    if (len < (int) (sizeof(eth_hdr_t) + sizeof(ipv4_hdr_t)))
        return;
    if ((ip->ver_ihl >> 4) != 4)
        return;

    int ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || len < (int) sizeof(eth_hdr_t) + ihl)
        return;

    int ip_total = bswap16(ip->total_len);
    if (ip_total < ihl)
        return;

    if (ip->proto == IP_PROTO_UDP) {
        if (ip_total < ihl + (int) sizeof(udp_hdr_t))
            return;
        const udp_hdr_t *udp = (const udp_hdr_t *) ((const uint8_t *) ip + ihl);
        uint16_t src_port = bswap16(udp->src_port);
        uint16_t dst_port = bswap16(udp->dst_port);
        int udp_len = bswap16(udp->len);
        if (udp_len < (int) sizeof(udp_hdr_t))
            return;
        if (src_port == DHCP_SERVER_PORT && dst_port == DHCP_CLIENT_PORT)
            handle_dhcp(udp, udp_len - (int) sizeof(udp_hdr_t));
        else if (src_port == DNS_PORT && dst_port == DNS_CLIENT_PORT)
            handle_dns(udp, udp_len - (int) sizeof(udp_hdr_t));
        return;
    }

    if (ip->proto == IP_PROTO_TCP) {
        handle_tcp(ip, ihl, ip_total);
        return;
    }

    if (ip->dst != g_local_ip || ip->proto != IP_PROTO_ICMP)
        return;
    if (ip_total < ihl + (int) sizeof(icmp_hdr_t))
        return;

    const icmp_hdr_t *icmp = (const icmp_hdr_t *) ((const uint8_t *) ip + ihl);
    if (icmp->type == ICMP_ECHO_REPLY) {
        serial_print("[net] ICMP echo reply from ");
        print_ip(ip->src);
        serial_print(" seq=");
        serial_print_dec(bswap16(icmp->seq));
        serial_print("\n");
        g_ping_replied = 1;
    }
}

void net_handle_packet(const void *data, int len) {
    if (len < (int) sizeof(eth_hdr_t))
        return;

    const eth_hdr_t *eth = (const eth_hdr_t *) data;
    uint16_t type = bswap16(eth->type);

    if (type == ETH_P_ARP) {
        handle_arp((const arp_pkt_t *) ((const uint8_t *) data + sizeof(eth_hdr_t)), len);
    } else if (type == ETH_P_IP) {
        if (!mac_is_broadcast(eth->dst)) {
            int ours = 1;
            for (int i = 0; i < ETH_ALEN; i++)
                if (eth->dst[i] != g_mac[i]) ours = 0;
            if (!ours) return;
        }
        handle_ipv4((const ipv4_hdr_t *) ((const uint8_t *) data + sizeof(eth_hdr_t)), len);
    }
}

void net_init(void) {
    g_gateway_known = 0;
    g_dhcp_configured = 0;
    g_dhcp_requested = 0;
    g_dns_requested = 0;
    g_tcp_state = 0;
    g_http_ok = 0;
    g_dns_host[0] = 0;
    g_local_ip = 0;
    g_gateway_ip = 0;
    g_dns_ip = 0;
    g_dns_answer_ip = 0;
    g_dhcp_server_ip = 0;
    g_offered_ip = 0;
    g_tcp_remote_ip = 0;
    g_tcp_seq = 0xE0020000;
    g_tcp_ack = 0;
    g_tcp_get_seq = 0;
    g_tcp_get_len = 0;
    g_tcp_wait_ticks = 0;
    g_tcp_retries = 0;
    g_http_bytes = 0;
    g_http_chunks = 0;
    g_http_headers_done = 0;
    g_http_body_printed = 0;
    g_http_body_len = 0;
    g_http_body[0] = 0;
    g_http_host[0] = 0;
    g_ping_replied = 0;

    if (rtl8139_init() != 0)
        return;

    rtl8139_get_mac(g_mac);
    send_dhcp(DHCP_DISCOVER, 0, 0);
}

void net_poll(void) {
    (void) g_gateway_known;
    rtl8139_poll();
    tcp_poll_retransmit();
}

int net_is_configured(void) {
    return g_dhcp_configured;
}

int net_http_ok(void) {
    return g_http_ok;
}

uint32_t net_local_ip(void) {
    return g_local_ip;
}

uint32_t net_dns_answer_ip(void) {
    return g_dns_answer_ip;
}

const char *net_http_body_preview(void) {
    return g_http_body;
}

uint32_t net_http_body_len(void) {
    return g_http_body_len;
}

void net_http_reset(void) {
    g_tcp_state = 0;
    g_http_ok = 0;
    g_http_bytes = 0;
    g_http_chunks = 0;
    g_http_headers_done = 0;
    g_http_body_printed = 0;
    g_http_body_len = 0;
    g_http_body[0] = 0;
    g_dns_answer_ip = 0;
    g_tcp_retries = 0;
    g_tcp_wait_ticks = 0;
    g_tcp_remote_ip = 0;
    g_tcp_seq = 0xE0020000;
    g_tcp_ack = 0;
    g_tcp_get_seq = 0;
    g_tcp_get_len = 0;
}

int net_http_fetch(const char *host) {
    if (!g_dhcp_configured || !g_gateway_known)
        return -1;
    net_http_reset();
    strncpy(g_http_host, host, sizeof(g_http_host) - 1);
    g_http_host[sizeof(g_http_host) - 1] = 0;
    dns_query(host);
    return 0;
}

int net_tcp_busy(void) {
    return g_tcp_state == 1 || g_tcp_state == 2 || g_tcp_state == 3 || g_tcp_state == 4;
}

void net_ping_send(uint32_t dst_ip) {
    if (!g_gateway_known)
        return;
    g_ping_replied = 0;
    send_icmp_echo(dst_ip, g_gateway_mac);
}

int net_ping_replied(void) {
    return g_ping_replied;
}

void net_poll_wait(int ticks) {
    for (int i = 0; i < ticks; i++)
        net_poll();
}
