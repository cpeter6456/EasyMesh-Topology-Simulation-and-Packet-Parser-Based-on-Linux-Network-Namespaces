#include "app.h"

#include "cmdu.h"
#include "ethernet.h"
#include "raw_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const uint8_t multicast_mac[MAC_ADDR_LEN] = IEEE1905_MULTICAST_MAC;

static void copy_mac(uint8_t dst[MAC_ADDR_LEN], const char *src)
{
    if (parse_mac(src, dst) < 0) {
        fprintf(stderr, "invalid built-in mac: %s\n", src);
    }
}

int node_config_from_name(const char *name, struct node_config *node)
{
    const char *override_ifname = getenv("EASYMESH_IFNAME");

    memset(node, 0, sizeof(*node));

    if (strcmp(name, "controller") == 0) {
        node->name = "Controller";
        node->ifnames[0] = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "c_to_a1";
        node->ifnames[1] = "c_to_a2";
        node->interface_count = 2;
        node->role = ROLE_CONTROLLER;
        node->rssi_dbm = -35;
        copy_mac(node->al_mac, "02:00:00:00:00:01");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:02");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:03");
        return 0;
    }

    if (strcmp(name, "agent1") == 0) {
        node->name = "Agent_1";
        node->ifnames[0] = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "a1_to_c";
        node->ifnames[1] = "a1_to_a2";
        node->interface_count = 2;
        node->role = ROLE_AGENT;
        node->rssi_dbm = -50;
        copy_mac(node->al_mac, "02:00:00:00:00:02");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:01");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:03");
        return 0;
    }

    if (strcmp(name, "agent2") == 0) {
        node->name = "Agent_2";
        node->ifnames[0] = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "a2_to_c";
        node->ifnames[1] = "a2_to_a1";
        node->interface_count = 2;
        node->role = ROLE_AGENT;
        node->rssi_dbm = -65;
        copy_mac(node->al_mac, "02:00:00:00:00:03");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:01");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:02");
        return 0;
    }

    return -1;
}

static int send_cmdu(int fd, const struct node_config *node,
                     const char *ifname,
                     const uint8_t dst_mac[MAC_ADDR_LEN],
                     const uint8_t *cmdu, size_t cmdu_len)
{
    uint8_t frame[MAX_CMDU_SIZE];
    size_t frame_len;

    if (ethernet_build(frame, sizeof(frame), &frame_len,
                       dst_mac, node->al_mac, cmdu, cmdu_len) < 0) {
        return -1;
    }

    return raw_socket_send(fd, ifname, dst_mac, frame, frame_len);
}

static int send_built_cmdu(int fd, const struct node_config *node,
                           const char *ifname,
                           const uint8_t dst_mac[MAC_ADDR_LEN],
                           int (*builder)(uint8_t *, size_t, size_t *,
                                          uint16_t, const struct node_config *),
                           uint16_t msg_id)
{
    uint8_t cmdu[MAX_CMDU_SIZE];
    size_t cmdu_len;

    if (builder(cmdu, sizeof(cmdu), &cmdu_len, msg_id, node) < 0) {
        return -1;
    }

    return send_cmdu(fd, node, ifname, dst_mac, cmdu, cmdu_len);
}

static void send_built_cmdu_all(const int fds[MAX_INTERFACES],
                                const struct node_config *node,
                                int (*builder)(uint8_t *, size_t, size_t *,
                                               uint16_t, const struct node_config *),
                                uint16_t msg_id)
{
    for (size_t i = 0; i < node->interface_count; i++) {
        send_built_cmdu(fds[i], node, node->ifnames[i], multicast_mac,
                        builder, msg_id);
    }
}

static int receive_one(int fd, uint8_t src_mac[MAC_ADDR_LEN],
                       struct cmdu_message *msg)
{
    uint8_t frame[MAX_CMDU_SIZE];
    uint8_t dst_mac[MAC_ADDR_LEN];
    const uint8_t *payload;
    size_t payload_len;
    ssize_t n = raw_socket_recv(fd, frame, sizeof(frame));

    if (n < 0) {
        return -1;
    }

    if (ethernet_parse(frame, (size_t)n, dst_mac, src_mac,
                       &payload, &payload_len) < 0) {
        return -1;
    }

    return cmdu_parse(payload, payload_len, msg);
}

static void print_rx(const struct node_config *node,
                     const uint8_t src_mac[MAC_ADDR_LEN],
                     const struct cmdu_message *msg)
{
    char src[18];
    mac_to_string(src_mac, src, sizeof(src));
    printf("\n[%s] RX from %s\n", node->name, src);
    cmdu_print(msg);
    fflush(stdout);
}

static const char *node_name_for_mac(const uint8_t mac[MAC_ADDR_LEN])
{
    static const uint8_t controller[] = {0x02, 0, 0, 0, 0, 1};
    static const uint8_t agent1[] = {0x02, 0, 0, 0, 0, 2};
    static const uint8_t agent2[] = {0x02, 0, 0, 0, 0, 3};

    if (memcmp(mac, controller, MAC_ADDR_LEN) == 0) return "Controller";
    if (memcmp(mac, agent1, MAC_ADDR_LEN) == 0) return "Agent_1";
    if (memcmp(mac, agent2, MAC_ADDR_LEN) == 0) return "Agent_2";
    return "Unknown";
}

static void print_link_status(const uint8_t reporter[MAC_ADDR_LEN],
                              const struct cmdu_message *msg)
{
    for (size_t i = 0; i < msg->tlv_count; i++) {
        const struct tlv_view *tlv = &msg->tlvs[i];
        uint32_t rssi;

        if (tlv->type != TLV_LINK_METRIC || tlv->length != 14) continue;
        memcpy(&rssi, tlv->value + MAC_ADDR_LEN + 4, sizeof(rssi));
        printf("[Mesh status] %s <-> %s: RSSI %d dBm (updated)\n",
               node_name_for_mac(reporter), node_name_for_mac(tlv->value),
               (int32_t)ntohl(rssi));
    }
}

static int open_interfaces(const struct node_config *node,
                           int fds[MAX_INTERFACES])
{
    for (size_t i = 0; i < node->interface_count; i++) {
        fds[i] = raw_socket_open(node->ifnames[i]);
        if (fds[i] < 0) {
            while (i > 0) close(fds[--i]);
            return -1;
        }
    }
    return 0;
}

int run_controller(const struct node_config *node)
{
    int fds[MAX_INTERFACES];

    if (open_interfaces(node, fds) < 0) {
        perror("raw_socket_open");
        return 1;
    }

    printf("[%s] listening on %s and %s for EtherType 0x%04X\n",
           node->name, node->ifnames[0], node->ifnames[1], IEEE1905_ETHERTYPE);
    fflush(stdout);

    for (;;) {
        struct pollfd pfds[MAX_INTERFACES];
        for (size_t i = 0; i < node->interface_count; i++) {
            pfds[i].fd = fds[i];
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
        }
        if (poll(pfds, node->interface_count, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (size_t i = 0; i < node->interface_count; i++) {
            uint8_t src_mac[MAC_ADDR_LEN];
            struct cmdu_message msg;
            if (!(pfds[i].revents & POLLIN) || receive_one(fds[i], src_mac, &msg) < 0 ||
                memcmp(src_mac, node->al_mac, MAC_ADDR_LEN) == 0) continue;
            print_rx(node, src_mac, &msg);
            if (msg.msg_type == MSG_LINK_METRIC_RESPONSE) {
                print_link_status(src_mac, &msg);
            } else if (msg.msg_type == MSG_TOPOLOGY_DISCOVERY ||
                       msg.msg_type == MSG_TOPOLOGY_QUERY ||
                       msg.msg_type == MSG_TOPOLOGY_NOTIFICATION) {
                send_built_cmdu(fds[i], node, node->ifnames[i], src_mac,
                                cmdu_build_topology_response, msg.msg_id);
            } else if (msg.msg_type == MSG_LINK_METRIC_QUERY) {
                send_built_cmdu(fds[i], node, node->ifnames[i], src_mac,
                                cmdu_build_link_metric_response, msg.msg_id);
            }
        }
        fflush(stdout);
    }

    for (size_t i = 0; i < node->interface_count; i++) close(fds[i]);
    return 1;
}

static int poll_for_replies(const int fds[MAX_INTERFACES], const struct node_config *node,
                            int milliseconds)
{
    int elapsed = 0;

    while (elapsed < milliseconds) {
        struct pollfd pfds[MAX_INTERFACES];
        for (size_t i = 0; i < node->interface_count; i++) {
            pfds[i].fd = fds[i];
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
        }
        int rc = poll(pfds, node->interface_count, 500);

        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        elapsed += 500;
        if (rc == 0) {
            continue;
        }

        for (size_t i = 0; i < node->interface_count; i++) {
            uint8_t src_mac[MAC_ADDR_LEN];
            struct cmdu_message msg;

            if ((pfds[i].revents & POLLIN) && receive_one(fds[i], src_mac, &msg) == 0) {
                if (memcmp(src_mac, node->al_mac, MAC_ADDR_LEN) == 0) {
                    continue;
                }
                print_rx(node, src_mac, &msg);

                if (msg.msg_type == MSG_TOPOLOGY_QUERY ||
                    msg.msg_type == MSG_TOPOLOGY_NOTIFICATION) {
                    send_built_cmdu(fds[i], node, node->ifnames[i], src_mac,
                                    cmdu_build_topology_response, msg.msg_id);
                } else if (msg.msg_type == MSG_LINK_METRIC_QUERY) {
                    send_built_cmdu(fds[i], node, node->ifnames[i], src_mac,
                                    cmdu_build_link_metric_response, msg.msg_id);
                }
            }
        }
    }

    return 0;
}

int run_agent(const struct node_config *node, int once)
{
    int fds[MAX_INTERFACES];
    uint16_t msg_id = 1000;

    if (open_interfaces(node, fds) < 0) {
        perror("raw_socket_open");
        return 1;
    }

    printf("[%s] started on %s and %s, AL MAC ready\n", node->name,
           node->ifnames[0], node->ifnames[1]);

    do {
        sleep(3);
        printf("[%s] TX Discovery\n", node->name);
        send_built_cmdu_all(fds, node, cmdu_build_discovery, msg_id++);

        printf("[%s] TX Topology Query\n", node->name);
        send_built_cmdu_all(fds, node, cmdu_build_topology_query, msg_id++);

        printf("[%s] TX Topology Notification\n", node->name);
        send_built_cmdu_all(fds, node, cmdu_build_topology_notification, msg_id++);

        printf("[%s] TX Link Metric Query\n", node->name);
        send_built_cmdu_all(fds, node, cmdu_build_link_metric_query, msg_id++);

        /* Periodic reports let the controller refresh every direct mesh link. */
        printf("[%s] TX Link Metric Response (RSSI report)\n", node->name);
        send_built_cmdu_all(fds, node, cmdu_build_link_metric_response, msg_id++);

        fflush(stdout);
        poll_for_replies(fds, node, once ? 3000 : 5000);
    } while (!once);

    for (size_t i = 0; i < node->interface_count; i++) close(fds[i]);
    return 0;
}
