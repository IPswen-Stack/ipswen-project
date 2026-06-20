# linux-kernel —— IPSWEN 定制版 Linux 5.4.0 内核

## 概述

本目录包含一个打过 **IPSWEN** 补丁的 **Linux 5.4.0 LTS** 完整内核源码树，以及预编译的 Debian 安装包（`.deb`）。IPSWEN 是一种实验性 IPv4 地址扩展技术——在传统 4 字节 IPv4 地址基础上增加了一个带符号的层次 "level" 维度和对应的扩展字节，实现地址空间的树状扩展。

内核修改涉及网络协议栈的六个关键层面，是整个 IPSWEN 研究生态的**系统基石**——所有用户态工具（iproute2、net-tools、BIRD、unpv13e）都依赖这些内核修改提供的系统调用和数据结构。

---

## 目录结构

```
linux-kernel/
├── linux-src-5.4.0-ipswen-v1-20240124/     # 完整内核源码树
│   ├── net/
│   │   ├── ipv4/         # ★ IPv4 协议栈核心（路由、FIB、IP 选项）
│   │   ├── ipv6/         # IPv6 栈
│   │   ├── core/         # Socket 层、Netlink
│   │   ├── netfilter/    # Netfilter 钩子
│   │   └── bridge/       # 桥接
│   ├── include/
│   │   ├── net/ipswen.h  # ★ IPSWEN 地址结构体
│   │   └── uapi/linux/rtnetlink.h  # ★ RTA_DST_EXT 属性
│   ├── arch/    # x86, ARM 等架构
│   ├── block/ certs/ crypto/ drivers/ fs/ mm/ kernel/
│   ├── KernelCodeSense.txt  # IPSWEN 修改涉及的文件索引
│   ├── Makefile / .config
│   └── README
│
├── linux-src-5.4.0-ipswen-v1-20240124.tar.gz    # 源码包
├── linux-headers-5.4.0-ipswen-v1-20240124_001_amd64.deb  # 头文件包
└── linux-image-5.4.0-ipswen-v1-20240124_001_amd64.deb   # 内核镜像包
```

---

## 内核版本号

```
5.4.0  -ipswen  -v1       -20240124
  │       │       │           │
  │       │       │           └ 构建日期: 2024年1月24日
  │       │       └ IPSWEN 补丁版本号
  │       └ IPSWEN 补丁系列标识
  └ 上游 Linux 5.4 LTS
```

基于 5.4 LTS（Long Term Support），该版本在 2025 年 12 月前持续获得安全更新，为实验提供了稳定的基础。

---

## IPSWEN 内核修改的六个层面

### 第 1 层：地址数据结构

在 `include/net/ipswen.h` 和其他头文件中定义：

```c
struct ipswen_ext {
    s8 level;              // 有符号层次（-IPSWEN_MAX_LEVEL..+IPSWEN_MAX_LEVEL）
    u8 ext[IPSWEN_MAX_LEVEL];  // 扩展字节数组
};

struct ipswen_addr {
    u32 base_ip;           // 基础 IPv4 地址（4 字节）
    struct ipswen_ext ext; // 扩展部分
};
```

IPSWEN 地址在内存中占用 4（base）+ 1（level）+ N（ext 字节）字节。

### 第 2 层：Socket 地址扩展

```c
// 扩展后的 sockaddr_in
struct sockaddr_in {
    sa_family_t sin_family;  // AF_INET
    in_port_t sin_port;
    struct in_addr sin_addr;
    // ... 原有的 ...
    struct ipswen_addr swen_addr;  // ★ 新增 IPSWEN 扩展
};
```

应用可以通过标准的 `bind()`/`connect()`/`sendto()` 使用 IPSWEN 地址，Socket 层自动识别。

### 第 3 层：FIB（转发信息库）扩展

`net/ipv4/fib_semantics.c`、`net/ipv4/fib_frontend.c`、`net/ipv4/route.c`：

- 路由查找时，前缀匹配算法包含 IPSWEN 的扩展字节
- 下一跳（nexthop/gateway）可以是 IPSWEN 地址
- `fib_lookup()` 返回的结果中包含 IPSWEN 相关信息

### 第 4 层：Netlink 协议扩展

`include/uapi/linux/rtnetlink.h`：
```c
#define RTA_DST_EXT  (RTA_MAX + 1)   // 新路由属性
```

`net/core/rtnetlink.c` 中处理此属性，允许 `ip route` 等命令通过 Netlink 向内核传递 IPSWEN 路由。

### 第 5 层：Socket 选项

`net/core/sock.c`：
- `IP_OPTIONS` setsockopt 在 IPSWEN 套接字上正常工作
- `SO_BINDTODEVICE` 允许绑定到特定网络设备

### 第 6 层：地址验证

`net/ipv4/devinet.c` 等文件中验证：
- Level 值在合法范围内
- 扩展字节数量 = |level|
- 地址不与标准 IPv4 冲突

---

## 内核-用户态交互架构

```
┌─────────────────────────────────────────┐
│              用户态 (Userspace)          │
│  ┌──────────┐ ┌───────────┐ ┌────────┐  │
│  │ iproute2 │ │ net-tools │ │  BIRD  │  │
│  │ (ip命令) │ │(ifconfig) │ │(bgp)   │  │
│  └────┬─────┘ └─────┬─────┘ └───┬────┘  │
│       │             │           │        │
│       ▼             ▼           ▼        │
│  ┌──────────────────────────────────┐    │
│  │   Netlink Socket (NETLINK_ROUTE) │    │
│  └──────────────┬───────────────────┘    │
├─────────────────┼───────────────────────┤
│                 ▼            内核态      │
│  ┌──────────────────────────────────┐    │
│  │  IPSWEN 扩展 FIB (路由子系统)     │    │
│  │  - 前缀匹配含 level+扩展字节      │    │
│  │  - RTA_DST_EXT 属性处理          │    │
│  │  - IPSWEN nexthop 处理           │    │
│  └──────────────┬───────────────────┘    │
│                 ▼                        │
│  ┌──────────────────────────────────┐    │
│  │  网络设备驱动层                    │    │
│  └──────────────────────────────────┘    │
└─────────────────────────────────────────┘
```

---

## KernelCodeSense.txt 说明

此文件是 IPSWEN 相关内核源文件的索引。它列出了 `block/`、`certs/`、`crypto/` 等子系统的文件——暗示 IPSWEN 修改的触及面不限于纯网络栈。可能与内核模块签名（`certs/`）、加密 API 兼容性（`crypto/`）或存储层（`block/`）有关。

---

## 从源码编译（完整指南）

### 第一步：安装编译依赖

```bash
sudo apt update
sudo apt install -y \
    build-essential gcc g++ binutils make \
    libncurses-dev libssl-dev \
    flex bison bc rsync kmod cpio \
    dwarves elfutils libelf-dev
```

### 第二步：配置内核

```bash
cd linux-src-5.4.0-ipswen-v1-20240124

# 方法 A: 使用已有 .config
make olddefconfig

# 方法 B: 交互式配置
make menuconfig
# 推荐检查以下选项：
#   CONFIG_NET_IPGRE=y/m (GRE 隧道)
#   CONFIG_IP_ADVANCED_ROUTER=y
#   CONFIG_IP_MULTIPLE_TABLES=y
```

### 第三步：编译

```bash
# 编译内核（使用所有 CPU 核心）
make -j$(nproc) 2>&1 | tee build_kernel.log

# 编译模块
make modules -j$(nproc) 2>&1 | tee build_modules.log

# 检查错误
grep -i "error:" build_*.log
```

**时间预估：**
- 8 核 CPU: ~30-40 分钟
- 4 核 CPU: ~60-90 分钟
- 2 核 CPU: ~2-3 小时

**磁盘空间：** 完整构建约需 15-18 GB。

### 第四步：安装

```bash
sudo make modules_install
sudo make install
sudo update-grub
sudo reboot
```

### 第五步：验证

```bash
uname -r
# 应输出: 5.4.0-ipswen-v1-20240124

# 检查 IPSWEN 支持
cat /proc/sys/net/ipv4/ipswen_enabled  # 如果存在
# 或
dmesg | grep -i ipswen
```

### 只编译网络栈（快速验证修改）

```bash
# 只重新编译网络子系统（不编译整个内核）
make net/ipv4/
make net/core/
# 然后手动替换模块或重启
```

---

## 预编译包安装

```bash
# 安装内核镜像（需重启）
sudo dpkg -i linux-image-5.4.0-ipswen-v1-20240124_001_amd64.deb
sudo update-grub
sudo reboot

# 安装头文件（编译用户态工具的依赖）
sudo dpkg -i linux-headers-5.4.0-ipswen-v1-20240124_001_amd64.deb

# 验证头文件
ls /usr/src/linux-headers-5.4.0-ipswen-v1-20240124/include/net/ipswen.h
```

---

## 实验定制

### 怎样修改最大 level

```bash
# 编辑头文件
vim include/net/ipswen.h
# 找到 IPSWEN_MAX_LEVEL
# 默认: #define IPSWEN_MAX_LEVEL 7
# 改为: #define IPSWEN_MAX_LEVEL 15

# 重新编译
make net/ipv4/ && make modules && sudo make modules_install && sudo reboot
```

### 怎样添加新的 Netlink 属性

```bash
# 1. 在 include/uapi/linux/rtnetlink.h 中添加
#define RTA_MY_ATTR (RTA_MAX + 2)

# 2. 在 net/core/rtnetlink.c 的 rtm_ipv4_policy 中添加处理
# 3. 重新编译
```

### 怎样调试内核 IPSWEN 路由

```bash
# 启用动态调试
echo 'file net/ipv4/route.c +p' | sudo tee /sys/kernel/debug/dynamic_debug/control
echo 'file net/ipv4/fib_semantics.c +p' | sudo tee -a /sys/kernel/debug/dynamic_debug/control

# 监控
sudo dmesg -wT | grep -i ipswen

# 用 ftrace 追踪
cd /sys/kernel/debug/tracing
echo function > current_tracer
echo '*ipswen*' > set_ftrace_filter
cat trace_pipe
```

### 怎样验证 IPSWEN 路由

```bash
# 添加 IPSWEN 路由（需要 iproute2-5.5.0）
sudo ip route add 10.0.0.0(2)128.129/33 via 192.168.1.1

# 查看内核 FIB
cat /proc/net/fib_trie | grep "("

# 查看路由缓存
ip route show cache | grep "("
```

---

## 内核模块签名（可选）

如果启用了 Secure Boot 或内核模块签名：

```bash
# 生成密钥
openssl req -new -nodes -utf8 -sha512 -days 36500 \
    -batch -x509 -outform PEM \
    -out kernel_key.pem \
    -keyout kernel_key.pem

# 签名模块
scripts/sign-file sha512 kernel_key.pem kernel_key.x509 <module>.ko
```

---

## 排查常见问题

| 问题 | 诊断 | 解决 |
|------|------|------|
| 新内核无法启动 | GRUB 选旧内核进入 | 检查 `dmesg` 日志 |
| `make menuconfig` 乱码 | 终端不支持 | `export TERM=xterm` |
| 编译 OOM | 并行数多 | `make -j2` 降低并行 |
| 模块签名失败 | Secure Boot | 进 BIOS 禁用 Secure Boot |
| 头文件无 `ipswen.h` | 未装 headers 包 | `sudo dpkg -i linux-headers-*.deb` |
| `ip route` 不认识 `(` 记法 | iproute2 未用 IPSWEN 版 | 安装 `../iproute2-5.5.0/` |

---

## 注意事项

- 5.4 LTS 内核，IPSWEN 修改是**附加式**的——不影响标准 IPv4/IPv6 功能
- `.deb` 包仅适用于 **amd64** 架构 (x86_64)
- 头文件包是编译 iproute2、net-tools 等工具的前置条件
- 内核源码树约 800-900 MB，完整编译后约 15-18 GB
- 如果遇到 Kernel Panic，在 GRUB 菜单中选择旧内核即可回退

---

## 相关项目

- **iproute2-5.5.0** —— 与本内核交互的 `ip` 命令
- **net-tools-1.60** —— `ifconfig`/`route`
- **bird** —— 向本内核 FIB 注入 IPSWEN 路由的 BGP 守护进程
- **unpv13e** —— 使用本内核 IPSWEN Socket API 的示例代码
- **happyfootball** —— Socket 级 IP 选项实验

---

## 附录 A: 内核配置关键选项

编译前确认以下配置（`make menuconfig`）：
```
[*] Networking support → Networking options →
    [*] TCP/IP networking
    [*]   IP: advanced router
    [*]   IP: policy routing
    [*]   IP: equal cost multipath
    [*]   IP: verbose route monitoring
    <M>   IP: tunneling
    <M>   IP: GRE demultiplexer
    <M>   IP: GRE tunnel over IP
```

## 附录 B: 内核日志关键字

调试时搜索 `dmesg` 的关键字：
- `ipswen` — IPSWEN 相关日志
- `RTNETLINK` — Netlink 错误
- `FIB` — 转发信息库
- `ICMP` — ICMP 消息
- `GRE` — GRE 隧道

```bash
# 实时监控
sudo dmesg -wT | grep -E "ipswen|RTNETLINK|FIB"

# 查看启动日志
sudo journalctl -k | grep -i ipswen
```

## 附录 C: 快速内核回退

如果新内核无法启动：
1. 重启，在 GRUB 菜单选择 "Advanced options"
2. 选择旧内核（之前能启动的版本）
3. 进入系统后移除问题内核：
```bash
dpkg -l | grep linux-image
sudo apt remove linux-image-5.4.0-ipswen-v1-20240124
sudo update-grub
```

---

## 附录 D: 大文件分块说明（重要！）

### 为什么源码包被分块？

`linux-src-5.4.0-ipswen-v1-20240124.tar.gz` 大小为 **171 MB**，超过 GitHub 的 100 MB 限制。已拆分为 2 个 95MB 的分块文件。

### 受影响的文件

| 原始文件 | 大小 | 分块 |
|---------|------|------|
| `linux-src-5.4.0-ipswen-v1-20240124.tar.gz` | 171 MB | `.part00` + `.part01` |

**重要**: `linux-src-5.4.0-ipswen-v1-20240124/` 目录中的完整内核源码树不受影响——它是 git 仓库的一部分。只有 `.tar.gz` 被分块。

### 还原方法

**方法 1: 一键还原（推荐）**
```bash
cd /path/to/ipswen-project
bash reassemble.sh
```

**方法 2: 手动还原**
```bash
cd linux-kernel
cat linux-src-5.4.0-ipswen-v1-20240124.tar.gz.part00 \
    linux-src-5.4.0-ipswen-v1-20240124.tar.gz.part01 \
    > linux-src-5.4.0-ipswen-v1-20240124.tar.gz

tar tzf linux-src-5.4.0-ipswen-v1-20240124.tar.gz | head -5   # 验证
```

### 验证

```bash
ls -lh linux-kernel/linux-src-5.4.0-ipswen-v1-20240124.tar.gz   # 应为约 171 MB
tar tzf linux-kernel/linux-src-5.4.0-ipswen-v1-20240124.tar.gz | wc -l
```

### 注意事项

- 完整的源码树已在仓库中——**如果只需要编译和修改内核，不需要还原 `.tar.gz`**
- `.tar.gz` 是备份副本，可用于在其他机器快速部署、归档保存、生成 checksum
