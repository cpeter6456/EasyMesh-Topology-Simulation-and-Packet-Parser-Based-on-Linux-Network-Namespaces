#ifndef RAW_SOCKET_H
#define RAW_SOCKET_H

#include "easymesh_defs.h"

#include <sys/types.h>

int raw_socket_open(const char *ifname);
int raw_socket_send(int fd, const char *ifname,
                    const uint8_t dst_mac[MAC_ADDR_LEN],
                    const uint8_t *frame, size_t frame_len);
ssize_t raw_socket_recv(int fd, uint8_t *buf, size_t buf_len);

#endif
