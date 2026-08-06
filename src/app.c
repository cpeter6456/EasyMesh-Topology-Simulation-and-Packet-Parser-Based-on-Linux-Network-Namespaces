#include "app.h"

#include "cmdu.h"
#include "ethernet.h"
#include "raw_socket.h"

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
        node->ifname = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "c_to_a1";
        node->role = ROLE_CONTROLLER;
        node->rssi_dbm = -35;
        copy_mac(node->al_mac, "02:00:00:00:00:01");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:02");
        return 0;
    }

    if (strcmp(name, "agent1") == 0) {
        node->name = "Agent_1";
        node->ifname = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "br-agent1";
        node->role = ROLE_AGENT;
        node->rssi_dbm = -50;
        copy_mac(node->al_mac, "02:00:00:00:00:02");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:01");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:03");
        return 0;
    }

    if (strcmp(name, "agent2") == 0) {
        node->name = "Agent_2";
        node->ifname = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "a2_to_a1";
        node->role = ROLE_AGENT;
        node->rssi_dbm = -65;
        copy_mac(node->al_mac, "02:00:00:00:00:03");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:02");
        return 0;
    }

    return -1;
}

static int send_cmdu(int fd, const struct node_config *node,
                     const uint8_t dst_mac[MAC_ADDR_LEN],
                     const uint8_t *cmdu, size_t cmdu_len)
{
    uint8_t frame[MAX_CMDU_SIZE];
    size_t frame_len;

    if (ethernet_build(frame, sizeof(frame), &frame_len,
                       dst_mac, node->al_mac, cmdu, cmdu_len) < 0) {
        return -1;
    }

    return raw_socket_send(fd, node->ifname, dst_mac, frame, frame_len);
}

static int send_built_cmdu(int fd, const struct node_config *node,
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

    return send_cmdu(fd, node, dst_mac, cmdu, cmdu_len);
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
    printf("\n[%s] RX from %s on %s\n", node->name, src, node->ifname);
    cmdu_print(msg);
    fflush(stdout);
}

int run_controller(const struct node_config *node)
{
    int fd = raw_socket_open(node->ifname);

    if (fd < 0) {
        perror("raw_socket_open");
        return 1;
    }

    printf("[%s] listening on %s for EtherType 0x%04X\n",
           node->name, node->ifname, IEEE1905_ETHERTYPE);
    fflush(stdout);

    for (;;) {
        sleep(3);
        uint8_t src_mac[MAC_ADDR_LEN];
        struct cmdu_message msg;

        if (receive_one(fd, src_mac, &msg) < 0) {
            if (errno == EINTR) {
                continue;
            }
            continue;
        }
        if (memcmp(src_mac, node->al_mac, MAC_ADDR_LEN) == 0) {
            continue;
        }

        print_rx(node, src_mac, &msg);

        if (msg.msg_type == MSG_TOPOLOGY_DISCOVERY ||
            msg.msg_type == MSG_TOPOLOGY_QUERY ||
            msg.msg_type == MSG_TOPOLOGY_NOTIFICATION) {
            if (send_built_cmdu(fd, node, src_mac, cmdu_build_topology_response,
                                msg.msg_id) == 0) {
                printf("[%s] TX Topology Response\n", node->name);
            }
        } else if (msg.msg_type == MSG_LINK_METRIC_QUERY) {
            if (send_built_cmdu(fd, node, src_mac, cmdu_build_link_metric_response,
                                msg.msg_id) == 0) {
                printf("[%s] TX Link Metric Response\n", node->name);
            }
        }
        fflush(stdout);
    }
}

static int poll_for_replies(int fd, const struct node_config *node,
                            int milliseconds)
{
    struct pollfd pfd;
    int elapsed = 0;

    pfd.fd = fd;
    pfd.events = POLLIN;

    while (elapsed < milliseconds) {
        int rc = poll(&pfd, 1, 500);

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

        if (pfd.revents & POLLIN) {
            uint8_t src_mac[MAC_ADDR_LEN];
            struct cmdu_message msg;

            if (receive_one(fd, src_mac, &msg) == 0) {
                if (memcmp(src_mac, node->al_mac, MAC_ADDR_LEN) == 0) {
                    continue;
                }
                print_rx(node, src_mac, &msg);

                if (msg.msg_type == MSG_TOPOLOGY_QUERY ||
                    msg.msg_type == MSG_TOPOLOGY_NOTIFICATION) {
                    send_built_cmdu(fd, node, src_mac,
                                    cmdu_build_topology_response, msg.msg_id);
                } else if (msg.msg_type == MSG_LINK_METRIC_QUERY) {
                    send_built_cmdu(fd, node, src_mac,
                                    cmdu_build_link_metric_response, msg.msg_id);
                }
            }
        }
    }

    return 0;
}

int run_agent(const struct node_config *node, int once)
{
    int fd = raw_socket_open(node->ifname);
    uint16_t msg_id = 1000;

    if (fd < 0) {
        perror("raw_socket_open");
        return 1;
    }

    printf("[%s] started on %s, AL MAC ready\n", node->name, node->ifname);

    do {
        sleep(3);
        printf("[%s] TX Discovery\n", node->name);
        send_built_cmdu(fd, node, multicast_mac,
                        cmdu_build_discovery, msg_id++);

        printf("[%s] TX Topology Query\n", node->name);
        send_built_cmdu(fd, node, multicast_mac,
                        cmdu_build_topology_query, msg_id++);

        printf("[%s] TX Topology Notification\n", node->name);
        send_built_cmdu(fd, node, multicast_mac,
                        cmdu_build_topology_notification, msg_id++);

        printf("[%s] TX Link Metric Query\n", node->name);
        send_built_cmdu(fd, node, multicast_mac,
                        cmdu_build_link_metric_query, msg_id++);

        fflush(stdout);
        poll_for_replies(fd, node, once ? 3000 : 5000);
    } while (!once);

    close(fd);
    return 0;
}
