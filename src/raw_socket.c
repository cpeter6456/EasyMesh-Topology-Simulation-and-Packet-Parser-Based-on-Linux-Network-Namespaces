#include "raw_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int ifindex_for(int fd, const char *ifname)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        return -1;
    }

    return ifr.ifr_ifindex;
}

int raw_socket_open(const char *ifname)
{
    int fd = socket(AF_PACKET, SOCK_RAW, htons(IEEE1905_ETHERTYPE));
    struct sockaddr_ll addr;
    int ifindex;

    if (fd < 0) {
        return -1;
    }

    ifindex = ifindex_for(fd, ifname);
    if (ifindex < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(IEEE1905_ETHERTYPE);
    addr.sll_ifindex = ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int raw_socket_send(int fd, const char *ifname,
                    const uint8_t dst_mac[MAC_ADDR_LEN],
                    const uint8_t *frame, size_t frame_len)
{
    struct sockaddr_ll addr;
    int ifindex = ifindex_for(fd, ifname);

    if (ifindex < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(IEEE1905_ETHERTYPE);
    addr.sll_ifindex = ifindex;
    addr.sll_halen = MAC_ADDR_LEN;
    memcpy(addr.sll_addr, dst_mac, MAC_ADDR_LEN);

    return sendto(fd, frame, frame_len, 0,
                  (struct sockaddr *)&addr, sizeof(addr)) == (ssize_t)frame_len
               ? 0
               : -1;
}

ssize_t raw_socket_recv(int fd, uint8_t *buf, size_t buf_len)
{
    return recvfrom(fd, buf, buf_len, 0, NULL, NULL);
}
