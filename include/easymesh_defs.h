#ifndef EASYMESH_DEFS_H
#define EASYMESH_DEFS_H

#include <stddef.h>
#include <stdint.h>

#define IEEE1905_ETHERTYPE 0x893A
#define IEEE1905_MULTICAST_MAC {0x01, 0x80, 0xC2, 0x00, 0x00, 0x13}

#define MAC_ADDR_LEN 6
#define MAX_CMDU_SIZE 1500
#define MAX_TLVS 16
#define MAX_NEIGHBORS 4
#define MAX_INTERFACES 3
#define MAX_LINK_STATUS 12

enum ieee1905_msg_type {
    MSG_TOPOLOGY_DISCOVERY = 0x0000,
    MSG_TOPOLOGY_NOTIFICATION = 0x0001,
    MSG_TOPOLOGY_QUERY = 0x0002,
    MSG_TOPOLOGY_RESPONSE = 0x0003,
    MSG_LINK_METRIC_QUERY = 0x0005,
    MSG_LINK_METRIC_RESPONSE = 0x0006
};

enum ieee1905_tlv_type {
    TLV_END_OF_MESSAGE = 0,
    TLV_AL_MAC_ADDRESS = 1,
    TLV_MAC_ADDRESS = 2,
    TLV_DEVICE_INFORMATION = 3,
    TLV_NEIGHBOR_DEVICE = 7,
    TLV_LINK_METRIC_QUERY = 8,
    TLV_RECEIVER_LINK_METRIC = 10
};

enum easymesh_role {
    ROLE_CONTROLLER = 1,
    ROLE_AGENT = 2
};

struct tlv_view {
    uint8_t type;
    uint16_t length;
    const uint8_t *value;
};

struct cmdu_message {
    uint16_t msg_type;
    uint16_t msg_id;
    uint8_t frag_id;
    uint8_t flags;
    struct tlv_view tlvs[MAX_TLVS];
    size_t tlv_count;
};

struct node_config {
    const char *name;
    const char *ifnames[MAX_INTERFACES];
    size_t interface_count;
    uint8_t al_mac[MAC_ADDR_LEN];
    uint8_t role;
    uint8_t neighbors[MAX_NEIGHBORS][MAC_ADDR_LEN];
    size_t neighbor_count;
    int32_t link_rssi_dbm[MAX_NEIGHBORS];
    const char *link_rssi_files[MAX_NEIGHBORS];
};

struct link_status {
    uint8_t reporter[MAC_ADDR_LEN];
    uint8_t neighbor[MAC_ADDR_LEN];
    int32_t rssi_dbm;
    int valid;
};

#endif
