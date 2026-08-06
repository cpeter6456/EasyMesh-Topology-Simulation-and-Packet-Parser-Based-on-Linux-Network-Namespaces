#include "cmdu.h"

#include "ethernet.h"
#include "tlv.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

struct cmdu_header_wire {
    uint16_t msg_type;
    uint16_t msg_id;
    uint8_t frag_id;
    uint8_t flags;
    uint16_t reserved;
} __attribute__((packed));

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

static int put_al_and_role(uint8_t *buf, size_t buf_len, size_t *offset,
                           const struct node_config *node)
{
    if (tlv_put(buf, buf_len, offset, TLV_AL_MAC_ADDRESS,
                node->al_mac, MAC_ADDR_LEN) < 0) {
        return -1;
    }
    return tlv_put(buf, buf_len, offset, TLV_SUPPORTED_ROLE,
                   &node->role, sizeof(node->role));
}

int cmdu_begin(uint8_t *buf, size_t buf_len, size_t *offset,
               uint16_t msg_type, uint16_t msg_id)
{
    if (buf_len < sizeof(struct cmdu_header_wire)) {
        return -1;
    }

    struct cmdu_header_wire hdr;
    hdr.msg_type = htons(msg_type);
    hdr.msg_id = htons(msg_id);
    hdr.frag_id = 0;
    hdr.flags = 0;
    hdr.reserved = 0;

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
        put_al_and_role(buf, buf_len, &offset, node) < 0 ||
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

    if (cmdu_begin(buf, buf_len, &offset, MSG_TOPOLOGY_QUERY, msg_id) < 0 ||
        put_al_and_role(buf, buf_len, &offset, node) < 0 ||
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
    uint8_t info[1 + MAC_ADDR_LEN + 1];

    info[0] = node->role;
    memcpy(info + 1, node->al_mac, MAC_ADDR_LEN);
    info[1 + MAC_ADDR_LEN] = (uint8_t)node->neighbor_count;

    if (cmdu_begin(buf, buf_len, &offset, msg_type, msg_id) < 0 ||
        put_al_and_role(buf, buf_len, &offset, node) < 0 ||
        tlv_put(buf, buf_len, &offset, TLV_DEVICE_INFORMATION,
                info, sizeof(info)) < 0) {
        return -1;
    }

    for (size_t i = 0; i < node->neighbor_count; i++) {
        if (tlv_put(buf, buf_len, &offset, TLV_NEIGHBOR_DEVICE,
                    node->neighbors[i], MAC_ADDR_LEN) < 0) {
            return -1;
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

    if (cmdu_begin(buf, buf_len, &offset, MSG_LINK_METRIC_QUERY, msg_id) < 0 ||
        put_al_and_role(buf, buf_len, &offset, node) < 0 ||
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

    if (cmdu_begin(buf, buf_len, &offset, MSG_LINK_METRIC_RESPONSE, msg_id) < 0 ||
        put_al_and_role(buf, buf_len, &offset, node) < 0) {
        return -1;
    }

    for (size_t i = 0; i < node->neighbor_count; i++) {
        uint8_t metric[MAC_ADDR_LEN + 4];
        uint32_t cost = 100u + (uint32_t)(node->rssi_dbm < 0 ? -node->rssi_dbm : node->rssi_dbm) + (uint32_t)i * 5u;
        uint32_t cost_net = htonl(cost);

        memcpy(metric, node->neighbors[i], MAC_ADDR_LEN);
        memcpy(metric + MAC_ADDR_LEN, &cost_net, sizeof(cost_net));

        if (tlv_put(buf, buf_len, &offset, TLV_LINK_METRIC,
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
            mac_to_string(tlv->value, mac, sizeof(mac));
            printf(" mac=%s", mac);
        } else if (tlv->type == TLV_SUPPORTED_ROLE && tlv->length == 1) {
            printf(" role=%u", tlv->value[0]);
        } else if (tlv->type == TLV_DEVICE_INFORMATION && tlv->length >= 8) {
            mac_to_string(tlv->value + 1, mac, sizeof(mac));
            printf(" role=%u al=%s neighbors=%u",
                   tlv->value[0], mac, tlv->value[7]);
        } else if (tlv->type == TLV_LINK_METRIC && tlv->length == 10) {
            uint32_t cost;

            mac_to_string(tlv->value, mac, sizeof(mac));
            memcpy(&cost, tlv->value + MAC_ADDR_LEN, sizeof(cost));
            printf(" neighbor=%s cost=%u", mac, ntohl(cost));
        }
        printf("\n");
    }
}
