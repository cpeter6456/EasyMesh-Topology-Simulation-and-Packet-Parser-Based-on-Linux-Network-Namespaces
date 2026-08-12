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
#include <time.h>
#include <unistd.h>

static const uint8_t multicast_mac[MAC_ADDR_LEN] = IEEE1905_MULTICAST_MAC;

static int read_rssi_file(const char *path, int32_t *rssi_dbm);

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
        copy_mac(node->al_mac, "02:00:00:00:00:01");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:02");
        node->link_rssi_dbm[0] = -35;
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:03");
        node->link_rssi_dbm[1] = -40;
        return 0;
    }

    if (strcmp(name, "agent1") == 0) {
        node->name = "Agent_1";
        node->ifnames[0] = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "br-agent1";
        node->interface_count = 1;
        node->role = ROLE_AGENT;
        copy_mac(node->al_mac, "02:00:00:00:00:02");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:01");
        node->link_rssi_dbm[0] = -50;
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:03");
        node->link_rssi_dbm[1] = -55;
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:04");
        node->link_rssi_dbm[2] = -58;
        return 0;
    }

    if (strcmp(name, "agent2") == 0) {
        node->name = "Agent_2";
        node->ifnames[0] = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "a2_to_c";
        node->ifnames[1] = "a2_to_a1";
        node->interface_count = 2;
        node->role = ROLE_AGENT;
        copy_mac(node->al_mac, "02:00:00:00:00:03");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:01");
        node->link_rssi_dbm[0] = -65;
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:02");
        node->link_rssi_dbm[1] = -60;
        return 0;
    }

    if (strcmp(name, "agent3") == 0) {
        node->name = "Agent_3";
        node->ifnames[0] = override_ifname != NULL && *override_ifname != '\0' ? override_ifname : "a3_to_a1";
        node->interface_count = 1;
        node->role = ROLE_AGENT;
        copy_mac(node->al_mac, "02:00:00:00:00:04");
        copy_mac(node->neighbors[node->neighbor_count++], "02:00:00:00:00:02");
        node->link_rssi_dbm[0] = -62;
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

static int receive_one(int fd, uint8_t frame[MAX_CMDU_SIZE],
                       uint8_t src_mac[MAC_ADDR_LEN],
                       struct cmdu_message *msg)
{
    uint8_t dst_mac[MAC_ADDR_LEN];
    const uint8_t *payload;
    size_t payload_len;
    ssize_t n = raw_socket_recv(fd, frame, MAX_CMDU_SIZE);

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
    static const uint8_t agent3[] = {0x02, 0, 0, 0, 0, 4};

    if (memcmp(mac, controller, MAC_ADDR_LEN) == 0) return "Controller";
    if (memcmp(mac, agent1, MAC_ADDR_LEN) == 0) return "Agent_1";
    if (memcmp(mac, agent2, MAC_ADDR_LEN) == 0) return "Agent_2";
    if (memcmp(mac, agent3, MAC_ADDR_LEN) == 0) return "Agent_3";
    return "Unknown";
}

static int link_status_changed(struct link_status statuses[MAX_LINK_STATUS],
                               const uint8_t reporter[MAC_ADDR_LEN],
                               const uint8_t neighbor[MAC_ADDR_LEN], int32_t rssi)
{
    for (size_t i = 0; i < MAX_LINK_STATUS; i++) {
        if (statuses[i].valid && memcmp(statuses[i].reporter, reporter, MAC_ADDR_LEN) == 0 &&
            memcmp(statuses[i].neighbor, neighbor, MAC_ADDR_LEN) == 0) {
            if (statuses[i].rssi_dbm == rssi) return 0;
            statuses[i].rssi_dbm = rssi;
            return 1;
        }
        if (!statuses[i].valid) {
            memcpy(statuses[i].reporter, reporter, MAC_ADDR_LEN);
            memcpy(statuses[i].neighbor, neighbor, MAC_ADDR_LEN);
            statuses[i].rssi_dbm = rssi;
            statuses[i].valid = 1;
            return 1;
        }
    }
    return 0;
}

static void print_link_status(struct link_status statuses[MAX_LINK_STATUS],
                              const uint8_t reporter[MAC_ADDR_LEN],
                              const struct cmdu_message *msg)
{
    for (size_t i = 0; i < msg->tlv_count; i++) {
        const struct tlv_view *tlv = &msg->tlvs[i];
        int32_t rssi_dbm;

        if (tlv->type != TLV_RECEIVER_LINK_METRIC || tlv->length != 35) continue;
        rssi_dbm = (int8_t)tlv->value[34];
        if (link_status_changed(statuses, reporter, tlv->value + MAC_ADDR_LEN, rssi_dbm)) {
            printf("[Mesh status] %s RX from %s: RSSI %d dBm (updated)\n",
                   node_name_for_mac(reporter),
                   node_name_for_mac(tlv->value + MAC_ADDR_LEN), rssi_dbm);
        }
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

static void refresh_link_rssi(struct node_config *node)
{
    for (size_t i = 0; i < node->neighbor_count; i++) {
        int32_t file_rssi;
        if (node->link_rssi_files[i] != NULL &&
            read_rssi_file(node->link_rssi_files[i], &file_rssi) == 0) {
            node->link_rssi_dbm[i] = file_rssi;
        }
    }
}

static void print_local_link_status(const struct node_config *node, size_t index)
{
    printf("[Mesh status] %s RX from %s: RSSI %d dBm (updated)\n",
           node->name, node_name_for_mac(node->neighbors[index]),
           node->link_rssi_dbm[index]);
}

int run_controller(struct node_config *node)
{
    int fds[MAX_INTERFACES];
    struct link_status statuses[MAX_LINK_STATUS] = {0};
    int32_t last_reported_rssi[MAX_NEIGHBORS];
    time_t next_metric_query;
    uint16_t msg_id = 1;

    if (open_interfaces(node, fds) < 0) {
        perror("raw_socket_open");
        return 1;
    }

    printf("[%s] listening on %s and %s for EtherType 0x%04X\n",
           node->name, node->ifnames[0], node->ifnames[1], IEEE1905_ETHERTYPE);
    refresh_link_rssi(node);
    memcpy(last_reported_rssi, node->link_rssi_dbm, sizeof(last_reported_rssi));
    for (size_t i = 0; i < node->neighbor_count; i++) print_local_link_status(node, i);
    send_built_cmdu_all(fds, node, cmdu_build_link_metric_query, msg_id++);
    next_metric_query = time(NULL) + 15;
    fflush(stdout);

    for (;;) {
        struct pollfd pfds[MAX_INTERFACES];
        for (size_t i = 0; i < node->interface_count; i++) {
            pfds[i].fd = fds[i];
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
        }
        if (poll(pfds, node->interface_count, 1000) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (size_t i = 0; i < node->interface_count; i++) {
            uint8_t frame[MAX_CMDU_SIZE];
            uint8_t src_mac[MAC_ADDR_LEN];
            struct cmdu_message msg;
            if (!(pfds[i].revents & POLLIN) || receive_one(fds[i], frame, src_mac, &msg) < 0 ||
                memcmp(src_mac, node->al_mac, MAC_ADDR_LEN) == 0) continue;
            if (msg.msg_type == MSG_LINK_METRIC_RESPONSE) {
                print_link_status(statuses, src_mac, &msg);
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
        refresh_link_rssi(node);
        time_t now = time(NULL);
        int should_report = 0;
        for (size_t i = 0; i < node->neighbor_count; i++) {
            if (abs(node->link_rssi_dbm[i] - last_reported_rssi[i]) >= 5) {
                print_local_link_status(node, i);
                should_report = 1;
            }
        }
        if (should_report) {
            memcpy(last_reported_rssi, node->link_rssi_dbm, sizeof(last_reported_rssi));
        }
        if (now >= next_metric_query) {
            send_built_cmdu_all(fds, node, cmdu_build_link_metric_query, msg_id++);
            next_metric_query = now + 15;
        }
        fflush(stdout);
    }

    for (size_t i = 0; i < node->interface_count; i++) close(fds[i]);
    return 1;
}

static int poll_for_replies(const int fds[MAX_INTERFACES], struct node_config *node,
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
            uint8_t frame[MAX_CMDU_SIZE];
            uint8_t src_mac[MAC_ADDR_LEN];
            struct cmdu_message msg;

            if ((pfds[i].revents & POLLIN) && receive_one(fds[i], frame, src_mac, &msg) == 0) {
                if (memcmp(src_mac, node->al_mac, MAC_ADDR_LEN) == 0) {
                    continue;
                }
                print_rx(node, src_mac, &msg);

                if (msg.msg_type == MSG_TOPOLOGY_QUERY ||
                    msg.msg_type == MSG_TOPOLOGY_NOTIFICATION) {
                    send_built_cmdu(fds[i], node, node->ifnames[i], src_mac,
                                    cmdu_build_topology_response, msg.msg_id);
                } else if (msg.msg_type == MSG_LINK_METRIC_QUERY) {
                    refresh_link_rssi(node);
                    send_built_cmdu(fds[i], node, node->ifnames[i], src_mac,
                                    cmdu_build_link_metric_response, msg.msg_id);
                }
            }
        }
    }

    return 0;
}

static int read_rssi_file(const char *path, int32_t *rssi_dbm)
{
    char line[32];
    char *end = NULL;
    long value;
    FILE *file = fopen(path, "r");

    if (file == NULL) return -1;
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);
    value = strtol(line, &end, 10);
    if (line == end || (*end != '\n' && *end != '\0') || value < -127 || value > 127) return -1;
    *rssi_dbm = (int32_t)value;
    return 0;
}

int run_agent(struct node_config *node, int once)
{
    int fds[MAX_INTERFACES];
    uint16_t msg_id = 1000;

    if (open_interfaces(node, fds) < 0) {
        perror("raw_socket_open");
        return 1;
    }

    refresh_link_rssi(node);

    printf("[%s] started on", node->name);
    for (size_t i = 0; i < node->interface_count; i++) {
        printf("%s%s", i == 0 ? " " : ", ", node->ifnames[i]);
    }
    printf(", AL MAC ready\n");

    /* One complete topology synchronization occurs only when the Agent starts. */
    send_built_cmdu_all(fds, node, cmdu_build_discovery, msg_id++);
    send_built_cmdu_all(fds, node, cmdu_build_topology_query, msg_id++);
    send_built_cmdu_all(fds, node, cmdu_build_topology_notification, msg_id++);

    do {
        poll_for_replies(fds, node, once ? 3000 : 1000);
        if (once) break;
    } while (!once);

    for (size_t i = 0; i < node->interface_count; i++) close(fds[i]);
    return 0;
}
