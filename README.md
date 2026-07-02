# EasyMesh-Topology-Simulation-and-Packet-Parser-Based-on-Linux-Network-Namespaces
基於 Linux 命名空間（Network Namespaces）的 EasyMesh 拓撲模擬與封包解析器
## 🌐 模擬網路拓撲架構 (Network Topology)

本專案在單一 Linux 環境下，利用 `Network Namespaces` 與 `veth pair` 模擬出一個三節點的 EasyMesh 鏈狀拓撲（Chain Topology），各節點之間皆為純二層（Layer 2）連線。
veth（Virtual Ethernet，虛擬乙太網

如
在  setup_mesh_env.sh 腳本中，這行指令就是在製造網路線
ip link add c_to_a1 type veth peer name a1_to_c

這裡製造了一條網路線，兩端的名稱分別叫 c_to_a1 和 a1_to_c

隨後，腳本用這兩行指令把網路線的兩端分別插進不同的房間：

ip link set c_to_a1 netns Controller

ip link set a1_to_c netns Agent_1

```mermaid
graph TD
    subgraph Controller_NS [Namespace: Controller]
        C[Controller 節點] -->|c_to_a1<br>02:00:00:00:00:01| V1(veth)
    end

    subgraph Agent1_NS [Namespace: Agent_1 / 客廳]
        V2(veth) -->|a1_to_c| BR[Linux Bridge: br-agent1<br>02:00:00:00:00:02]
        BR -->|a1_to_a2| V3(veth)
    end

    subgraph Agent2_NS [Namespace: Agent_2 / 房間]
        V4(veth) -->|a2_to_a1<br>02:00:00:00:00:03| A2[Agent_2 節點]
    end

    V1 === V2
    V3 === V4

    style BR fill:#f9f,stroke:#333,stroke-width:2px
    style C fill:#bbf,stroke:#333,stroke-width:2px
    style A2 fill:#bbf,stroke:#333,stroke-width:2px
