#include "app.h"

#include <stdio.h>

int main(void)
{
    struct node_config node;

    if (node_config_from_name("controller", &node) < 0) {
        fprintf(stderr, "failed to load controller config\n");
        return 1;
    }

    return run_controller(&node);
}
