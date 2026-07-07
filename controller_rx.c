#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/ethernet.h>

#define ETH_P_1905 0x893A

#pragma pack(push, 1)
struct ieee1905_cmdu {
    uint16_t msg_type;
    uint16_t msg_id;
    uint8_t  frag_id;
    uint8_t  flags;
    uint16_t reserved;
};

struct tlv_header {
    uint8_t  type;
    uint16_t length;
};
#pragma pack(pop)

int main() {
    int sock_fd;
    uint8_t buffer[2048];

    // 建立監聽 0x893A 的 Raw Socket
    if ((sock_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_1905))) < 0) {
        perror("Socket 建立失敗");
        return -1;
    }

    printf("Controller：正在監聽 1905.1 (EasyMesh) 核心封包...\n");

    while (1) {
        ssize_t data_size = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, NULL);
        if (data_size < 0) {
            perror("接收錯誤");
            break;
        }

        // 1. 解析乙太網標頭
        struct ethhdr *eth = (struct ethhdr *)buffer;
        
        // 2. 跳過乙太網標頭，指向 1905.1 CMDU Header
        int offset = sizeof(struct ethhdr);
        struct ieee1905_cmdu *cmdu = (struct ieee1905_cmdu *)(buffer + offset);

        uint16_t msg_type = ntohs(cmdu->msg_type);
        if (msg_type == 0x0001) {
            printf("\n==============================================\n");
            printf("🔔 偵測到來自鄰居的 Topology Discovery 訊號！\n");
            printf("來源 MAC 位址: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   eth->h_source[0], eth->h_source[1], eth->h_source[2],
                   eth->h_source[3], eth->h_source[4], eth->h_source[5]);
            printf("1905 Msg ID: %d\n", ntohs(cmdu->msg_id));
            
            offset += sizeof(struct ieee1905_cmdu);

            // 3. 迴圈深度解析 TLV 鏈
            while (offset < data_size) {
                struct tlv_header *tlv = (struct tlv_header *)(buffer + offset);
                uint8_t tlv_type = tlv->type;
                uint16_t tlv_len = ntohs(tlv->length);

                offset += sizeof(struct tlv_header);

                // 遇到 End TLV (Type = 0) 就停止解析
                if (tlv_type == 0) {
                    printf("-> 抵達封包結尾 (End TLV)\n");
                    break;
                }

                // 依據 Type 處理數據
                if (tlv_type == 1) { // AL MAC Address TLV
                    printf("[TLV 0x01] AL MAC 位址: ");
                    for(int i=0; i<6; i++) {
                        printf("%02X%s", buffer[offset + i], (i==5) ? "" : ":");
                    }
                    printf("\n");
                } 
                else if (tlv_type == 2) { // SupportedRole TLV
                    uint8_t role = buffer[offset];
                    printf("[TLV 0x02] 設備角色: %d (%s)\n", role, (role == 2) ? "EasyMesh Agent" : "Controller");
                } 
                else {
                    printf("[TLV 未知] Type: %d, Length: %d\n", tlv_type, tlv_len);
                }

                // 游標推進到下一個 TLV 節點
                offset += tlv_len;
            }
            printf("==============================================\n");
        }
    }

    close(sock_fd);
    return 0;
}