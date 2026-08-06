#ifndef EASYMESH_APP_H
#define EASYMESH_APP_H

#include "easymesh_defs.h"

int run_controller(const struct node_config *node);
int run_agent(const struct node_config *node, int once);
int node_config_from_name(const char *name, struct node_config *node);

#endif
