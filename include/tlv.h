#ifndef TLV_H
#define TLV_H

#include "easymesh_defs.h"

int tlv_put(uint8_t *buf, size_t buf_len, size_t *offset,
            uint8_t type, const void *value, uint16_t value_len);
int tlv_put_end(uint8_t *buf, size_t buf_len, size_t *offset);
int tlv_parse_all(const uint8_t *buf, size_t len, struct tlv_view *tlvs,
                  size_t max_tlvs, size_t *tlv_count);
const struct tlv_view *tlv_find(const struct cmdu_message *msg, uint8_t type);

#endif
