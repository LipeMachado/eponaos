#include "net.h"
#include "rtl8139.h"
#include "serial.h"
#include "string.h"
#include <stddef.h>

#define IP_PROTO_ICMP 1
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

static uint8_t g_mac[ETH_ALEN];
static uint8_t g_gateway_mac[ETH_ALEN];
static int g_gateway_known = 0;
static int g_dhcp_configured = 0;
static int g_dhcp_requested = 0;
static uint16_t g_ip_id = 1;
static uint16_t g_icmp_seq = 1;

static uint32_t g_local_ip = 0;
static uint32_t g_gateway_ip = 0;
static uint32_t g_dns_ip = 0;
static uint32_t g_dhcp_server_ip = 0;
static uint32_t g_offered_ip = 0;

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
    g_local_ip = 0;
    g_gateway_ip = 0;
    g_dns_ip = 0;
    g_dhcp_server_ip = 0;
    g_offered_ip = 0;

    if (rtl8139_init() != 0)
        return;

    rtl8139_get_mac(g_mac);
    send_dhcp(DHCP_DISCOVER, 0, 0);
}

void net_poll(void) {
    (void) g_gateway_known;
    rtl8139_poll();
}
