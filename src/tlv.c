#include "tlv.h"

#include <arpa/inet.h>
#include <string.h>

int tlv_put(uint8_t *buf, size_t buf_len, size_t *offset,
            uint8_t type, const void *value, uint16_t value_len)
{
    size_t needed = *offset + 3u + value_len;

    if (needed > buf_len) {
        return -1;
    }

    buf[*offset] = type;
    *offset += 1;

    uint16_t net_len = htons(value_len);
    memcpy(buf + *offset, &net_len, sizeof(net_len));
    *offset += sizeof(net_len);

    if (value_len > 0 && value != NULL) {
        memcpy(buf + *offset, value, value_len);
    }
    *offset += value_len;

    return 0;
}

int tlv_put_end(uint8_t *buf, size_t buf_len, size_t *offset)
{
    return tlv_put(buf, buf_len, offset, TLV_END_OF_MESSAGE, NULL, 0);
}

int tlv_parse_all(const uint8_t *buf, size_t len, struct tlv_view *tlvs,
                  size_t max_tlvs, size_t *tlv_count)
{
    size_t offset = 0;
    size_t count = 0;

    while (offset + 3u <= len) {
        uint8_t type = buf[offset++];
        uint16_t value_len;

        memcpy(&value_len, buf + offset, sizeof(value_len));
        value_len = ntohs(value_len);
        offset += sizeof(value_len);

        if (offset + value_len > len) {
            return -1;
        }

        if (type == TLV_END_OF_MESSAGE) {
            *tlv_count = count;
            return 0;
        }

        if (count >= max_tlvs) {
            return -1;
        }

        tlvs[count].type = type;
        tlvs[count].length = value_len;
        tlvs[count].value = buf + offset;
        count++;
        offset += value_len;
    }

    *tlv_count = count;
    return 0;
}

const struct tlv_view *tlv_find(const struct cmdu_message *msg, uint8_t type)
{
    for (size_t i = 0; i < msg->tlv_count; i++) {
        if (msg->tlvs[i].type == type) {
            return &msg->tlvs[i];
        }
    }
    return NULL;
}
