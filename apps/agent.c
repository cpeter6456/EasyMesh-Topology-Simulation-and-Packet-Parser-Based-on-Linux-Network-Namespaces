#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s agent1|agent2 [--once] [--rssi <dbm>] [--rssi-file <path>]\n", prog);
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

int main(int argc, char **argv)
{
    struct node_config node;
    int once = 0;
    int rssi = -1;
    const char *env_rssi = getenv("EASYMESH_RSSI");

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[i], "--rssi") == 0) {
            if (i + 1 >= argc || parse_rssi(argv[++i], &rssi) < 0) {
                usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--rssi-file") == 0) {
            if (i + 1 >= argc || setenv("EASYMESH_RSSI_FILE", argv[++i], 1) < 0) {
                usage(argv[0]);
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (node_config_from_name(argv[1], &node) < 0) {
        usage(argv[0]);
        return 1;
    }

    if (rssi < 0 && env_rssi != NULL && parse_rssi(env_rssi, &rssi) < 0) {
        fprintf(stderr, "invalid EASYMESH_RSSI value: %s\n", env_rssi);
        return 1;
    }

    if (rssi >= 0) {
        node.rssi_dbm = rssi;
        printf("[%s] configured RSSI %d dBm\n", argv[1], node.rssi_dbm);
    }

    return run_agent(&node, once);
}
