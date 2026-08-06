#ifndef EASYMESH_ETHERNET_H
#define EASYMESH_ETHERNET_H

#include "easymesh_defs.h"

int ethernet_build(uint8_t *frame, size_t frame_len, size_t *out_len,
                   const uint8_t dst_mac[MAC_ADDR_LEN],
                   const uint8_t src_mac[MAC_ADDR_LEN],
                   const uint8_t *payload, size_t payload_len);
int ethernet_parse(const uint8_t *frame, size_t frame_len,
                   uint8_t dst_mac[MAC_ADDR_LEN],
                   uint8_t src_mac[MAC_ADDR_LEN],
                   const uint8_t **payload, size_t *payload_len);
void mac_to_string(const uint8_t mac[MAC_ADDR_LEN], char *out, size_t out_len);
int parse_mac(const char *text, uint8_t mac[MAC_ADDR_LEN]);

#endif
