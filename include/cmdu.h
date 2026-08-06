#ifndef CMDU_H
#define CMDU_H

#include "easymesh_defs.h"

int cmdu_begin(uint8_t *buf, size_t buf_len, size_t *offset,
               uint16_t msg_type, uint16_t msg_id);
int cmdu_parse(const uint8_t *buf, size_t len, struct cmdu_message *msg);
int cmdu_build_discovery(uint8_t *buf, size_t buf_len, size_t *len,
                         uint16_t msg_id, const struct node_config *node);
int cmdu_build_topology_query(uint8_t *buf, size_t buf_len, size_t *len,
                              uint16_t msg_id, const struct node_config *node);
int cmdu_build_topology_notification(uint8_t *buf, size_t buf_len, size_t *len,
                                     uint16_t msg_id, const struct node_config *node);
int cmdu_build_topology_response(uint8_t *buf, size_t buf_len, size_t *len,
                                 uint16_t msg_id, const struct node_config *node);
int cmdu_build_link_metric_query(uint8_t *buf, size_t buf_len, size_t *len,
                                 uint16_t msg_id, const struct node_config *node);
int cmdu_build_link_metric_response(uint8_t *buf, size_t buf_len, size_t *len,
                                    uint16_t msg_id, const struct node_config *node);
void cmdu_print(const struct cmdu_message *msg);

#endif
