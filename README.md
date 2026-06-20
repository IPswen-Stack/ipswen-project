# IPSWEN 项目 —— An Evolutionary Addressing Extension to the TCP/IPv4 Architecture

> *IPswen ― An Evolutionary Addressing Extension to the TCP/IPv4 architecture*
> *Welcome to the IPswen development team and technical forum!*
> —— [ipswen.net](https://ipswen.net)

---

## 概述

**IPswen**（中文可表述为"孙氏IP"）乃是一项针对 TCP/IPv4 体系架构的**可变长地址扩展方案（Variable Length Addressing Extension）**，其核心特征为：单栈实现（Single Stack Implementation），且与 IPv4 双向互通（Bidirectionally Interworkable with IPv4）。该方案之设计宗旨在于以增量可部署（Incrementally Deployable）且二进制向后兼容（Binary Backwards Compatible）——亦即"演进式"（Evolutionary）——之方式，支撑互联网 IPv4 部分之持续演进。

IPswen 之根本目标，乃是在 IPv4 之逻辑框架内寻获一个扩大化的 L3（跨网络层）地址空间，并尽可能多地保持与现存 IPv4 寻址与路由架构之兼容性。为达此目的，仅需对 IPv4 之 32 位地址空间施以修改（或称"打补丁"），而 TCP/IPv4 协议栈之其余一切均保持原封不动。其结果乃是一个**多层次 L3 地址空间（Multilevel L3 Address Space）**，由 8 个子空间（8 Subspaces）构成，其地址长度涵盖 4 字节至 11 字节之范围——此范围应足以支撑 IPv4 协议栈之长期演进。

何以名之曰"IPswen"？该名称源自早期开发中所用之实验代码，诸如 `struct ipswen_addr{}` 及其相关之扩展 IPv4 Socket API 数据结构与函数。最初，该项目曾被称为 `ipv4pp`（即"IPv4++"），随后极短暂地更名为 `ipv4x`（用以表示一新代码分支 IP4x/IPlee），然此后发现该名称与另一不相关之项目名称相巧合。自此以后，该项目正式更名为 IPswen，并代表 **IP**v4 Extended with the IP**swen** Addressing Extension Scheme 之含义。

### 项目总体定位

本工作空间（`ipswen-project/`）乃 IPswen 研究项目之实验代码仓库，包含一个综合性网络协议实验平台。该实验平台覆盖从 Linux 内核协议栈之底层数据结构修改、Netlink 路由套接字接口之扩展、BGP 路由协议之 SAFI 扩展、用户态网络工具之适配，直至应用层 Socket API 之 IP 选项实验与全球路由连通性测试之完整技术栈。整体项目构成一条从内核到应用、从数据平面到控制平面之垂直研究管线。

IPSWEN 项目整体由 **13 个子项目** 及 **1 个辅助脚本** 构成，各子项目分别对应网络协议栈之不同抽象层级。以下各节将对该项目之核心概念进行严谨阐述——其内容严格遵循 `ipswen.net` 官方网站之口径——继而对每一子目录进行穷尽式介绍，其内容大量源自各子项目自身之 README 文档，经适当改编以适配根级视角，且未缩减信息容量。

---

## IPswen 核心概念

### 可变长地址记法

IPswen 保留并沿袭了 IPv4 之"老派"（人类友好）点分十进制记法（Dotted Decimal Notation），用于 IPswen 地址之文本表示。与传统 IPv4 地址使用 4 段十进制数字（`A.B.C.D`）之记法有所不同，IPswen 地址在标准 IPv4 基地址（Base Address）之后，以小数点分隔，追加一个**级维度数字（Level Digit）**及可选之扩展字节段（Extension Bytes）。

IPswen 地址之通用记法如下：

$$\text{IPswen Address} = A.B.C.D.L.E_1.E_2.\ldots.E_n$$

其中：
- $A.B.C.D$ —— 标准 32 位 IPv4 基地址（Base Address），与现有 IPv4 基础设施完全兼容；
- $L$ —— 级维度（Level），乃一有符号整数，其取值范围为 $L \in \{-7, -6, \ldots, -1, 0, +1, \ldots, +7\}$，共计 15 个可能值，各自对应 8 种不同之地址长度（4 至 11 字节不等）；
- $E_1.E_2.\ldots.E_n$ —— 扩展地址字节序列，以小数点分隔，其字节数由 $|L|$ 确定（当 $L \neq 0$ 时，扩展字节数为 $|L|$；当 $L = 0$ 时，无扩展字节）。

### 8 个子空间与地址长度

IPswen 构建了一个多层次 L3 地址空间，由 **8 个子空间（8 Subspaces）** 构成，对应 **8 种地址长度**，自最短之 4 字节至最长之 11 字节。一级级维度之绝对值为该子空间中地址之扩展字节数：

| 级维度 $L$ | 地址之总长度（字节） | 子空间名称 | 备注 |
|:----------:|:-------------------:|:----------|:-----|
| 0 | 4 | Level 0 Subspace | 与标准 32 位 IPv4 一一映射 |
| $\pm$1 | 5 | Level $\pm$1 Subspace | 含 1 扩展字节 |
| $\pm$2 | 6 | Level $\pm$2 Subspace | 含 2 扩展字节 |
| $\pm$3 | 7 | Level $\pm$3 Subspace | 含 3 扩展字节 |
| $\pm$4 | 8 | Level $\pm$4 Subspace | 含 4 扩展字节 |
| $\pm$5 | 9 | Level $\pm$5 Subspace | 含 5 扩展字节 |
| $\pm$6 | 10 | Level $\pm$6 Subspace | 含 6 扩展字节 |
| $\pm$7 | 11 | Level $\pm$7 Subspace | 含 7 扩展字节 |

是故，最短（Level 0）之 IPswen 地址 `a.b.c.d.0` 仅具 5 个十进制数字（含级维度之小数点后数字），而最长之 IPswen 地址则具 12 个十进制数字，例如：`192.168.0.1.7.1.2.3.4.5.6.7`，此乃来自 Level 7 子空间之地址。

### 地址示例

以下示例均直接引自 `ipswen.net` 网站，以展示 IPswen 地址记法之具体形态：

1. **`1.2.3.4.0`** —— 来自 **Level 0 子空间** 之 IPswen 地址，依设计，与固定长度 32 位 IPv4 "基地址" `1.2.3.4` 一一映射。凡 Level 0 之地址即为标准 IPv4 地址，确保与未修改系统之完全互操作性。

2. **`1.2.3.4.1.5`** —— 来自 **Level 1 子空间** 之 40 位（5 字节）地址。其中 `1.2.3.4` 为基地址，`1` 为级维度（$L=+1$），`5` 为 1 字节之扩展地址段。

3. **`1.2.3.4.5.6.7.8.9.0/64`** —— 来自 **Level 5 子空间** 之地址块（一个 8 字节长之前缀），该块含有 $2^8 = 256$ 个地址。其中 `1.2.3.4` 为基地址，`5` 为级维度（$L=+5$），`6.7.8.9.0` 为 5 字节之扩展地址段。

4. **`0.0.0.0.-7.0`**（或简记作 `0.0.0.0.-7.0:1-7`）—— `LN7_ANY_ADDRESS`，即 **负向第 7 扩展级子空间（Negative 7th Extension Level Subspace）** 中之"全零地址"，其语义为"任意地址均可"（Any Address Will Do）。**负向级维度与负向扩展地址长度专为 IPswen NAT 机制中之特殊用途而设计。** 此乃 IPswen 方案之一项关键创新：通过引入负向扩展子空间，支持简单高效之**无状态 NAT（Stateless NAT）**机制，以实现平滑、低成本且单栈式之过渡。

### 级维度之语义

- **$L = 0$（Level 0 Subspace）**: 标准 IPv4 模式。地址与标准 IPv4 语义完全一致，无扩展字节。地址 `A.B.C.D.0` 等价于传统 IPv4 地址 `A.B.C.D`。此模式确保与现存全部 IPv4 设备、应用及路由基础设施之**完全互操作性**。

- **$L > 0$（正向扩展子空间）**: 扩展字节作为地址之**高位部分**追加。正向扩展地址可理解为在传统 IPv4 地址基础之上增加了额外的网络前缀层次，使路由表项可表达更精细之拓扑信息。$L$ 之值愈大，可表达之地址空间愈广阔——Level 7 子空间含 $2^{56}$ 个地址。

- **$L < 0$（负向扩展子空间）**: 扩展字节作为地址之**主机标识符部分**之扩展。负向扩展地址专为 IPswen NAT 机制而设计——在此等机制中，负向扩展字节用于编码内网主机之扩展标识，而基地址（通常为公网 IPv4 地址）保持不变。此设计使得 NAT 操作无需维护连接状态表，实现真正的**无状态 NAT（Stateless NAT）**。

### 孙氏IP之三要素（IPswen's Three Elements）

依 `ipswen.net` 官方网站之定义，IPswen 寻址方案乃基于三项设计目标与约束（或称"哲学原则"），合称为 **"孙氏IP之三要素"（IPswen's Three Elements）**：

1. **可变长寻址（Variable Length Addressing）**: IPswen 地址之长度非固定，自 4 字节至 11 字节不等，视其级维度而定。此乃全部 IPswen 设计之根本出发点——通过引入可变长地址语义，在保持与固定长度 32 位 IPv4 兼容之同时，极大化扩展可用地址空间。

2. **单栈实现（Single Stack Implementation）**: IPswen 并非引入新的独立协议栈，而是在已有 TCP/IPv4 协议栈之基础上进行扩展——"仅需修改（或称'打补丁'）IPv4 之 32 位地址空间，而 TCP/IPv4 协议栈之其余一切均保持原封不动"。此设计原则确保 IPswen 之部署复杂度远低于引入全新协议栈之方案（如 IPv6），且可利用全部现有之 IPv4 技术积累与应用生态。

3. **双向互通与兼容（Bidirectional Interworking and Compatibility）**: IPswen 地址空间与标准 IPv4 地址空间之间支持**双向互通**——Level 0 子空间与 IPv4 一一映射，正向扩展地址可通过无状态 NAT 与 IPv4 互通，负向扩展地址可在 NAT 场景下承载内网主机标识。此原则确保 IPswen 网络与现存 IPv4 互联网之间可实现**平滑、无中断之交互**。

### 设计理念与基本假设

IPswen 之设计理念可追溯至互联网历史上三次卓越的演进式 IPv4 扩展——**CIDR（无类别域间路由）、NAT44（网络地址转换）以及 VPN（虚拟专用网络）**。正如互联网社区基于此等演进式技术创新成功地维持并扩展了 TCP/IPv4 架构，IPswen 项目组认为，互联网社区有必要亦有责任继续使用 IPswen 或类似之演进式技术创新来维护和扩展 TCP/IPv4 架构及其现有之技术基础与应用生态，同时在适当之应用领域或受控环境中部署 IPv6，以实现更好的共存与互通。

IPswen 乃基于以下**三项基本假设（Three Assumptions）**：

1. **IPv4 乃一项永久性技术基础（Permanent Tech Base）**：TCP/IPv4 协议栈，连同其地址池分配（已分配之 IPv4 地址作为宝贵资产）、全球路由基础设施、网络设备与配置，以及现有之 IPv4 应用与服务生态（技术基础），极有可能将永远存续并需被永久维护——即便 IPv6 在地球上每一角落皆 100% 可用。何以有人会如此相信？若你意识到所谓 IPv6 群体思维（IPv6 Groupthink）之"屋顶效应（Roof Effect）"，则可能需要重新思考此问题。

2. **IPv4 具有高度可扩展性**：使得 IPswen 扩展选项得以运作，较之将互联网之全部一切迁移至 IPv6，可能更为现实——即便每一"现代"网络设备或应用皆内建 IPv6 支持，仅待用户"开启"之。

3. **网络通信之历史充满重大失败与技术灾难**：一项失败技术所造成之影响愈大，其灾难亦愈大。意谓何也？IPswen 可能以其自身之方式失败，但其所造成之灾难小于 IPv6 方式所造成者。

### 对 IPv6 群体思维之批判（A Critique of the IPv6 Groupthink）

`ipswen.net` 对"IPv6 群体思维"之核心批判如下：

> *"... the key issue with the IPv6 groupthink is that it is not just about how advanced and perfect the IPv6 technology is, or how flawed, slow and inefficient IPv4 (or its various extensions like NAT or IPswen) would be, but it is all about how to deal with (or get rid of and replace) the existing IPv4 technology base and its application ecosystem."*
>
> —— "IPv6 群体思维之关键问题，不在于 IPv6 技术如何先进完美，亦不在于 IPv4（或其各类扩展如 NAT 或 IPswen）如何有缺陷、缓慢且低效，而全然在于如何处理（或摆脱并替代）现有之 IPv4 技术基础及其应用生态。"

复引尤瓦尔·诺亚·赫拉利（Yuval Noah Harari）于《人类简史》（Sapiens, A Brief History）中之名言：

> *"You could never convince a monkey to give you a banana by promising him limitless bananas after death in monkey heaven."*
>
> —— 你永远无法通过承诺在猴子天堂死后有无尽香蕉，来说服一只猴子放弃手中之香蕉。

同理：

> *"Equally, you could never convince a sane person to give up a working IPv4 address by promising him limitless addresses after discarding all the existing IPv4 devices and applications in an IPv6 heaven."*
>
> —— 同样，你永远无法通过承诺在丢弃所有现有 IPv4 设备与应用之后于 IPv6 天堂中有无尽地址，来说服一个理智之人放弃一个正在工作的 IPv4 地址。

### 学术论文发表

IPswen 方案之技术细节已发表于以下国际学术会议：

- **IPswen 技术概要（Outline）**：发表于 **IFIP Networking 2022** 会议（IEEE Xplore）。论文预印本可自 `ipswen.net` 下载：[IPswenOutline.pdf](https://ipswen.net/docs/overview/IPswenOutline.pdf)。
  - 摘要：*"IPswen is a variable length addressing and routing scheme for supporting the continued evolution of the Internet TCP/IPv4 architecture. To be an incrementally deployable and realistically useful solution, three new technical constraints on strict backward compatibility are devised to establish its design goals, principles and implementation elements, namely, variable length addressing, single stack implementation, and bidirectional interworking (with socket overlapping). Based on a multilevel L3 address space that consists of 8 subspaces of 8 kinds of address lengths ranging from 4 to 11 bytes, the result shows that TCP/IPv4 does possess a substantial potential for extension and future evolution. The scheme maintains binary backward compatibility to TCP/IPv4 and its numerous existing applications, and support a simple and efficient stateless NAT mechanism for smooth, low cost and single stack transition."*

- **IPswen 方法论与批判（Methodology and Critique）**：发表于 **IEEE ISNCC 2023** 会议（IEEE Xplore）。论文预印本及 PPT 幻灯片可自 `ipswen.net` 下载：[IPswenMethodology.pdf](https://ipswen.net/docs/overview/IPswenMethodology.pdf)、[IPswenMethodology_PPT.pdf](https://ipswen.net/docs/overview/IPswenMethodology_PPT.pdf)。
  - 摘要：*"We evaluate a unique opportunity for the continued evolution of the IPv4 Internet based on IPswen and its coexistence strategy with IPv6. We here emphasize two crucial aspects of this approach, namely, (1) making use of IPswen's special technical advantages for the long-term evolution of the Internet, and (2) avoiding conventional 'IPv6 groupthink' to enable healthy coexistence and independent development of the two IP technology bases."*

---

## 项目整体架构

下文以 ASCII 框图展示 IPSWEN 实验平台各组件之间之依赖关系与数据流向。层级自上而下依次为：路由控制平面、隧道基础设施、传输层实验、用户态工具链、内核基础层、数据平面加速层。

```
┌─────────────────────────────────────────────────────────────────────┐
│                      路由控制平面 (Control Plane)                      │
│  ┌──────────────┐   ┌──────────────────┐   ┌─────────────────────┐  │
│  │  bird/       │   │  mrtdb/          │   │  autoconfig/        │  │
│  │  BGP 守护进程 │◄──┤  MRT 解析与      │   │  GRE 隧道自动化      │  │
│  │  SAFI=127    │   │  连通性测试平台   │   │  C/S 架构          │  │
│  └──────┬───────┘   └──────────────────┘   └──────────┬──────────┘  │
│         │                                              │            │
├─────────┼──────────────────────────────────────────────┼────────────┤
│         │           隧道基础设施 (Tunnel Infrastructure)  │            │
│         │     ┌────────────────────────────────────────┘            │
│         │     │                                                     │
├─────────┼─────┼─────────────────────────────────────────────────────┤
│         │     │      传输层实验 (Transport Layer Experiments)         │
│         │     │  ┌────────────────────────────────────────┐         │
│         │     └──┤  happyfootball/                        │         │
│         │        │  Happy Eyeballs v2 + IP Options        │         │
│         │        │  + SO_BINDTODEVICE 多路径              │         │
│         │        └────────────────────────────────────────┘         │
├─────────┼───────────────────────────────────────────────────────────┤
│         │         用户态工具链 (Userspace Toolchain)                   │
│  ┌──────┴───────┐   ┌──────────────────┐   ┌─────────────────────┐  │
│  │ iproute2-    │   │  net-tools-1.60/ │   │  unpv13e/           │  │
│  │ 5.5.0/       │   │  ifconfig/route  │   │  UNP 示例的         │  │
│  │ ip 命令       │   │  /netstat        │   │  IPSWEN 适配        │  │
│  └──────────────┘   └──────────────────┘   └─────────────────────┘  │
├─────────────────────────────────────────────────────────────────────┤
│                   内核基础层 (Kernel Foundation)                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  linux-kernel/  (linux-src-5.4.0-ipswen-v1)                  │   │
│  │  ┌─────────────────────────────────────────────────────────┐ │   │
│  │  │ 数据平面:  include/net/ipswen.h  (核心数据结构)           │ │   │
│  │  │ 网络栈:    net/ipv4/  (FIB, 路由, ICMP)                  │ │   │
│  │  │ Netlink:   net/core/rtnetlink.c  (RTA_DST_EXT)          │ │   │
│  │  │ Socket:    net/ipv4/ip_sockglue.c  (IPSWEN_LEVEL)       │ │   │
│  │  └─────────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────┤
│                数据平面加速层 (Data Plane Acceleration)                 │
│  ┌──────────────────┐   ┌──────────────────────────────────────┐    │
│  │  netmap/         │   │  optmap/                             │    │
│  │  高性能包 I/O     │   │  eBPF/XDP 自定义 IP 选项              │    │
│  │  + VALE L2 交换  │   │  数据包路由                          │    │
│  └──────────────────┘   └──────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────────┤
│                  域名解析辅助层 (DNS Auxiliary Layer)                   │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  fakedns/  (LD_PRELOAD DNS 代理, IPv6→伪IPv4 映射)           │   │
│  └──────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────┤
│                    第三方库 (Third-Party Libraries)                    │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  libs/  (libbgpstream-2.2.0, wandio-4.2.4-1)                │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

上述架构图所揭示之关键信息流如下：

1. **内核** 提供 IPSWEN 地址之 FIB 存储与查询能力，通过 Netlink 接口向用户态暴露。
2. **iproute2** 与 **net-tools** 通过 Netlink 与内核交互，使用户能够以点分十进制级维度记法（如 `10.0.0.0.2.128.129/33`）配置路由与接口。
3. **bird** 通过 Netlink 将 BGP 学习到之 IPSWEN 路由注入内核 FIB，同时通过 TCP 与远端 BGP 对等体交换 IPSWEN NLRI。
4. **autoconfig** 为 BGP 对等体之间按需建立 GRE 隧道，提供 IP 层连通性基础。
5. **mrtdb** 解析公开 BGP MRT 归档，生成 BIRD 配置，并以 Scapy 探测包验证实验网络之全网连通性。
6. **happyfootball** 在 IPSWEN Socket API 之上进行传输层多路径实验，测量 IP 选项对 MSS 之影响。
7. **netmap** 与 **optmap** 分别自内核模块与 eBPF/XDP 两个路径提供高性能数据平面，用于评估 IPSWEN 地址查找在高速转发场景下之开销。
8. **fakedns** 提供 DNS 层面之透明代理，将 IPv6 域名解析结果映射为伪 IPv4 地址，使实验网络中之传统应用无需修改即可访问 IPv6 资源。

---

## 各子项目详解

以下对 `ipswen-project/` 目录下每一子目录进行穷尽式介绍。内容综合各子项目自身 README 之信息，并补充根级视角下之项目间关联说明。

---

### 一、linux-kernel/ —— IPSWEN 修改版 Linux 5.4.0 LTS 内核

#### 定位

`linux-kernel/` 包含 IPSWEN 项目的**核心基础层**——经过系统性修改的 Linux 5.4.0 LTS 内核源码树。一切上层工具、守护进程与应用程序均依赖于本内核所提供的 IPSWEN 地址语义、FIB 扩展与 Socket API。该内核构成整个实验平台的**运行时基础**，无此内核则所有上层 IPSWEN 特性均无从运作。

#### 修改层次

内核修改覆盖 **6 个逻辑层次**，自底向上依次为：

**第一层：核心数据结构**（`include/net/ipswen.h`）
- 定义 `struct ipswen_addr`：包含 `__be32 base`（标准 IPv4 基地址）、`s8 level`（有符号级维度）、`u8 ext[IPSWEN_MAX_LEVEL]`（扩展字节数组）。
- 定义 `IPSWEN_MAX_LEVEL` 宏，默认值 7。
- 提供 `ipswen_addr_equal()`、`ipswen_addr_copy()`、`ipswen_addr_is_zero()` 等内联辅助函数。
- 在 `struct fib_key` 和 `struct inetpeer_addr` 中添加 IPSWEN 联合体成员。

**第二层：Socket 层**（`net/ipv4/ip_sockglue.c`）
- 新增 `setsockopt` / `getsockopt` 选项 `IPSWEN_LEVEL`，允许应用按套接字粒度指定 IPSWEN 级维度。
- 在 `ip_queue_xmit()` 中根据套接字的 IPSWEN 状态扩展 IP 数据包的源地址与目的地址。

**第三层：FIB 层**（`net/ipv4/fib_semantics.c`、`fib_trie.c`、`fib_rules.c`）
- 扩展 FIB 语义结构以携带 IPSWEN 扩展前缀信息。
- 修改最长前缀匹配（LPM）算法，使路由查找时可以基于 `(base, level, ext_bytes)` 三元组进行匹配。
- 支持同一基地址、不同 level 的路由共存。

**第四层：Netlink 接口**（`net/core/rtnetlink.c`、`include/uapi/linux/rtnetlink.h`）
- 新增 `RTA_DST_EXT` Netlink 属性，用于在 `RTM_NEWROUTE` / `RTM_GETROUTE` 消息中携带 IPSWEN 扩展目的地址。
- 扩展 `rtm_ipv4_policy` 数组以接受新属性。
- 修改路由消息的编码 / 解码函数，确保 IPSWEN 信息在内核与用户态之间正确传递。

**第五层：Socket 选项注册**（`net/ipv4/ip_sockglue.c`）
- 注册 `IPSWEN_LEVEL` 选项的处理函数，提供权限检查（需要 `CAP_NET_ADMIN`）。
- 在与 `struct sock` 关联的 `struct ipswen_sock_state` 中持久化套接字级 IPSWEN 配置。

**第六层：输入验证与边界检查**
- 在地址解析路径中添加 level 范围验证（$-7 \leq L \leq 7$）。
- 对扩展字节数组进行长度一致性检查。
- 防止畸形 Netlink 消息导致的内核越界访问。

#### 编译与安装

**前置条件：**
- Ubuntu 18.04+ 或 Debian 10+（amd64 架构）
- 至少 20 GB 可用磁盘空间（编译产物约 15-18 GB）
- 已安装 `build-essential, libncurses-dev, bison, flex, libssl-dev, libelf-dev`

**编译步骤：**

```bash
# 第一步：进入内核源码目录
cd linux-kernel/linux-src-5.4.0-ipswen-v1-20240124/

# 第二步：配置内核（可直接复制当前运行内核的配置）
cp /boot/config-$(uname -r) .config
make olddefconfig

# 第三步（可选）：精细调整配置
make menuconfig
# 确认以下选项已启用:
#   CONFIG_NET_IPGRE=m
#   CONFIG_NET_IPIP=m
#   CONFIG_IP_ADVANCED_ROUTER=y
#   CONFIG_IP_MULTIPLE_TABLES=y
#   CONFIG_IP_ROUTE_VERBOSE=y

# 第四步：编译
make -j$(nproc)          # 内核映像与模块
make modules_install     # 安装模块到 /lib/modules/
make headers_install     # 安装内核头文件到 /usr/src/
make install             # 安装内核映像到 /boot/ 并更新 GRUB

# 第五步：构建 .deb 包（可选，便于分发）
make deb-pkg -j$(nproc)
# 产物: ../linux-image-5.4.0-ipswen-v1-20240124_*.deb
#       ../linux-headers-5.4.0-ipswen-v1-20240124_*.deb

# 第六步：重启进入新内核
sudo reboot
uname -r  # 应输出: 5.4.0-ipswen-v1-20240124
```

**头文件部署（供 iproute2、net-tools、bird 等编译使用）：**

```bash
sudo dpkg -i linux-headers-5.4.0-ipswen-v1-20240124_*.deb
# 确认头文件已就位:
ls /usr/src/linux-headers-5.4.0-ipswen-v1-20240124/include/net/ipswen.h
```

#### 大文件分块说明

`linux-src-5.4.0-ipswen-v1-20240124.tar.gz` 大小为 171 MB，超过 GitHub 单文件 100 MB 限制，已拆分为 `part00` 与 `part01` 两个 95 MB 分块。完整源码树（`linux-src-5.4.0-ipswen-v1-20240124/`）本身未拆分——仅 `.tar.gz` 归档受影响。还原方法见本 README 附录 A。

#### 调试与验证

```bash
# 验证 IPSWEN 路由添加
sudo ip route add 10.0.0.0.2.128.129/33 via 192.168.1.1

# 检查内核 FIB 中的 IPSWEN 条目
cat /proc/net/fib_trie | grep "("

# 动态调试
echo 'file net/ipv4/route.c +p' | sudo tee /sys/kernel/debug/dynamic_debug/control
sudo dmesg -wT | grep -i ipswen

# ftrace 追踪
cd /sys/kernel/debug/tracing
echo function > current_tracer
echo '*ipswen*' > set_ftrace_filter
cat trace_pipe
```

---

### 二、iproute2-5.5.0/ —— 修改版 iproute2 工具集

#### 定位

`iproute2-5.5.0/` 包含经过 IPSWEN 适配的 `iproute2` 工具集（版本 5.5.0）。此为用户态与内核 IPSWEN 功能之间的**首要交互接口**——所有以 `A.B.C.D.L.E.F.G...` 记法书写的 `ip route`、`ip addr` 命令均依赖本工具集中新增的 `ipswen_aton()` / `ipswen_ntoa()` 地址解析函数。

#### 核心修改

**1. lib/utils.c —— 地址解析与格式化**

新增两个关键函数：

- `ipswen_aton(const char *str, struct ipswen_addr *addr)`：将 IPSWEN 记法字符串（如 `"10.0.0.1.2.1.2"`）解析为二进制 `struct ipswen_addr` 结构。解析逻辑为：先以标准 `inet_aton()` 解析基地址 `A.B.C.D`，再读取级维度数字 $L$ 及后续 $|L|$ 个扩展字节。
- `ipswen_ntoa(const struct ipswen_addr *addr, char *buf, size_t len)`：将二进制 `struct ipswen_addr` 格式化为 IPSWEN 记法字符串。

**2. ip/iproute.c —— 路由命令扩展**

- 在 `parse_rta()` 中添加 `RTA_DST_EXT` 属性的解析与格式化。
- 在路由添加路径中，将命令行中的 IPSWEN 记法编码为 `RTA_DST_EXT` Netlink 属性，随 `RTM_NEWROUTE` 消息发送至内核。
- 在路由显示路径中，从 Netlink 应答消息中解码 `RTA_DST_EXT` 属性并格式化输出。

**3. ip/ipaddress.c —— 地址命令扩展**

- 支持以 IPSWEN 记法显示和配置地址。

#### 编译

```bash
cd iproute2-5.5.0/

# 确认内核头文件已安装
ls /usr/include/net/ipswen.h || echo "需要先安装 IPSWEN 内核头文件包"

# 配置
./configure

# 编译
make -j$(nproc)

# 安装（替换系统 ip 命令，谨慎操作）
sudo make install

# 或仅测试不安装
./ip/ip route list | grep "("
```

#### 使用示例

```bash
# 添加 IPSWEN 路由
sudo ./ip/ip route add 10.0.0.0.2.128.129/33 via 192.168.1.1

# 查看包含 IPSWEN 扩展的路由
./ip/ip route show | grep "("

# 添加 IPSWEN 地址
sudo ./ip/ip addr add 192.168.100.1.3.10.20.30/35 dev eth0
```

---

### 三、net-tools-1.60/ —— 修改版 net-tools 工具集

#### 定位

`net-tools-1.60/` 包含经典 Linux 网络工具集 `net-tools`（版本 1.60）的 IPSWEN 适配版本。虽然 `iproute2` 是现代 Linux 网络配置的推荐工具，但 `ifconfig`、`route`、`netstat` 等传统命令在实验教学、脚本兼容性以及快速诊断场景中仍具有不可替代的价值。本子项目确保这些传统工具能够识别并正确显示 IPSWEN 地址与路由条目。

#### 核心修改

**1. ifconfig.c**
- 在接口地址显示函数中添加对 IPSWEN 扩展地址的格式化逻辑。
- 支持通过 `ifconfig` 查看接口上配置的 IPSWEN 地址。

**2. route.c**
- 在路由表显示中添加对 `RTA_DST_EXT` 属性的识别与格式化。
- 支持以 `A.B.C.D.L.E.F.G...` 记法显示路由表项。

**3. netstat.c**
- 在路由表显示（`netstat -r`）中添加 IPSWEN 路由条目的格式化输出。
- 扩展地址解析逻辑以兼容 IPSWEN 记法。

#### 编译

```bash
cd net-tools-1.60/

# 确认内核头文件
ls /usr/include/net/ipswen.h

# 编译
make -j$(nproc)

# 测试
./ifconfig -a
./route -n | grep "("
./netstat -rn | grep "("
```

---

### 四、bird/ —— IPSWEN 修改版 BIRD Internet Routing Daemon

#### 定位

`bird/` 包含经过 IPSWEN 扩展的 BIRD Internet Routing Daemon（基于上游 BIRD 2.13.1）。BIRD 在该项目生态中承担**路由控制平面的核心角色**：通过 BGP 协议在实验网络节点之间交换 IPSWEN 路由信息，并将学习到的路由通过 Netlink 注入 IPSWEN 内核的 FIB 表。其关键扩展为引入私有 SAFI 值 127 以承载 IPSWEN NLRI（Network Layer Reachability Information）。

#### 核心修改

**1. 新增 SAFI=127（IPSWEN_SAFI）**

在 `nest/route.h` 与 `proto/bgp/bgp.h` 中定义 `SAFI_IPSWEN = 127`。IANA 未分配该值，仅供实验网络内部使用。

**2. IPSWEN NLRI 编码 / 解码**

在 `proto/bgp/attrs.c` 中添加 IPSWEN NLRI 的编码（构建 UPDATE 消息时）与解码（解析 UPDATE 消息时）逻辑。每个 IPSWEN NLRI 前缀编码格式为：

```
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|   Level (1 octet, signed)  |  Prefix Length     |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|   Base IPv4 Prefix (0-4 octets)                  |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
|   Extension Bytes (0-7 octets)                   |
+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
```

**3. BGP 能力协商**

在 OPEN 消息的 Capability 参数中添加 `SAFI=127` 的宣告与解析。仅当两端均在 OPEN 消息中宣告支持 SAFI=127 时，IPSWEN 路由才会被交换。此设计确保与标准 BGP 实现的互操作性——对端不认识 SAFI=127 时，BIRD 不会向其发送 IPSWEN UPDATE。

**4. 配置语法扩展**

在配置文件中新增 `ipswen` 通道类型，允许在 `protocol static` 或 `protocol bgp` 中定义 IPSWEN 路由：

```bird
protocol static {
    ipswen;
    route 10.0.0.0.2.128.129/33 via 192.168.1.1;
    route 10.0.0.0.5.1.2.3.4.5/37 via "eth0";
}
```

**5. 条件编译**

IPSWEN 特性通过 `#ifdef HAVE_IPSWEN` 条件编译保护。若编译环境中不存在 `<net/ipswen.h>` 头文件，BIRD 将回退为标准 BIRD 2.13.1 行为——无 IPSWEN SAFI，无 IPSWEN NLRI 处理，完全等同于上游版本。

#### 大文件配置

`bird/configs/` 中包含 9 个百万路由配置文件（用于压力测试），每个文件超过 150 MB，已拆分为 `.part00` 与 `.part01` 分块：

| 文件 | 大小 | 路由数 |
|------|------|--------|
| `node1_IPv4_1000000.conf` | 150 MB | 1,000,000 |
| `node1_IPswen_level0_1000000.conf` | 152 MB | 1,000,000 |
| `node1_IPswen_level1_1000000.conf` | 155 MB | 1,000,000 |
| `node1_IPswen_level2_1000000.conf` | 158 MB | 1,000,000 |
| `node1_IPswen_level3_1000000.conf` | 161 MB | 1,000,000 |
| `node1_IPswen_level4_1000000.conf` | 165 MB | 1,000,000 |
| `node1_IPswen_level5_1000000.conf` | 168 MB | 1,000,000 |
| `node1_IPswen_level6_1000000.conf` | 172 MB | 1,000,000 |
| `node1_IPswen_level7_1000000.conf` | 175 MB | 1,000,000 |

还原方法：运行项目根目录的 `reassemble.sh` 脚本，或手动 `cat *.part00 *.part01 > output.conf`。

#### 编译与运行

```bash
cd bird/

# 确认内核头文件已安装
ls /usr/include/net/ipswen.h

# 进入源码目录编译
cd src/
autoreconf -i
./configure
make -j$(nproc)

# 产物: bird, birdc, birdcl

# 运行
sudo ./bird -c ../configs/bird.conf   # 后台
sudo ./bird -l                        # 前台（调试模式）

# 交互式控制
sudo ./birdc
bird> show protocols
bird> show route count
bird> show bgp sessions
bird> show route table master | wc -l
```

**多节点分发：**

```bash
cd bird/src/
python3 dist.py bird birdc birdcl start.sh.template stop.sh.template
python3 dist.py ../configs/bird.conf ../configs/node1_*.conf
```

`dist.py` 读取 `nodes.txt`（格式：`节点ID IP 端口 用户名 密码`），通过 `sshpass + scp` 向远程节点推送编译产物与配置文件。

#### 性能基准参考

| 场景 | 路由数 | 内存占用 | 收敛时间 |
|------|--------|---------|---------|
| 空配置 | 0 | ~15 MB | <1s |
| 100 万条静态 | 1,000,000 | ~1.2 GB | ~30s |
| 100 万条 BGP | 1,000,000 | ~1.5 GB | ~2-5 min |

---

### 五、mrtdb/ —— 全球路由连通性测试平台

#### 定位

`mrtdb/` 是本项目群中**规模最大、功能最全面的子项目**（自身 README 长达 1021 行），其定位为：基于公开 BGP MRT（Multi-Threaded Routing Toolkit）路由归档数据进行大规模互联网路由连通性实验的综合平台。该平台集成了 MRT 文件解析、C 语言嵌入式内存路由数据库（cmrtdb）、Python 三层控制架构、Scapy 探针生成以及实验结果分析的全套工具链。

#### 核心组件

**1. BGPdump.py —— MRT 文件解析器**
- 从 RIPE RIS（Routing Information Service）或 RouteViews 的 MRT 归档中提取 BGP UPDATE 消息。
- 解析 AS_PATH、COMMUNITY、NLRI 等 BGP 属性。
- 输出 JSON 格式的路由数据，供下游组件消费。

**2. cmrtdb/ —— C 语言内存路由数据库**
- 高性能内存驻留路由数据库，以 Radix Tree（基树／压缩前缀树）为底层数据结构。
- 提供 C 库接口（`libcmrtdb.a`），供 Python 通过 `ctypes` 调用。
- 支持标准 IPv4 前缀以及 IPSWEN 扩展前缀的插入、查询、删除与最长前缀匹配。
- `mrtdb_user.py` 封装 `ctypes` 调用为 Pythonic 接口。

**3. srv.py —— 核心服务器**
- 基于 `asyncio` 的异步 TCP 服务器，监听多个端口以接受路由查询与探测请求。
- 维护与 `cmrtdb` 的交互会话，将路由查询缓存于内存中。
- 在接收到探测任务后，协调 Scapy 探针的生成与发送。

**4. cli.py —— 命令行客户端**
- 向 `srv.py` 发起连接，发送路由查询或探测任务请求。
- 支持交互模式与批处理脚本模式。
- 提供 `ping`、`traceroute`、`probe` 等子命令。

**5. ping.py 与 pktest.py —— 连通性测量工具**
- `ping.py`: 基于 Scapy 的定制化 ICMP Echo 探测工具，支持 IPSWEN 源地址设置、自定义 IP 选项注入以及 GRE 隧道封装。
- `pktest.py`: 通用数据包测试框架，支持多种协议（TCP SYN、UDP、ICMP）的端到端可达性测量。

**6. generate_bird_config.py —— BIRD 配置生成器（关键桥接组件）**
- 将 `BGPdump.py` 输出的路由数据转换为 BIRD 配置文件格式。
- 支持按 AS 过滤、按 prefix 聚合、按 level 分类。
- **此组件为 mrtdb 与 bird 之间的关键桥接**：实现从"解析公开路由数据"到"在实验网络中重放路由拓扑"的完整工作流。

**7. rt.py —— 路由表操作库**
- 提供路由表条目（Route Entry）的 Python 对象模型。
- 支持 IPv4 前缀与 IPSWEN 前缀的统一表示与操作。

**8. notebooks/ —— Jupyter 实验笔记本**
- 包含数据分析和结果可视化的 Jupyter Notebook。
- 对实验结果进行统计汇总、绘制 CDF 图、分析连通性矩阵。

**9. 辅助工具集**
- `asprefix.py` —— AS 前缀分析
- `CDefs.py` —— 常量定义
- `gre_test.py` —— GRE 隧道测试
- `ip_options_auto_test.py` —— IP 选项自动测试
- `udpclient.py` / `udpserver.py` —— UDP 连通性测试
- `optest.py` —— 选项测试
- `mrtdb_test.py` —— 路由数据库单元测试
- `test_ip.py` / `test_retry.py` —— 辅助测试脚本

#### 子目录结构

```
mrtdb/
├── cmrtdb/                  # C 语言路由数据库库
├── control/                 # 控制平面脚本
├── libbgpdump-1.6.0/        # MRT 解析 C 库（第三方）
├── mrt_data/                # MRT 数据归档存放目录
├── notebooks/               # Jupyter 分析笔记本
├── results/                 # 实验结果输出目录
├── testdata/                # 测试数据集
├── BGPdump.py               # MRT 解析器
├── cli.py                   # CLI 客户端
├── srv.py                   # 核心服务器
├── ping.py                  # Scapy 探测工具
├── pktest.py                # 数据包测试框架
├── rt.py                    # 路由表对象模型
├── generate_bird_config.py  # BIRD 配置生成（桥接 mrtdb → bird）
├── mrtdb_user.py            # cmrtdb Python 封装
├── 80.py                    # 端口 80 探测工具
└── ... (其他辅助脚本)
```

#### 典型工作流

1. **数据获取**: `BGPdump.py` 解析 RIPE RIS MRT 归档，提取全网路由信息。
2. **路由导入**: `generate_bird_config.py` 将路由转换为 BIRD 配置文件。
3. **路由分发**: `dist.py`（在 bird 项目中）将配置推送到各实验节点。
4. **BGP 对等**: `bird` 在各节点之间交换路由，注入内核 FIB。
5. **隧道建立**: `autoconfig` 为 BGP 对等体建立 GRE 隧道。
6. **连通性测试**: `ping.py` / `pktest.py` 向全球前缀发送探测包并收集结果。
7. **结果分析**: `notebooks/` 中的 Jupyter 笔记本对结果进行统计分析与可视化。

---

### 六、autoconfig/ —— GRE 隧道自动化配置系统

#### 定位

`autoconfig/` 实现了一个基于 **Client/Server 架构** 的 GRE 隧道自动化建立与拆除系统。该子项目在本平台中的角色是**连通性基础设施提供者**——为 bird 的 BGP 对等体之间、mrtdb 的探测节点与目标网络之间、以及 happyfootball 的多路径实验提供所需的 IP 层隧道。

#### 架构

```
┌──────────────┐                    ┌──────────────────┐
│  Hub 节点     │  TCP :1111 (JSON)  │  Spoke 节点       │
│  server.py   │◄──────────────────►│  client.py       │
│              │                    │                  │
│  维护隧道池   │                    │  connect/disconnect│
│  CIDR 分配   │                    │  本地隧道管理      │
└──────────────┘                    └──────────────────┘
```

#### 协议设计

客户端与服务端之间通过 TCP 端口 1111 交换 JSON 消息。协议语义如下：

**请求格式（Client → Server）：**
```json
{"action": "connect", "token": "", "hostname": "node3"}
{"action": "disconnect", "token": "", "hostname": "node3"}
{"action": "status"}
```

**响应格式（Server → Client）：**
```json
{"status": "ok", "cidr": "10.0.0.0/31", "peer_ip": "10.0.0.1", "my_ip": "10.0.0.0"}
{"status": "fail", "reason": "No enough resources"}
```

**隧道建立流程：**
1. Spoke 节点客户端向 Hub 节点服务器发送 `{"action": "connect"}` 请求。
2. 服务端从预配置的 CIDR 地址池中分配一个未使用的 `/31` 子网。
3. 服务端创建本地 GRE 隧道接口并配置 IP。
4. 服务端返回 `{"status": "ok", "cidr": ..., "peer_ip": ..., "my_ip": ...}`。
5. 客户端收到响应后，创建本地 GRE 隧道接口，配置对方返回的 `my_ip` 为自己接口的 IP，`peer_ip` 为对端 IP。
6. 双向 IP 连通性建立完成。（可选）在此基础上运行 BGP 或其他路由协议。

**关键设计决策：** `disconnect` 时客户端**无论服务端响应如何均先行删除本地隧道**——此设计确保即使服务端状态丢失（如进程重启），残留在 Spoke 节点上的隧道也能被清理。

#### MTU 设计

```
1500 (以太网标准 MTU)
 - 20 (外层 IPv4 头部)
 -  4 (GRE 头部)
 - 26 (预留 IP 选项空间)
──────────────
 1450 ← 隧道内层 MTU
```

26 字节预留支持实验性 IP 选项（最大 40 字节）。若路径涉及多层封装（如 IP-in-IP over GRE），需要进一步调低 MTU。

#### 依赖

- Python 3.6+（仅需标准库：`json, socket, os, sys, socketserver`）
- Linux 内核 `CONFIG_NET_IPGRE=m`
- iproute2（需 root 权限）
- 网络防火墙放行 TCP 1111 与 GRE（协议号 47）

#### 实验定制

- **扩展地址池**: 修改 `server.py` 中的 `cidr_to_alloc` 列表。
- **更换隧道类型**: 将 GRE 改为 IP-in-IP（`mode ipip`）。
- **添加认证**: 在请求中添加 `token` 字段并在服务端验证。
- **集成到 bash**: `python3 client.py connect <server_ip>` 后跟 `ping` 验证。

---

### 七、happyfootball/ —— Happy Eyeballs v2 多路径传输实验

#### 定位

`happyfootball/` 实现了基于 RFC 8305（Happy Eyeballs Version 2）的**多路径 TCP 连接建立实验框架**。该子项目的核心研究问题是：在 IPSWEN 扩展地址环境下，IP 选项如何影响路径 MTU（进而影响 TCP MSS），以及 Happy Eyeballs 算法在多路径选择中的行为特性。

#### 核心技术要素

**1. mysocket.cpp / mysocket.hpp —— Happy Eyeballs 实现**

核心函数 `happy_socket()` 实现了以下算法：

```
happy_socket(dest_addr, sock1, sock2, wait_us, grace_us):
  1. 在 sock1 上发起非阻塞 connect()
  2. 在 sock2 上发起非阻塞 connect()
  3. 使用 select() 等待任一 socket 就绪
  4. 若 sock2 先成功 → 返回 sock2（立即选择备用路径）
  5. 若 sock1 先成功:
     a. 在 grace_us 宽限期内继续等待 sock2
     b. 若 sock2 在宽限期内成功 → 返回 sock2（优选备路径）
     c. 若宽限期结束 sock2 仍不成功 → 返回 sock1（回退主路径）
  6. 若两者均超时 → 返回 -1（连接失败）
```

**2. IP 选项注入**

通过 `setsockopt(sock, IPPROTO_IP, IP_OPTIONS, ...)` 在每个 TCP SYN 包中注入自定义 IP 选项（type=0xA0），以测量中间路由器对非标准 IP 选项的容忍度。

**3. SO_BINDTODEVICE 多路径绑定**

使用 `setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, "ethN")` 将不同 socket 绑定到不同物理 / 隧道接口上，实现多路径并行连接尝试。

**4. MSS 测量**

通过 `getsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, ...)` 获取 TCP 层协商的 MSS 值，分析 IP 选项大小对 MSS 的定量影响。

#### MSS 理论计算

MSS = 路径 MTU − 40（标准 TCP/IP 头）− IP 选项字节数（对齐到 4 字节边界）− 封装开销。

| IP 选项大小 | 对齐后 | MSS (MTU=1500) | MSS (MTU=1450, GRE隧道内) |
|-----------|--------|-----------------|--------------------------|
| 0 | 0 | 1460 | 1410 |
| 4 | 4 | 1456 | 1406 |
| 8 | 8 | 1452 | 1402 |
| 12 | 12 | 1448 | 1398 |
| 20 | 20 | 1440 | 1390 |
| 40 | 40 | 1420 | 1370 |

#### happy_socket() 决策矩阵

| sock1 结果 | sock2 结果 | 最终选择 | 原因 |
|-----------|-----------|---------|------|
| 成功 (0.5s) | 成功 (0.8s) | sock2 | 宽限期内备用也成功 → 优选备用路径 |
| 成功 (0.5s) | 成功 (1.5s) | sock1 | 超出宽限期 → 不等了，用主路径 |
| 成功 (0.5s) | 超时 | sock1 | 备用失败 → 主路径为唯一可用 |
| 成功 (0.8s) | 成功 (0.3s) | sock2 | 备用先成功 → 立即选择 |
| 超时 | 成功 (0.3s) | sock2 | 主路径超时 → 仅备用可用 |
| 超时 | 超时 | -1 | 全部失败 |

#### 编译

```bash
cd happyfootball/
g++ -std=c++11 -o mysocket mysocket.cpp
g++ -std=c++11 -o send send.cpp
g++ -std=c++11 -o test test.cpp
```

#### 实验建议

- **实验 1**: 用不同 IP 选项大小调用 `./send`，记录 MSS 变化曲线。
- **实验 2**: 对比 `happy_socket()` 多路径与单一 `connect()` 的连接延迟差异。
- **实验 3**: 调整 `grace_us` 参数（0 → 2,000,000 µs），观察选择行为变化。

---

### 八、fakedns/ —— LD_PRELOAD DNS 代理与 IPv6-to-伪IPv4 映射

#### 定位

`fakedns/` 实现了一个基于 `LD_PRELOAD` 机制的**透明 DNS 解析代理**。其核心功能为：拦截目标应用程序的 DNS 解析系统调用，将 AAAA 记录（IPv6 地址）映射为保留的伪 IPv4 地址范围，从而使仅支持 IPv4 的传统应用程序能够透明地访问 IPv6-only 资源。在 IPSWEN 实验平台中，fakedns 提供了**域名解析层的透明适配**，使得实验网络中的节点无需修改应用程序代码即可接入 IPv6 外部资源。

#### 技术原理

`fakedns` 通过 `LD_PRELOAD` 机制劫持以下 8 个 glibc 系统调用封装函数：

| 序号 | 函数 | 作用 |
|------|------|------|
| 1 | `getaddrinfo` | DNS 解析入口（最核心） |
| 2 | `getnameinfo` | 反向 DNS 解析 |
| 3 | `gethostbyname` | 传统主机名解析 |
| 4 | `gethostbyname2` | 指定地址族的主机名解析 |
| 5 | `gethostbyaddr` | 按地址查主机名 |
| 6 | `freeaddrinfo` | 释放 addrinfo 链表 |
| 7 | `bind` | 绑定 socket（地址重写） |
| 8 | `connect` | 连接 socket（地址重写） |

当目标程序调用 `getaddrinfo("example.com", ...)` 时，若 DNS 返回 IPv6 地址（如 `2001:db8::1`），fakedns 的 `DnsMapper` 单例类将其映射到一个 `44.0.0.0/8` 范围内的唯一伪 IPv4 地址（如 `44.0.0.1`），并在内部哈希表中维护 `伪IPv4 → 真实IPv6` 的双向映射。后续的 `connect()` 调用被拦截并被重写为与伪 IPv4 地址的连接——fakedns 在内部完成伪 IPv4 到真实 IPv6 的翻译，并在 socket 层使用真实 IPv6 地址发起连接。

#### 编译与使用

```bash
cd fakedns/
make

# 使用
LD_PRELOAD=./fakedns.so <target_program>
# 例如:
LD_PRELOAD=./fakedns.so curl https://ipv6.google.com/
```

#### 基准测试

子目录含 `benchmark` 与 `benchmark_mapper` 程序（C++ 编写），用于测量 DnsMapper 在不同负载下的性能表现（查找延迟、内存开销、吞吐量）。`plot_benchmark.py` 将基准测试结果绘制为统计图表。

---

### 九、netmap/ —— 高性能数据包 I/O 框架

#### 定位

`netmap/` 是一个高性能数据包 I/O 框架，通过绕过内核网络栈的零拷贝批量数据包处理机制，实现线速（line-rate）包转发能力。该子项目还包含 VALE 软件 L2 交换机。在 IPSWEN 实验平台中，netmap 承担**数据平面性能评估**的角色——在 IPSWEN 内核路由查找路径可能引入额外开销的场景下，netmap 提供一个"理论上限"性能基准，同时也用于高速生成和采集实验流量。

#### 核心组件

- **内核模块 (`netmap.ko`)**: 实现零拷贝数据包缓冲池、内存映射 I/O 以及批量数据包收发。
- **VALE 交换机**: 高性能软件 L2 交换机，基于 netmap 框架构建，支持多端口线速交换。
- **修改版设备驱动**: 经 netmap 适配的 Intel (e1000/e1000e/igb/ixgbe/i40e/igc/ixgbevf) 与 NVIDIA (forcedeth) 网卡驱动，支持 netmap 模式下的零拷贝收发。

#### 编译

```bash
cd netmap/
./configure
make -j$(nproc)
sudo make install   # 安装内核模块
sudo modprobe netmap
```

---

### 十、optmap/ —— 基于 eBPF/XDP 的自定义 IP 选项数据包路由

#### 定位

`optmap/` 实现了基于 eBPF/XDP（eXpress Data Path）的**自定义 IP 选项驱动数据包路由**。利用 Berkeley Packet Filter 的可编程能力，在网卡驱动的早期接收路径（XDP hook 点）对携带特定 IP 选项（`OPT_EXT_SRC=233`, `OPT_EXT_DEST=241`）的数据包进行路由决策。该组件在 IPSWEN 平台中代表了数据平面的**可编程加速路径**——与 netmap 的零拷贝批量处理形成互补（eBPF/XDP 更侧重早期过滤与转发决策，netmap 更侧重批量包 I/O）。

#### 核心数据结构

```c
struct opt_ext {
    __u8 type;       // 233 (OPT_EXT_SRC) 或 241 (OPT_EXT_DEST)
    __u8 length;     // 选项总长度
    __u8 data[0];    // 可变长度数据
};
```

#### 运行原理

1. 编译 eBPF 程序为 BPF 字节码（通过 clang/LLVM）。
2. 使用 `ip link set dev eth0 xdp obj optmap.o` 将程序加载到网卡 XDP hook 点。
3. 每收到一个数据包，XDP 程序检查 IP 头中的选项字段。
4. 若匹配到自定义选项类型（233 或 241），根据选项中的路由信息决定转发或上送内核。

---

### 十一、libs/ —— 第三方依赖库

#### 定位

`libs/` 目录存放 IPSWEN 项目依赖的第三方 C/C++ 库的源码。当前包含两个库：

**1. libbgpstream-2.2.0**

BGPStream 是 CAIDA 开发的 BGP 数据流处理框架，提供统一的 API 以访问 RIPE RIS 与 RouteViews 的 BGP 记录归档。该库被 `mrtdb/` 子项目用于解析 MRT 格式的 BGP 路由数据。

编译：
```bash
cd libs/libbgpstream-2.2.0/
./configure
make -j$(nproc)
sudo make install
```

**2. wandio-4.2.4-1**

WAND's IO library（libwandio）是一个提供压缩透明 I/O 的 C 库——自动检测并解压 gzip/bzip2/lzma 等格式的数据流。BGPStream 依赖此库以透明读取压缩的 MRT 归档。

编译：
```bash
cd libs/wandio-4.2.4-1/
./configure
make -j$(nproc)
sudo make install
```

---

### 十二、unpv13e/ —— 修改版 UNIX Network Programming 示例

#### 定位

`unpv13e/` 是基于 W. Richard Stevens 经典著作《UNIX Network Programming》第三版示例代码的 IPSWEN 适配版本。该子项目在原始 UNP 示例的基础上添加了对 IPSWEN Socket API（`setsockopt(IPSWEN_LEVEL)`）的使用演示，为开发者提供具体可运行的代码参考。其核心贡献是为 `ping` 程序添加了 IPSWEN 源地址设置支持。

#### 核心修改

**ping 程序的 IPSWEN 扩展**:
- 新增命令行参数 `-L <level>` 指定 IPSWEN 级维度。
- 新增 `-E <ext_bytes>` 指定扩展字节。
- 在创建 ICMP socket 后调用 `setsockopt(sock, IPPROTO_IP, IPSWEN_LEVEL, ...)` 设置 IPSWEN 源地址。

#### 编译

```bash
cd unpv13e/
./configure
cd lib/ && make && cd ../
cd ping/ && make
# 使用:
sudo ./ping/ping -L 3 -E "10.20.30" 192.168.1.1
```

---

### 十三、reassemble.sh —— 大文件一键还原脚本

#### 定位

`reassemble.sh` 位于项目根目录，是用于将因 GitHub 100 MB 单文件限制而拆分的多个大文件还原为原始文件的**自动化 shell 脚本**。受影响的文件分布于 `linux-kernel/` 和 `bird/configs/` 子目录中。

#### 受影响的文件汇总

| 子项目 | 原始文件 | 分块数 | 总大小 |
|--------|---------|--------|--------|
| linux-kernel | `linux-src-5.4.0-ipswen-v1-20240124.tar.gz` | 2 | 171 MB |
| bird/configs | `node1_IPv4_1000000.conf` | 2 | 150 MB |
| bird/configs | `node1_IPswen_level0_1000000.conf` | 2 | 152 MB |
| bird/configs | `node1_IPswen_level1_1000000.conf` | 2 | 155 MB |
| bird/configs | `node1_IPswen_level2_1000000.conf` | 2 | 158 MB |
| bird/configs | `node1_IPswen_level3_1000000.conf` | 2 | 161 MB |
| bird/configs | `node1_IPswen_level4_1000000.conf` | 2 | 165 MB |
| bird/configs | `node1_IPswen_level5_1000000.conf` | 2 | 168 MB |
| bird/configs | `node1_IPswen_level6_1000000.conf` | 2 | 172 MB |
| bird/configs | `node1_IPswen_level7_1000000.conf` | 2 | 175 MB |

**共计**: 10 个文件，21 个分块，还原后总计约 1.7 GB。

#### 使用方法

```bash
cd /home/ubuntu/ipswen-project/
bash reassemble.sh
```

脚本自动扫描所有 `.part*` 文件，将其按序拼接还原，并删除中间分块文件以释放磁盘空间。

---

## 系统依赖关系

### 编译顺序依赖

各子项目之间存在严格的编译顺序依赖关系。以下列出正确的构建顺序：

```
第一步: libs/ (libbgpstream, wandio)
        ↓
第二步: linux-kernel/ (编译安装 IPSWEN 内核 + 头文件包)
        ↓
第三步: iproute2-5.5.0/ + net-tools-1.60/ + unpv13e/
        (依赖第二步的头文件)
        ↓
第四步: bird/ (依赖第二步的头文件)
        ↓
第五步: netmap/ + optmap/ (依赖第二步的内核源码树)
        ↓
第六步: fakedns/ + happyfootball/ (无内核头文件依赖，可独立编译)
        ↓
第七步: autoconfig/ + mrtdb/ (Python 项目，无编译依赖)
```

### 运行时依赖

```
bird ──────────► linux-kernel (Netlink FIB 注入)
bird ──────────► autoconfig (BGP 对等体间 GRE 隧道)
mrtdb ─────────► bird (通过 generate_bird_config.py 提供配置)
mrtdb ─────────► autoconfig (探测节点间的隧道)
mrtdb ─────────► libs/ (libbgpstream + libwandio)
happyfootball ─► linux-kernel (IPSWEN Socket API)
happyfootball ─► autoconfig (多路径隧道)
iproute2 ──────► linux-kernel (Netlink 通信)
net-tools ─────► linux-kernel (ioctl/Netlink 通信)
unpv13e ───────► linux-kernel (IPSWEN Socket API)
fakedns ───────► (独立运行，仅依赖 glibc LD_PRELOAD)
netmap ────────► linux-kernel (内核模块依赖)
optmap ────────► linux-kernel (eBPF/XDP 子系统)
```

---

## 编译与部署总览

### 完整环境构建

以下给出从头构建完整 IPSWEN 实验平台的步骤概览。假设起点为一台运行 Ubuntu 20.04 LTS（amd64）的物理机或虚拟机。

**阶段一：系统准备**

```bash
sudo apt update
sudo apt install -y build-essential gcc g++ make cmake autoconf automake \
    libtool pkg-config libncurses-dev bison flex libssl-dev libelf-dev \
    python3 python3-pip python3-venv libcap-dev libpcap-dev clang llvm \
    libbpf-dev sshpass
```

**阶段二：内核编译与安装**

```bash
cd linux-kernel/linux-src-5.4.0-ipswen-v1-20240124/
cp /boot/config-$(uname -r) .config
make olddefconfig
make -j$(nproc) && make modules_install && make headers_install
make install
sudo reboot
```

**阶段三：用户态工具编译**

```bash
# iproute2
cd iproute2-5.5.0/ && ./configure && make -j$(nproc) && sudo make install

# net-tools
cd net-tools-1.60/ && make -j$(nproc)

# unpv13e
cd unpv13e/ && ./configure && cd lib/ && make && cd ../ping/ && make

# bird
cd bird/src/ && autoreconf -i && ./configure && make -j$(nproc)

# fakedns
cd fakedns/ && make

# happyfootball
cd happyfootball/ && g++ -std=c++11 -o mysocket mysocket.cpp && \
    g++ -std=c++11 -o send send.cpp && g++ -std=c++11 -o test test.cpp
```

**阶段四：数据平面与第三方库**

```bash
# libs
cd libs/libbgpstream-2.2.0/ && ./configure && make -j$(nproc) && sudo make install
cd libs/wandio-4.2.4-1/ && ./configure && make -j$(nproc) && sudo make install

# netmap
cd netmap/ && ./configure && make -j$(nproc) && sudo make install

# optmap (eBPF/XDP)
cd optmap/ && make
```

**阶段五：大文件还原**

```bash
cd /home/ubuntu/ipswen-project/
bash reassemble.sh
```

**阶段六：验证**

```bash
# 检查内核
uname -r | grep ipswen

# 检查 IPSWEN 头文件
ls /usr/include/net/ipswen.h

# 验证 iproute2
ip route add 10.0.0.0.2.128.129/33 via 192.168.1.1 && ip route show | grep "("

# 检查 BIRD
bird/src/bird -l &
bird/src/birdc show status
killall bird
```

---

## 实验工作流

### 典型实验场景一：IPSWEN 路由收敛时间测量

1. 在 2 个节点上部署 IPSWEN 内核与 BIRD。
2. 使用 `autoconfig` 建立两节点间的 GRE 隧道。
3. 使用 `generate_bird_config.py`（mrtdb）生成 100 万条 IPSWEN 路由。
4. 在两节点上加载 BIRD 配置并建立 BGP 对等。
5. 用 `birdc show route count` 监控路由表增长，记录从 BGP 会话建立到路由表稳定的时间差。
6. 对 $L = 0$ 到 $L = 7$ 各重复实验，绘制 level 与收敛时间的函数关系。

### 典型实验场景二：IP 选项对 TCP MSS 的影响

1. 使用 `autoconfig` 建立两个节点间的 GRE 隧道。
2. 在节点 A 运行 `happyfootball/send`，指定不同 IP 选项大小（0, 4, 8, 12, 20, 40 字节）。
3. 在节点 B 运行 `happyfootball/test` 作为接收端。
4. 记录每种选项大小下的 MSS 值（`getsockopt(TCP_MAXSEG)`）。
5. 对比理论计算值与实测值，分析中间设备对 IP 选项的处理行为。

### 典型实验场景三：全球路由连通性扫描

1. 使用 `mrtdb/BGPdump.py` 从 RIPE RIS 获取最新 BGP 路由快照。
2. 使用 `mrtdb/generate_bird_config.py` 生成 BIRD 配置文件。
3. 在多节点实验床上部署 BIRD 并建立全网状 BGP 对等。
4. 使用 `mrtdb/ping.py` 扫描全球路由前缀，记录响应率与延迟。
5. 使用 `mrtdb/notebooks/` 中的 Jupyter 笔记本进行统计汇总与可视化。

---

## 注意事项

### 系统层面

- **内核版本锁定**: IPSWEN 修改基于 Linux 5.4.0 LTS。在不同小版本（如 5.4.1）上编译可能需要手动解决合并冲突。
- **磁盘空间**: 内核完整编译需要约 20 GB 空闲空间；还原所有大文件后需额外约 1.7 GB；bird 的百万路由配置加载后内存占用可达 15 GB。
- **根权限**: 绝大多数操作（内核编译安装、iproute2 安装、GRE 隧道创建、LD_PRELOAD 注入、eBPF 程序加载）均需 root 权限。
- **网络隔离**: IPSWEN 实验应仅在受控网络环境中进行。IPSWEN SAFI=127 的 BGP 路由**绝对不应**泄露到公共互联网——私有 AS 号（65150/65151）提供了一定保护，但不能完全排除配置错误导致的泄露风险。

### 安全层面

- **内核修改风险**: 修改后的内核可能存在未发现的稳定性问题。建议在虚拟机中先行测试，并保留可回退的旧内核。
- **LD_PRELOAD 注入**: fakedns 的 LD_PRELOAD 机制会劫持目标程序的全部 DNS 相关调用。在多线程程序中使用时需注意线程安全性。
- **eBPF 程序**: optmap 的 XDP 程序直接运行在内核上下文，代码错误可能导致数据包丢失或内核崩溃。

### 兼容性

- IPSWEN 的各项修改均为**附加式**（additive），不影响标准 IPv4/IPv6 路径。
- bird 在无 IPSWEN 头文件时通过条件编译退化为上游标准版本。
- fakedns 的伪 IPv4 地址使用 `44.0.0.0/8` 范围（原为 AMPRNET 业余无线电网络保留，现已部分回收），在公共互联网上不可路由。

---

## 附录 A: 快速命令参考卡片

### IPSWEN 内核

```bash
# 编译安装
cd linux-kernel/linux-src-5.4.0-ipswen-v1-20240124/
make -j$(nproc) && make modules_install && make headers_install && make install

# 验证
uname -r | grep ipswen
cat /proc/net/fib_trie | grep "("

# 调试
sudo dmesg -wT | grep -i ipswen
echo 'file net/ipv4/* +p' | sudo tee /sys/kernel/debug/dynamic_debug/control
```

### iproute2 IPSWEN

```bash
# 添加路由
sudo ip route add 10.0.0.0.2.128.129/33 via 192.168.1.1

# 查看 IPSWEN 路由
ip route show | grep "("

# 删除
sudo ip route del 10.0.0.0.2.128.129/33
```

### BIRD 控制

```bash
# 启动
sudo ./bird -c configs/bird.conf

# birdc 常用命令
show protocols             # 协议状态
show route count           # 路由总数
show bgp sessions          # BGP 邻居
show memory                # 内存用量
configure soft             # 热重载
shutdown                   # 退出
```

### autoconfig

```bash
# Hub 节点
sudo python3 server.py &

# Spoke 节点
sudo python3 client.py connect 192.168.1.253
sudo python3 client.py disconnect 192.168.1.253
```

### fakedns

```bash
# 单程序注入
LD_PRELOAD=./fakedns.so curl https://example.com/

# 全局注入（谨慎）
export LD_PRELOAD=/path/to/fakedns.so
```

### happyfootball

```bash
# 测试不同 IP 选项大小
for s in 0 4 8 12 20 40; do
    ./send 192.168.10.2:80 $s
done
```

---

## 附录 B: 各子项目 README 行数统计

以下列出各子项目之 README 文档行数，以反映各子项目的文档详尽程度：

| 子项目 | README.md 行数 |
|--------|---------------|
| mrtdb | 1,021 |
| fakedns | 959 |
| netmap | 938 |
| optmap | 866 |
| bird | 459 |
| linux-kernel | 450 |
| happyfootball | 359 |
| autoconfig | 293 |
| iproute2-5.5.0 | 292 |
| unpv13e | 267 |
| net-tools-1.60 | 244 |
| libs | 225 |
| **合计** | **6,373** |

---

## 附录 C: 文件清单

### 项目根目录

| 路径 | 类型 | 说明 |
|------|------|------|
| `README.md` | 文件 | 本文档 |
| `reassemble.sh` | Shell 脚本 | 大文件一键还原 |

### 各子项目关键文件

| 子项目 | 关键文件 / 目录 |
|--------|---------------|
| `linux-kernel/` | `linux-src-5.4.0-ipswen-v1-20240124/`, `*.tar.gz.part*` |
| `iproute2-5.5.0/` | `lib/utils.c`, `ip/iproute.c` |
| `net-tools-1.60/` | `ifconfig.c`, `route.c`, `netstat.c` |
| `bird/` | `src/`, `configs/` (含 `.part*` 文件), `bin/` |
| `mrtdb/` | `BGPdump.py`, `srv.py`, `cli.py`, `ping.py`, `generate_bird_config.py`, `cmrtdb/`, `notebooks/` |
| `autoconfig/` | `server.py`, `client.py` |
| `happyfootball/` | `mysocket.cpp`, `mysocket.hpp`, `send.cpp`, `test.cpp` |
| `fakedns/` | `fakedns.cpp`, `patch.h`, `Makefile`, `benchmark*.cpp` |
| `netmap/` | `configure`, `drivers.mak`, 各驱动源码 |
| `optmap/` | eBPF/XDP 源代码与 Makefile |
| `libs/` | `libbgpstream-2.2.0/`, `wandio-4.2.4-1/` |
| `unpv13e/` | `ping/`, `lib/` |

---

## 附录 D: 联系与引用

本实验平台为学术研究目的而构建。各子项目的 README 文件中包含更详细的编译说明、调试指南与 API 参考。对于特定子项目的深入问题，应首先查阅对应子目录中的 README.md 文档。

研究使用本平台时，建议明确说明所用内核版本（Linux 5.4.0-ipswen-v1）、各工具的修改版本号以及所用实验拓扑。

---

*IPSWEN 项目文档完。*
