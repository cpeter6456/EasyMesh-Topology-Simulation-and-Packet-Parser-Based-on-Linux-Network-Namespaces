#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s agent1|agent2|agent3 [--once] [--rssi-link <neighbor> <dbm>] [--rssi-link-file <neighbor> <path>]\n", prog);
}

static int parse_rssi(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    parsed = strtol(text, &end, 10);
    if (text == end || *end != '\0') {
        return -1;
    }

    if (parsed < -127 || parsed > 127) {
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static int link_index_for_name(const struct node_config *node, const char *name)
{
    uint8_t last_byte;

    if (strcmp(name, "controller") == 0) last_byte = 1;
    else if (strcmp(name, "agent1") == 0) last_byte = 2;
    else if (strcmp(name, "agent2") == 0) last_byte = 3;
    else if (strcmp(name, "agent3") == 0) last_byte = 4;
    else return -1;

    for (size_t i = 0; i < node->neighbor_count; i++) {
        if (node->neighbors[i][MAC_ADDR_LEN - 1] == last_byte) return (int)i;
    }
    return -1;
}

int main(int argc, char **argv)
{
    struct node_config node;
    int once = 0;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (node_config_from_name(argv[1], &node) < 0) {
        usage(argv[0]);
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[i], "--rssi-link") == 0) {
            int link_index;
            int link_rssi;
            if (i + 2 >= argc || (link_index = link_index_for_name(&node, argv[++i])) < 0 ||
                parse_rssi(argv[++i], &link_rssi) < 0) {
                usage(argv[0]);
                return 1;
            }
            node.link_rssi_dbm[link_index] = link_rssi;
        } else if (strcmp(argv[i], "--rssi-link-file") == 0) {
            int link_index;
            if (i + 2 >= argc || (link_index = link_index_for_name(&node, argv[++i])) < 0) {
                usage(argv[0]);
                return 1;
            }
            node.link_rssi_files[link_index] = argv[++i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    return run_agent(&node, once);
}
