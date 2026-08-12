#include "app.h"

#include "cmdu.h"
#include "ethernet.h"
#include "tlv.h"

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "selftest failed: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    struct node_config node;
    uint8_t buf[1024];
    uint8_t frame[2048];
    size_t len = 0;
    size_t frame_len = 0;
    struct cmdu_message msg;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t dst_mac[MAC_ADDR_LEN] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x13};
    uint8_t src_mac[MAC_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    const struct tlv_view *tlv = NULL;

    if (node_config_from_name("agent1", &node) < 0) {
        fprintf(stderr, "failed to load agent1 config\n");
        return 1;
    }
    if (check(node.interface_count == 1 && node.neighbor_count == 3,
              "agent1 bridge and four-node topology configuration") < 0) return 1;
    if (check(node.link_rssi_dbm[0] != node.link_rssi_dbm[1],
              "agent1 per-link RSSI configuration") < 0) return 1;
    if (node_config_from_name("agent3", &node) < 0 ||
        check(node.interface_count == 1 && node.neighbor_count == 1,
              "agent3 leaf-link configuration") < 0) return 1;
    if (node_config_from_name("agent1", &node) < 0) return 1;

    if (cmdu_build_discovery(buf, sizeof(buf), &len, 1001, &node) < 0) {
        fprintf(stderr, "discovery build failed\n");
        return 1;
    }

    if (check(len >= 8 && buf[0] == 0 && buf[1] == 0 &&
              buf[2] == 0 && buf[3] == 0 &&
              buf[4] == 0x03 && buf[5] == 0xe9 &&
              buf[6] == 0 && buf[7] == 0x80,
              "IEEE 1905.1 CMDU header wire format") < 0) return 1;

    if (cmdu_parse(buf, len, &msg) < 0) {
        fprintf(stderr, "discovery parse failed\n");
        return 1;
    }

    if (check(msg.msg_type == MSG_TOPOLOGY_DISCOVERY, "message type") < 0) return 1;
    if (check(msg.tlv_count == 2, "discovery tlv count") < 0) return 1;

    tlv = tlv_find(&msg, TLV_AL_MAC_ADDRESS);
    if (check(tlv != NULL && tlv->length == MAC_ADDR_LEN, "AL MAC TLV") < 0) return 1;
    if (check(memcmp(tlv->value, node.al_mac, MAC_ADDR_LEN) == 0, "AL MAC value") < 0) return 1;

    if (cmdu_build_topology_query(buf, sizeof(buf), &len, 1006, &node) < 0 ||
        cmdu_parse(buf, len, &msg) < 0 ||
        check(msg.msg_type == MSG_TOPOLOGY_QUERY && msg.tlv_count == 0,
              "topology query has no mandatory TLVs") < 0) return 1;

    if (cmdu_build_topology_notification(buf, sizeof(buf), &len, 1002, &node) < 0) {
        fprintf(stderr, "topology notification build failed\n");
        return 1;
    }

    if (cmdu_parse(buf, len, &msg) < 0) {
        fprintf(stderr, "topology notification parse failed\n");
        return 1;
    }

    if (check(msg.msg_type == MSG_TOPOLOGY_NOTIFICATION, "topology notification type") < 0) return 1;
    if (check(msg.tlv_count == 1 && msg.tlvs[0].type == TLV_AL_MAC_ADDRESS,
              "topology notification TLV format") < 0) return 1;

    if (cmdu_build_topology_response(buf, sizeof(buf), &len, 1003, &node) < 0) {
        fprintf(stderr, "topology response build failed\n");
        return 1;
    }

    if (cmdu_parse(buf, len, &msg) < 0) {
        fprintf(stderr, "topology response parse failed\n");
        return 1;
    }

    if (check(msg.msg_type == MSG_TOPOLOGY_RESPONSE, "topology response type") < 0) return 1;
    if (check(msg.tlv_count >= 1 && msg.tlvs[0].type == TLV_DEVICE_INFORMATION,
              "topology response TLV format") < 0) return 1;

    if (cmdu_build_link_metric_response(buf, sizeof(buf), &len, 1004, &node) < 0) {
        fprintf(stderr, "link metric response build failed\n");
        return 1;
    }

    if (cmdu_parse(buf, len, &msg) < 0) {
        fprintf(stderr, "link metric response parse failed\n");
        return 1;
    }

    if (check(msg.msg_type == MSG_LINK_METRIC_RESPONSE, "link metric response type") < 0) return 1;
    if (check(msg.tlv_count == node.neighbor_count, "link metric tlv count") < 0) return 1;
    if (check(msg.tlvs[0].type == TLV_RECEIVER_LINK_METRIC && msg.tlvs[0].length == 35,
              "receiver link metric TLV format") < 0) return 1;
    if (check(memcmp(msg.tlvs[0].value, node.al_mac, MAC_ADDR_LEN) == 0 &&
              memcmp(msg.tlvs[0].value + MAC_ADDR_LEN, node.neighbors[0], MAC_ADDR_LEN) == 0 &&
              (int8_t)msg.tlvs[0].value[34] == node.link_rssi_dbm[0],
              "receiver link metric per-link values") < 0) return 1;

    if (cmdu_build_link_metric_query(buf, sizeof(buf), &len, 1005, &node) < 0 ||
        cmdu_parse(buf, len, &msg) < 0) {
        fprintf(stderr, "link metric query build/parse failed\n");
        return 1;
    }
    if (check(msg.tlv_count == 1 && msg.tlvs[0].type == TLV_LINK_METRIC_QUERY &&
              msg.tlvs[0].length == 2 && msg.tlvs[0].value[0] == 0 &&
              msg.tlvs[0].value[1] == 2, "link metric query TLV format") < 0) return 1;

    if (ethernet_build(frame, sizeof(frame), &frame_len, dst_mac, src_mac, buf, len) < 0) {
        fprintf(stderr, "ethernet build failed\n");
        return 1;
    }

    if (ethernet_parse(frame, frame_len, dst_mac, src_mac, &payload, &payload_len) < 0) {
        fprintf(stderr, "ethernet parse failed\n");
        return 1;
    }

    if (check(payload_len == len, "payload length") < 0) return 1;
    if (check(memcmp(payload, buf, len) == 0, "payload bytes") < 0) return 1;

    printf("selftest: CMDU/TLV/Ethernet round trip OK\n");
    return 0;
}
