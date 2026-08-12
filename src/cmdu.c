#include "cmdu.h"

#include "ethernet.h"
#include "tlv.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

struct cmdu_header_wire {
    uint8_t message_version;
    uint8_t reserved;
    uint16_t msg_type;
    uint16_t msg_id;
    uint8_t frag_id;
    uint8_t flags;
} __attribute__((packed));

#define CMDU_MESSAGE_VERSION 0x00u
#define CMDU_FLAG_LAST_FRAGMENT 0x80u

static const char *msg_type_name(uint16_t type)
{
    switch (type) {
    case MSG_TOPOLOGY_DISCOVERY:
        return "Topology Discovery";
    case MSG_TOPOLOGY_NOTIFICATION:
        return "Topology Notification";
    case MSG_TOPOLOGY_QUERY:
        return "Topology Query";
    case MSG_TOPOLOGY_RESPONSE:
        return "Topology Response";
    case MSG_LINK_METRIC_QUERY:
        return "Link Metric Query";
    case MSG_LINK_METRIC_RESPONSE:
        return "Link Metric Response";
    default:
        return "Unknown";
    }
}

static int put_al_and_interface(uint8_t *buf, size_t buf_len, size_t *offset,
                                const struct node_config *node)
{
    if (tlv_put(buf, buf_len, offset, TLV_AL_MAC_ADDRESS,
                node->al_mac, MAC_ADDR_LEN) < 0) {
        return -1;
    }
    return tlv_put(buf, buf_len, offset, TLV_MAC_ADDRESS,
                   node->al_mac, MAC_ADDR_LEN);
}

int cmdu_begin(uint8_t *buf, size_t buf_len, size_t *offset,
               uint16_t msg_type, uint16_t msg_id)
{
    if (buf_len < sizeof(struct cmdu_header_wire)) {
        return -1;
    }

    struct cmdu_header_wire hdr;
    hdr.message_version = CMDU_MESSAGE_VERSION;
    hdr.reserved = 0;
    hdr.msg_type = htons(msg_type);
    hdr.msg_id = htons(msg_id);
    hdr.frag_id = 0;
    hdr.flags = CMDU_FLAG_LAST_FRAGMENT;

    memcpy(buf, &hdr, sizeof(hdr));
    *offset = sizeof(hdr);
    return 0;
}

int cmdu_parse(const uint8_t *buf, size_t len, struct cmdu_message *msg)
{
    struct cmdu_header_wire hdr;

    if (len < sizeof(hdr)) {
        return -1;
    }

    memset(msg, 0, sizeof(*msg));
    memcpy(&hdr, buf, sizeof(hdr));
    msg->msg_type = ntohs(hdr.msg_type);
    msg->msg_id = ntohs(hdr.msg_id);
    msg->frag_id = hdr.frag_id;
    msg->flags = hdr.flags;

    return tlv_parse_all(buf + sizeof(hdr), len - sizeof(hdr),
                         msg->tlvs, MAX_TLVS, &msg->tlv_count);
}

int cmdu_build_discovery(uint8_t *buf, size_t buf_len, size_t *len,
                         uint16_t msg_id, const struct node_config *node)
{
    size_t offset;

    if (cmdu_begin(buf, buf_len, &offset, MSG_TOPOLOGY_DISCOVERY, msg_id) < 0 ||
        put_al_and_interface(buf, buf_len, &offset, node) < 0 ||
        tlv_put_end(buf, buf_len, &offset) < 0) {
        return -1;
    }

    *len = offset;
    return 0;
}

int cmdu_build_topology_query(uint8_t *buf, size_t buf_len, size_t *len,
                              uint16_t msg_id, const struct node_config *node)
{
    size_t offset;

    (void)node;
    if (cmdu_begin(buf, buf_len, &offset, MSG_TOPOLOGY_QUERY, msg_id) < 0 ||
        tlv_put_end(buf, buf_len, &offset) < 0) {
        return -1;
    }

    *len = offset;
    return 0;
}

static int cmdu_build_topology_payload(uint8_t *buf, size_t buf_len, size_t *len,
                                       uint16_t msg_type, uint16_t msg_id,
                                       const struct node_config *node)
{
    size_t offset;
    uint8_t info[16] = {0};

    memcpy(info, node->al_mac, MAC_ADDR_LEN);
    info[6] = 1; /* one simulated local interface */
    memcpy(info + 7, node->al_mac, MAC_ADDR_LEN);
    info[13] = 0x01; /* IEEE 802.11 media type */
    info[14] = 0x00;
    info[15] = 0; /* no media-specific information */

    if (cmdu_begin(buf, buf_len, &offset, msg_type, msg_id) < 0) {
        return -1;
    }

    if (msg_type == MSG_TOPOLOGY_NOTIFICATION) {
        if (tlv_put(buf, buf_len, &offset, TLV_AL_MAC_ADDRESS,
                    node->al_mac, MAC_ADDR_LEN) < 0) return -1;
    } else if (tlv_put(buf, buf_len, &offset, TLV_DEVICE_INFORMATION,
                       info, sizeof(info)) < 0) {
        return -1;
    }

    if (msg_type == MSG_TOPOLOGY_RESPONSE) {
        for (size_t i = 0; i < node->neighbor_count; i++) {
            uint8_t neighbor[13] = {0};
            memcpy(neighbor, node->al_mac, MAC_ADDR_LEN);
            memcpy(neighbor + 6, node->neighbors[i], MAC_ADDR_LEN);
            if (tlv_put(buf, buf_len, &offset, TLV_NEIGHBOR_DEVICE,
                        neighbor, sizeof(neighbor)) < 0) {
                return -1;
            }
        }
    }

    if (tlv_put_end(buf, buf_len, &offset) < 0) {
        return -1;
    }

    *len = offset;
    return 0;
}

int cmdu_build_topology_notification(uint8_t *buf, size_t buf_len, size_t *len,
                                     uint16_t msg_id, const struct node_config *node)
{
    return cmdu_build_topology_payload(buf, buf_len, len,
                                       MSG_TOPOLOGY_NOTIFICATION, msg_id, node);
}

int cmdu_build_topology_response(uint8_t *buf, size_t buf_len, size_t *len,
                                 uint16_t msg_id, const struct node_config *node)
{
    return cmdu_build_topology_payload(buf, buf_len, len,
                                       MSG_TOPOLOGY_RESPONSE, msg_id, node);
}

int cmdu_build_link_metric_query(uint8_t *buf, size_t buf_len, size_t *len,
                                 uint16_t msg_id, const struct node_config *node)
{
    size_t offset;
    uint8_t query[2] = {0, 2};

    (void)node;
    /* IEEE 1905.1 Link Metric Query TLV: all neighbors, TX and RX metrics. */
    /* For all-neighbors queries, the specific-neighbor EUI-48 is omitted. */

    if (cmdu_begin(buf, buf_len, &offset, MSG_LINK_METRIC_QUERY, msg_id) < 0 ||
        tlv_put(buf, buf_len, &offset, TLV_LINK_METRIC_QUERY,
                query, sizeof(query)) < 0 ||
        tlv_put_end(buf, buf_len, &offset) < 0) {
        return -1;
    }

    *len = offset;
    return 0;
}

int cmdu_build_link_metric_response(uint8_t *buf, size_t buf_len, size_t *len,
                                    uint16_t msg_id, const struct node_config *node)
{
    size_t offset;

    if (cmdu_begin(buf, buf_len, &offset, MSG_LINK_METRIC_RESPONSE, msg_id) < 0) {
        return -1;
    }

    for (size_t i = 0; i < node->neighbor_count; i++) {
        uint8_t metric[35] = {0};

        /* IEEE 1905.1 Receiver Link Metric TLV: 12-byte AL header plus one
         * 23-byte interface-pair metric. AL MACs stand in for simulated
         * interface MACs; counters are unavailable and therefore zero. */
        memcpy(metric, node->al_mac, MAC_ADDR_LEN);
        memcpy(metric + 6, node->neighbors[i], MAC_ADDR_LEN);
        memcpy(metric + 12, node->neighbors[i], MAC_ADDR_LEN);
        memcpy(metric + 18, node->neighbors[i], MAC_ADDR_LEN);
        metric[24] = 0x01; /* IEEE 802.11 media type */
        metric[25] = 0x00;
        metric[34] = (uint8_t)node->link_rssi_dbm[i];

        if (tlv_put(buf, buf_len, &offset, TLV_RECEIVER_LINK_METRIC,
                    metric, sizeof(metric)) < 0) {
            return -1;
        }
    }

    if (tlv_put_end(buf, buf_len, &offset) < 0) {
        return -1;
    }

    *len = offset;
    return 0;
}

void cmdu_print(const struct cmdu_message *msg)
{
    printf("CMDU type=0x%04X (%s), id=%u, tlvs=%zu\n",
           msg->msg_type, msg_type_name(msg->msg_type), msg->msg_id,
           msg->tlv_count);

    for (size_t i = 0; i < msg->tlv_count; i++) {
        const struct tlv_view *tlv = &msg->tlvs[i];
        char mac[18];

        printf("  TLV type=%u len=%u", tlv->type, tlv->length);
        if ((tlv->type == TLV_AL_MAC_ADDRESS ||
             tlv->type == TLV_NEIGHBOR_DEVICE) &&
            tlv->length == MAC_ADDR_LEN) {
            mac_to_string(tlv->value + MAC_ADDR_LEN, mac, sizeof(mac));
            printf(" mac=%s", mac);
        } else if (tlv->type == TLV_MAC_ADDRESS && tlv->length == MAC_ADDR_LEN) {
            mac_to_string(tlv->value, mac, sizeof(mac));
            printf(" interface=%s", mac);
        } else if (tlv->type == TLV_DEVICE_INFORMATION && tlv->length >= 16) {
            mac_to_string(tlv->value, mac, sizeof(mac));
            printf(" al=%s interfaces=%u", mac, tlv->value[6]);
        } else if (tlv->type == TLV_RECEIVER_LINK_METRIC && tlv->length == 35) {

            mac_to_string(tlv->value, mac, sizeof(mac));
            printf(" neighbor=%s rssi=%d dBm", mac,
                   (int8_t)tlv->value[34]);
        }
        printf("\n");
    }
}
