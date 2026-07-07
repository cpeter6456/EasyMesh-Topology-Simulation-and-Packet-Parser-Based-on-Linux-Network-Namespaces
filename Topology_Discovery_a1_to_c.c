#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <sys/ioctl.h>

#define ETH_P_1905 0x893A
#define IF_NAME "a1_to_c"

// 強制編譯器對齊 1 Byte，避免記憶體自動補零（Padding）
#pragma pack(push, 1)

// 1905.1 CMDU Header
struct ieee1905_cmdu {
    uint16_t msg_type;
    uint16_t msg_id;
    uint8_t  frag_id;
    uint8_t  flags;
    uint16_t reserved;
};

// TLV 固定標頭
struct tlv_header {
    uint8_t  type;
    uint16_t length;
};
#pragma pack(pop)

int main() {
    int sock_fd;
    struct sockaddr_ll socket_address;
    struct ifreq ifr;
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    // 1. 建立二層 Raw Socket
    if ((sock_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_1905))) < 0) {
        perror("Socket 建立失敗");
        return -1;
    }

    // 2. 獲取網卡介面索引 (Interface Index)
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, IF_NAME, IFNAMSIZ - 1);
    if (ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("獲取網卡 Index 失敗");
        close(sock_fd);
        return -1;
    }

    // 3. 組裝乙太網標頭 (Ethernet Header)
    struct ethhdr *eth = (struct ethhdr *)buffer;
    // 目的 MAC 位址：1905.1 多播通常是 01:80:C2:00:00:13，這裡先用多播
    eth->h_dest[0] = 0x01; eth->h_dest[1] = 0x80; eth->h_dest[2] = 0xC2;
    eth->h_dest[3] = 0x00; eth->h_dest[4] = 0x00; eth->h_dest[5] = 0x13;
    // 來源 MAC 位址：Agent_1 (02:00:00:00:00:02)
    eth->h_source[0] = 0x02; eth->h_source[1] = 0x00; eth->h_source[2] = 0x00;
    eth->h_source[3] = 0x00; eth->h_source[4] = 0x00; eth->h_source[5] = 0x02;
    eth->h_proto = htons(ETH_P_1905);

    int offset = sizeof(struct ethhdr);

    // 4. 組裝 1905.1 CMDU Header
    struct ieee1905_cmdu *cmdu = (struct ieee1905_cmdu *)(buffer + offset);
    cmdu->msg_type = htons(0x0001); // Topology Discovery Message
    cmdu->msg_id = htons(1001);
    cmdu->frag_id = 0;
    cmdu->flags = 0;
    offset += sizeof(struct ieee1905_cmdu);

    // 5. 組裝 AL MAC Address TLV (Type = 1, Length = 6)
    struct tlv_header *tlv1 = (struct tlv_header *)(buffer + offset);
    tlv1->type = 1; 
    tlv1->length = htons(6);
    offset += sizeof(struct tlv_header);
    // 寫入 AL MAC (這裡同樣設為 Agent_1 的管理 MAC)
    uint8_t al_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    memcpy(buffer + offset, al_mac, 6);
    offset += 6;

    // 6. 組裝 SupportedRole TLV (Type = 2, Length = 1, Value: 1=Registrar/Controller, 2=Enrollee/Agent)
    struct tlv_header *tlv2 = (struct tlv_header *)(buffer + offset);
    tlv2->type = 2;
    tlv2->length = htons(1);
    offset += sizeof(struct tlv_header);
    buffer[offset] = 2; // 2 代表這個節點是個 Agent
    offset += 1;

    // 7. 組裝 End of Message TLV (Type = 0, Length = 0)
    struct tlv_header *tlv_end = (struct tlv_header *)(buffer + offset);
    tlv_end->type = 0;
    tlv_end->length = 0;
    offset += sizeof(struct tlv_header);

    // 8. 設定 Socket 發送目標
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.sll_ifindex = ifr.ifr_ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, eth->h_dest, 6);

    // 發送封包
    printf("Agent_1:正在發送 1905.1 Topology Discovery 封包...\n");
    if (sendto(sock_fd, buffer, offset, 0, (struct sockaddr*)&socket_address, sizeof(socket_address)) < 0) {
        perror("發送失敗");
    } else {
        printf("發送成功！總長度: %d 遞進位組\n", offset);
    }

    close(sock_fd);
    return 0;
}