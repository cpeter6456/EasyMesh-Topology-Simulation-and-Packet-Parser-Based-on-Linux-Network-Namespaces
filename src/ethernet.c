#include "ethernet.h"

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <stdio.h>
#include <string.h>

int ethernet_build(uint8_t *frame, size_t frame_len, size_t *out_len,
                   const uint8_t dst_mac[MAC_ADDR_LEN],
                   const uint8_t src_mac[MAC_ADDR_LEN],
                   const uint8_t *payload, size_t payload_len)
{
    size_t total = sizeof(struct ethhdr) + payload_len;

    if (total > frame_len) {
        return -1;
    }

    struct ethhdr *eth = (struct ethhdr *)frame;
    memcpy(eth->h_dest, dst_mac, MAC_ADDR_LEN);
    memcpy(eth->h_source, src_mac, MAC_ADDR_LEN);
    eth->h_proto = htons(IEEE1905_ETHERTYPE);

    memcpy(frame + sizeof(struct ethhdr), payload, payload_len);
    *out_len = total;
    return 0;
}

int ethernet_parse(const uint8_t *frame, size_t frame_len,
                   uint8_t dst_mac[MAC_ADDR_LEN],
                   uint8_t src_mac[MAC_ADDR_LEN],
                   const uint8_t **payload, size_t *payload_len)
{
    const struct ethhdr *eth;

    if (frame_len < sizeof(struct ethhdr)) {
        return -1;
    }

    eth = (const struct ethhdr *)frame;
    if (ntohs(eth->h_proto) != IEEE1905_ETHERTYPE) {
        return -1;
    }

    memcpy(dst_mac, eth->h_dest, MAC_ADDR_LEN);
    memcpy(src_mac, eth->h_source, MAC_ADDR_LEN);
    *payload = frame + sizeof(struct ethhdr);
    *payload_len = frame_len - sizeof(struct ethhdr);
    return 0;
}

void mac_to_string(const uint8_t mac[MAC_ADDR_LEN], char *out, size_t out_len)
{
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int parse_mac(const char *text, uint8_t mac[MAC_ADDR_LEN])
{
    unsigned int values[MAC_ADDR_LEN];

    if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return -1;
    }

    for (size_t i = 0; i < MAC_ADDR_LEN; i++) {
        if (values[i] > 0xFF) {
            return -1;
        }
        mac[i] = (uint8_t)values[i];
    }

    return 0;
}
