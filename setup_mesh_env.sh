#!/bin/bash

# 確保以 root 權限執行
if [ "$EUID" -ne 0 ]; then
  echo "請使用 sudo 執行此腳本！"
  exit 1
fi


echo "=================================================="
# 1. 清理舊環境（防止重複執行出錯）
echo "[1/4] 清理舊的 Network Namespaces 與 Bridge..."
ip netns del Controller 2>/dev/null
ip netns del Agent_1    2>/dev/null
ip netns del Agent_2    2>/dev/null
ip link del br-agent1   2>/dev/null
echo "清理完成。"

# 2. 建立三個獨立的 Network Namespaces
echo "[2/4] 建立 Network Namespaces (Controller, Agent_1, Agent_2)..."
ip netns add Controller
ip netns add Agent_1
ip netns add Agent_2

# 3. 建立 veth pair（虛擬網路線）
# c_to_a1 / a1_to_c  : 連接 Controller 與 Agent_1
# a1_to_a2 / a2_to_a1: 連接 Agent_1 與 Agent_2
echo "[3/4] 建立 veth pair 並綁定到對應的 Namespace..."
ip link add c_to_a1 type veth peer name a1_to_c
ip link add a1_to_a2 type veth peer name a2_to_a1

# 將網卡塞入對應的 Namespace 中
ip link set c_to_a1 netns Controller
ip link set a1_to_c netns Agent_1
ip link set a1_to_a2 netns Agent_1
ip link set a2_to_a1 netns Agent_2

# 4. 配置各節點網路與 MAC 位址
echo "[4/4] 啟用介面並配置模擬 MAC 位址..."

# --- Controller 設定 ---
ip netns exec Controller ip link set lo up
ip netns exec Controller ip link set c_to_a1 address 02:00:00:00:00:01
ip netns exec Controller ip link set c_to_a1 up

# --- Agent_2 設定 ---
ip netns exec Agent_2 ip link set lo up
ip netns exec Agent_2 ip link set a2_to_a1 address 02:00:00:00:00:03
ip netns exec Agent_2 ip link set a2_to_a1 up

# --- Agent_1 設定 (關鍵：建立 Linux Bridge 來橋接兩端) ---
ip netns exec Agent_1 ip link set lo up
ip netns exec Agent_1 ip link set a1_to_c up
ip netns exec Agent_1 ip link set a1_to_a2 up

# 在 Agent_1 內部建立一個 Bridge 叫 br-agent1
ip netns exec Agent_1 ip link add br-agent1 type bridge
ip netns exec Agent_1 ip link set br-agent1 address 02:00:00:00:00:02
ip netns exec Agent_1 ip link set br-agent1 up

# 將兩張網卡當成 Ports 塞進 Bridge 裡，實現二層導通
ip netns exec Agent_1 ip link set a1_to_c master br-agent1
ip netns exec Agent_1 ip link set a1_to_a2 master br-agent1

echo "=================================================="
echo "EasyMesh 模擬環境部署成功！"
echo "鏈狀拓撲結構："
echo " [Controller] (02:00:00:00:00:01)"
echo "      │ (c_to_a1)"
echo " [Agent_1]    (02:00:00:00:00:02) <- 內建 Bridge 橋接兩端"
echo "      │ (a1_to_a2)"
echo " [Agent_2]    (02:00:00:00:00:03)"
echo "=================================================="