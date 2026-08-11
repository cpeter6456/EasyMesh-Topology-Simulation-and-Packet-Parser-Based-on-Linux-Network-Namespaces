#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${ROOT_DIR}/bin"
LOG_DIR="${ROOT_DIR}/logs"
CAPTURE_DIR="${ROOT_DIR}/captures"
RSSI_DIR="${ROOT_DIR}/runtime/rssi"
ETHERTYPE_FILTER="ether proto 0x893a"

need_root() {
  if [ "$EUID" -ne 0 ]; then
    echo "請使用 sudo 執行此命令"
    exit 1
  fi
}

cleanup_netns() {
  ip netns pids Controller 2>/dev/null | xargs -r kill 2>/dev/null || true
  ip netns pids Agent_1 2>/dev/null | xargs -r kill 2>/dev/null || true
  ip netns pids Agent_2 2>/dev/null | xargs -r kill 2>/dev/null || true
  ip netns del Controller 2>/dev/null || true
  ip netns del Agent_1 2>/dev/null || true
  ip netns del Agent_2 2>/dev/null || true
}

setup_topology() {
  need_root
  mkdir -p "$LOG_DIR" "$CAPTURE_DIR" "$RSSI_DIR"

  printf '%s\n' -35 >"${RSSI_DIR}/controller-from-agent1.rssi"
  printf '%s\n' -40 >"${RSSI_DIR}/controller-from-agent2.rssi"
  printf '%s\n' -50 >"${RSSI_DIR}/agent1-from-controller.rssi"
  printf '%s\n' -55 >"${RSSI_DIR}/agent1-from-agent2.rssi"
  printf '%s\n' -65 >"${RSSI_DIR}/agent2-from-controller.rssi"
  printf '%s\n' -60 >"${RSSI_DIR}/agent2-from-agent1.rssi"

  echo "[1/4] 清理舊的 Network Namespace..."
  cleanup_netns

  echo "[2/4] 建立 Controller、Agent_1、Agent_2..."
  ip netns add Controller
  ip netns add Agent_1
  ip netns add Agent_2

  echo "[3/4] 建立 veth pair..."
  ip link add c_to_a1 type veth peer name a1_to_c
  ip link add c_to_a2 type veth peer name a2_to_c
  ip link add a1_to_a2 type veth peer name a2_to_a1

  ip link set c_to_a1 netns Controller
  ip link set c_to_a2 netns Controller
  ip link set a1_to_c netns Agent_1
  ip link set a1_to_a2 netns Agent_1
  ip link set a2_to_c netns Agent_2
  ip link set a2_to_a1 netns Agent_2

  echo "[4/4] 配置 MAC、Bridge 與介面狀態..."
  ip netns exec Controller ip link set lo up
  ip netns exec Controller ip link set c_to_a1 address 02:00:00:00:00:01
  ip netns exec Controller ip link set c_to_a1 up
  ip netns exec Controller ip link set c_to_a2 up

  ip netns exec Agent_2 ip link set lo up
  ip netns exec Agent_2 ip link set a2_to_a1 address 02:00:00:00:00:03
  ip netns exec Agent_2 ip link set a2_to_c up
  ip netns exec Agent_2 ip link set a2_to_a1 up

  ip netns exec Agent_1 ip link set lo up
  ip netns exec Agent_1 ip link set a1_to_c up
  ip netns exec Agent_1 ip link set a1_to_a2 up

  echo "EasyMesh Lab Ready:"
  echo "  Controller c_to_a1/c_to_a2  02:00:00:00:00:01"
  echo "  Agent_1    a1_to_c/a1_to_a2 02:00:00:00:00:02"
  echo "  Agent_2    a2_to_c/a2_to_a1 02:00:00:00:00:03"
}

ensure_binaries() {
  if [ ! -x "${BIN_DIR}/easymesh-controller" ] || [ ! -x "${BIN_DIR}/easymesh-agent" ]; then
    echo "找不到可執行檔，先執行 make"
    make -C "$ROOT_DIR"
  fi
}

start_nodes() {
  need_root
  ensure_binaries
  mkdir -p "$LOG_DIR" "$RSSI_DIR"

  echo "啟動 Controller 與兩個 Agent..."
  ip netns exec Controller "${BIN_DIR}/easymesh-controller" \
    --rssi-link-file agent1 "${RSSI_DIR}/controller-from-agent1.rssi" \
    --rssi-link-file agent2 "${RSSI_DIR}/controller-from-agent2.rssi" >"${LOG_DIR}/controller.log" 2>&1 &
  echo $! >"${LOG_DIR}/controller.pid"

  ip netns exec Agent_1 "${BIN_DIR}/easymesh-agent" agent1 \
    --rssi-link-file controller "${RSSI_DIR}/agent1-from-controller.rssi" \
    --rssi-link-file agent2 "${RSSI_DIR}/agent1-from-agent2.rssi" >"${LOG_DIR}/agent1.log" 2>&1 &
  echo $! >"${LOG_DIR}/agent1.pid"

  ip netns exec Agent_2 "${BIN_DIR}/easymesh-agent" agent2 \
    --rssi-link-file controller "${RSSI_DIR}/agent2-from-controller.rssi" \
    --rssi-link-file agent1 "${RSSI_DIR}/agent2-from-agent1.rssi" >"${LOG_DIR}/agent2.log" 2>&1 &
  echo $! >"${LOG_DIR}/agent2.pid"

  echo "完成。Log:"
  echo "  ${LOG_DIR}/controller.log"
  echo "  ${LOG_DIR}/agent1.log"
  echo "  ${LOG_DIR}/agent2.log"
}

stop_nodes() {
  need_root
  for pid_file in "${LOG_DIR}"/*.pid; do
    [ -e "$pid_file" ] || continue
    kill "$(cat "$pid_file")" 2>/dev/null || true
    rm -f "$pid_file"
  done
}

capture_packets() {
  need_root
  mkdir -p "$CAPTURE_DIR"
  local out_c_to_a1="${CAPTURE_DIR}/ieee1905_$(date +%Y%m%d_%H%M%S)_c_to_a1.pcap"
  local out_c_to_a2="${CAPTURE_DIR}/ieee1905_$(date +%Y%m%d_%H%M%S)_c_to_a2.pcap"
  local out_a1_to_a2="${CAPTURE_DIR}/ieee1905_$(date +%Y%m%d_%H%M%S)_a1_to_a2.pcap"
  echo "擷取 Controller namespace 內 c_to_a1 上的 IEEE 1905.1 封包..."
  echo "輸出: $out_c_to_a1"
  ip netns exec Controller tcpdump -U -i c_to_a1 -w "$out_c_to_a1" "$ETHERTYPE_FILTER" &

  echo "擷取 Controller namespace 內 c_to_a2 上的 IEEE 1905.1 封包..."
  echo "輸出: $out_c_to_a2"
  ip netns exec Controller tcpdump -U -i c_to_a2 -w "$out_c_to_a2" "$ETHERTYPE_FILTER" &

  echo "擷取 Agent_1 namespace 內 a1_to_a2 上的 IEEE 1905.1 封包..."
  echo "輸出: $out_a1_to_a2"
  ip netns exec Agent_1 tcpdump -U -i a1_to_a2 -w "$out_a1_to_a2" "$ETHERTYPE_FILTER" &

}

show_status() {
  need_root
  ip netns list
  echo
  ip netns exec Controller ip link show 2>/dev/null || true
  ip netns exec Agent_1 ip link show 2>/dev/null || true
  ip netns exec Agent_2 ip link show 2>/dev/null || true
}

usage() {
  cat <<USAGE
Usage: sudo $0 <command>

Commands:
  setup     建立 Network Namespace / veth / bridge / MAC
  start     啟動 Controller 與 Agent_1/Agent_2
  stop      停止背景節點程式
  capture   使用 tcpdump 擷取 EtherType 0x893A
  status    顯示 namespace 與 link 狀態
  clean     停止程式並清除 namespace
  all       setup 後啟動全部節點
USAGE
}

case "${1:-setup}" in
  setup)
    setup_topology
    ;;
  start)
    start_nodes
    ;;
  stop)
    stop_nodes
    ;;
  capture)
    capture_packets
    ;;
  status)
    show_status
    ;;
  clean)
    stop_nodes
    cleanup_netns
    ;;
  all)
    setup_topology
    start_nodes
    ;;
  *)
    usage
    exit 1
    ;;
esac
