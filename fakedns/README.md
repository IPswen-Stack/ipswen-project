# fakedns —— 基于 DNS 劫持的 IPv4-to-IPv6/IPSWEN 透明代理（Shim 层）

## 概述

`fakedns`（又名 **shim**）是一个基于 `LD_PRELOAD` 动态库注入技术的**透明网络协议转换层**。它劫持应用程序的 POSIX socket 系统调用（`getaddrinfo`、`gethostbyname`、`connect`、`close` 等），在不修改应用程序源代码的前提下，使仅支持 IPv4 的遗留应用程序能够透明地访问 IPv6 服务器和 IPSWEN 扩展地址。

**核心问题：** 大量遗留应用程序只调用 `getaddrinfo(..., AF_INET, ...)` 请求 IPv4 地址，直接无法与纯 IPv6 服务器通信。`fakedns` 通过伪造 DNS 解析结果（返回 240.0.0.0/8 范围内的"虚拟 IPv4"地址）和拦截 `connect()` 系统调用（将虚拟 IPv4 连接重定向到真实 IPv6/IPSWEN 地址），实现了对应用完全透明的协议转换。

**为什么叫 "fakedns"：** 因为它伪造（fake）了 DNS 解析结果。应用程序以为自己获得的 `192.168.1.1` 或 `240.0.0.5` 是一个真实的 IPv4 地址，但实际上这个地址在 `fakedns` 内部映射到了一个真实的 IPv6 地址或 IPSWEN 扩展地址。当应用程序用这个假地址发起连接时，`fakedns` 拦截 `connect()` 系统调用，替换为目标真实地址族（AF_INET6 或带 IP 选项的 AF_INET）。

**为什么叫 "shim"：** 因为它像一层薄垫片（shim）插入在应用程序和操作系统内核之间，对上下双方都是透明的。

---

## 目录结构

```
fakedns/
├── shim.cpp                # ★ 核心源码：DnsMapper 单例 + 8 个系统调用劫持
├── patch.h                 # PATCH_FUNC 宏模板（通用函数劫持工具）
├── fakedns.cpp             # 旧版源码（gzip 压缩，已损坏，由 shim.cpp 替代）
├── fakedns.so              # Debug 版共享库（含调试日志输出）
├── libfakedns.so           # Release 版共享库
├── fakedns_release.so      # Benchmark 专用发布版（无调试输出）
├── Makefile                # 编译脚本
│
├── test_server.cpp         # IPv6-only TCP echo 服务器（测试目标）
├── test_server             # 编译产物
├── test_client.cpp         # AF_INET 客户端（使用 getaddrinfo）
├── test_client             # 编译产物
├── test_client_legacy.cpp  # AF_INET 客户端（使用 gethostbyname，老式 API）
├── test_client_legacy      # 编译产物
│
├── benchmark.cpp           # ★ 系统调用级性能基准测试（8 个 hook 的延迟测量）
├── benchmark               # 编译产物
├── benchmark_mapper.cpp    # ★ 映射表数据结构性能基准测试
├── benchmark_mapper        # 编译产物
├── plot_benchmark.py       # 基准测试结果可视化（Python + matplotlib）
│
├── run_test.sh             # 端到端功能测试脚本
├── run_benchmark.sh        # 基准测试运行脚本
│
├── DnsMapper_Performance.png   # 映射表性能图表（PNG，300 DPI）
└── DnsMapper_Performance.pdf   # 映射表性能图表（PDF 矢量）
```

---

## 核心架构：三层地址映射模型

```
┌──────────────────────────────────────────────────────┐
│              应用程序（未修改的 IPv4-only 代码）        │
│  getaddrinfo("ip6-localhost", AF_INET) → 240.0.0.5  │
│  connect(240.0.0.5:8080)                            │
└──────────────────┬───────────────────────────────────┘
                   │
    ┌──────────────▼──────────────┐
    │   fakedns (LD_PRELOAD)      │
    │                             │
    │  ┌───────────────────────┐  │
    │  │    DnsMapper 单例     │  │
    │  │                       │  │
    │  │  reverse_:            │  │
    │  │    RealTarget → VIP   │  │  ← getaddrinfo/gethostbyname 写入
    │  │                       │  │
    │  │  mappings_:           │  │
    │  │    VIP → MappingEntry │  │  ← connect() 读取
    │  │                       │  │
    │  │  socket_targets_:     │  │
    │  │    fd → RealTarget    │  │  ← connect() 写入, close() 清理
    │  │                       │  │
    │  │  free_ips_ (LIFO)     │  │  ← VIP 池 (65536 个)
    │  │  lru_ (过期回收)       │  │
    │  └───────────────────────┘  │
    │                             │
    │  劫持的 8 个系统调用：        │
    │  getaddrinfo, gethostbyname, │
    │  connect, close,             │
    │  read, recv, send, write     │
    └──────────────┬──────────────┘
                   │ 真实 AF_INET6 connect() 或
                   │ AF_INET connect() + IP_OPTIONS
    ┌──────────────▼──────────────┐
    │     Linux Kernel            │
    │  (AF_INET6 / AF_INET        │
    │   + IPSWEN IP_OPTIONS)      │
    └─────────────────────────────┘
```

---

## 核心源码完整解析

### 1. 地址命名约定与编码方案

#### 虚拟 IPv4 地址池（VIP）

```cpp
constexpr uint32_t VIP_FIRST_HOST = 0xF0000001u;     // 240.0.0.1 (host order)
constexpr uint32_t VIP_LAST_EXCLUSIVE = 0xF0010001u;  // 240.1.0.1 (exclusive)
constexpr uint32_t VIP_MASK_BASE = 0xF0000000u;       // 240.0.0.0/8
constexpr uint32_t VIP_MASK_LIMIT = 0x00010000u;      // 65536 个 VIP
```

**VIP 池的大小推导：**
- `0xF0010000 - 0xF0000000 = 0x00010000 = 65536`
- 使用 240.0.0.0/8（Class E 实验地址段），这些地址在公网上不会被正常使用
- `DnsMapper` 构造函数中以 LIFO 顺序初始化：从 `0xF0010000u` 递减到 `0xF0000000u`（共 65536 个）

**选择 240.0.0.0/8 的原因：** RFC 1112 将 240.0.0.0/4 保留为"未来使用"（Class E），实际互联网路由器通常丢弃发往这些地址的数据包。在实验环境中使用这些地址不会与真实网络地址冲突。

#### RealTarget —— 真实目标地址的联合体

```cpp
struct RealTarget {
    uint8_t kind = 0;  // 0=IPv6, 1=IPv4-with-extension
    uint8_t pad1[3]{};
    union {
        struct { uint32_t base_ipv4_host_order; uint32_t ext; uint32_t pad2; uint32_t pad3; } v4;
        uint8_t ipv6[16];
    } u{};
};
```

**两个目标类型的语义：**

| kind | 类型 | 内存布局 | 示例 |
|------|------|----------|------|
| 0 | 纯 IPv6 | `u.ipv6[0..15]` = 128 位 IPv6 地址 | `::1` → `u.ipv6` |
| 1 | IPv4 + 扩展 | `u.v4.base_ipv4_host_order` = IPv4；`u.v4.ext` = 扩展字节 | `192.168.1.1(2)0.99` → kind=1, base=192.168.1.1, ext=0x0063 |

**`to_string()` 方法的编码细节：**
```cpp
std::string to_string() const {
    // kind=0 (IPv6): 直接用 inet_ntop(AF_INET6)
    // kind=1 (IPv4+ext): 格式 "A.B.C.D(2)E"，其中 E 是 ext 的十进制值
    //   注：level 硬编码为 2——这意味着总是有 2 个扩展字节
    return std::string(buf) + "(2)" + std::to_string(u.v4.ext);
}
```

**如何修改以支持不同 level：** 如果你的地址格式有不同数量的扩展字节（如 level=3 表示 3 字节扩展），需要在 `to_string()` 中动态计算 level 值，并在 `parse_ipv4_with_ext_target()` 中支持从输入中读取 level：

```cpp
// 示例修改
uint32_t level = 0;
parse_decimal_token(level_str, level);
// to_string: buf + "(" + std::to_string(level) + ")" + format_ext(ext, level)
```

#### IPv4 扩展地址的文本表示

支持两种伪 IPv4+扩展的文本格式（在 `parse_ipv4_with_ext_target()` 中解析）：

**格式 A：括号表示法**
```
1.2.3.4(2)0.99
 └─┬──┘ │ └─┬─┘
   │    │   └── 扩展字节（点分十进制）
   │    └────── level（扩展字节数量）
   └─────────── 基础 IPv4
```
- `(2)` 表示 level=2（有两个扩展字节）
- `0.99` 被解析为 `ext = 0x0063`（0 是高位字节，99 是低位字节，大端序）

**格式 B：.ext 后缀法**
```
1.2.3.4.ext99
       └─┬──┘
         └──── 扩展字节（level=1 隐含）
```
- `.ext` 标记后跟十进制数值，自动设置 level=1
- 适用于单个扩展字节的简单场景

**扩展字节的解析算法（`parse_ext_components()`）：**
```cpp
bool parse_ext_components(const std::string &s, uint32_t &out) {
    out = 0;
    // 按 '.' 分割，每段解析为一个 0-255 的字节
    // 大端序组装：out = (out << 8) | byte
    // 例如 "0.99" → out = 0, byte0=0 → out=0; byte1=99 → out = (0<<8)|99 = 99 = 0x0063
}
```

---

### 2. DnsMapper 单例类 —— 核心映射引擎

#### 2.1 数据结构全景

`DnsMapper` 是线程安全的单例（使用 `pthread_mutex_t`），包含以下核心数据结构：

```
DnsMapper
│
├── free_ips_: std::vector<uint32_t>
│   └── LIFO 空闲 VIP 栈（初始 65536 个，从 0xF0010000 递减到 0xF0000000）
│
├── lru_: std::list<uint32_t>
│   └── LRU 双向链表（最近使用的 VIP 在头部，最久未使用的在尾部）
│
├── mappings_: std::map<uint32_t, MappingEntry>
│   └── VIP → {target, last_used, lru_it} 的正向映射（std::map = 红黑树，O(log n)）
│
├── reverse_: std::map<RealTarget, uint32_t>
│   └── RealTarget → VIP 的反向映射（用于查重：同一个目标不分配多个 VIP）
│
├── socket_targets_: std::map<int, RealTarget>
│   └── fd → RealTarget（记录每个 socket 对应的真实目标，用于未来 session 管理）
│
└── lock_: pthread_mutex_t
    └── 全局互斥锁（所有 DnsMapper 操作都在锁保护下进行）
```

#### 2.2 get_fake_ip() —— 正向映射（DNS 解析时调用）

```cpp
uint32_t get_fake_ip(const RealTarget &target) {
    pthread_mutex_lock(&lock_);
    const long now = time(nullptr);

    // Step 1: 检查是否已有映射（通过 reverse_ 查重）
    auto back = reverse_.find(target);
    if (back != reverse_.end()) {
        // 已有映射 → touch LRU 并返回
        touch_locked(vip, it->second, now);
        return vip;
    }

    // Step 2: 如果 free_ips_ 空了，尝试回收过期条目
    if (free_ips_.empty()) recycle_expired_locked(now);

    // Step 3: 仍然为空 → 返回 0（调用方负责处理）
    if (free_ips_.empty()) return 0;

    // Step 4: 从 free_ips_ 弹出最后一个（LIFO），创建新映射
    uint32_t vip = free_ips_.back();
    free_ips_.pop_back();
    create_mapping_locked(vip, target, now);
    return vip;
}
```

**时间复杂度分析：**
- `reverse_.find()`：O(log n)（std::map 红黑树）
- 无过期条目时 `recycle_expired_locked()` 是 O(1)（检查 lru_.back()）
- `create_mapping_locked()`：O(log n)（插入两个 std::map）
- **总体：O(log n)**，其中 n = 当前活跃映射数量（最多 65536）

#### 2.3 get_real_target() —— 反向映射（connect 时调用）

```cpp
bool get_real_target(uint32_t fake_ip_host_order, RealTarget &out) {
    pthread_mutex_lock(&lock_);
    auto it = mappings_.find(fake_ip_host_order);
    if (it == mappings_.end()) return false;  // 未找到 → 透传给真实 connect
    out = it->second.target;
    touch_locked(fake_ip_host_order, it->second, time(nullptr));  // 更新 LRU
    return true;
}
```

#### 2.4 LRU 过期回收机制

```cpp
constexpr long MAPPING_TTL = 300;  // 5 分钟

void recycle_expired_locked(long now) {
    while (!lru_.empty()) {
        uint32_t vip = lru_.back();  // LRU 尾部 = 最久未使用
        auto it = mappings_.find(vip);
        if (now - it->second.last_used < MAPPING_TTL) break;  // 遇到未过期的 → 停止
        // 清理反向映射、LRU 节点、正向映射，归还 VIP 到 free_ips_
        reverse_.erase(it->second.target);
        lru_.pop_back();
        mappings_.erase(it);
        free_ips_.push_back(vip);
    }
}
```

**设计原理：**
- LRU 链表保证最久未使用的条目在尾部，O(1) 找到候选过期条目
- 300 秒 TTL 适中——足够覆盖一个 TCP 连接的生命周期，但不会让死条目占用 VIP 过久
- 回收时**遇到第一个未过期条目就停止**（因为 LRU 链表按最近使用时间排序），避免全量扫描

#### 2.5 socket_targets_ —— Socket 目标跟踪

```cpp
void track_socket(int fd, const RealTarget &target) { socket_targets_[fd] = target; }
void untrack_socket(int fd) { socket_targets_.erase(fd); }
bool socket_target(int fd, RealTarget &target) { /* 查找并返回 */ }
```

**用途：** 在 `connect()` 成功（或 `EINPROGRESS` 非阻塞连接发起）后，记录 fd→target 映射。在 `close()` 时清理。这为未来可能的基于 fd 的读写优化（如在 `read()`/`write()` 中做额外处理）预留了基础设施。

---

### 3. 系统调用劫持 —— 8 个 intercepted 函数

#### 3.1 dlsym 延迟解析模式

所有真实系统调用通过 `dlsym(RTLD_NEXT, "函数名")` 延迟解析：

```cpp
template <typename T>
T must_resolve(const char *name) {
    void *p = dlsym(RTLD_NEXT, name);
    if (!p) { fprintf(stderr, "Error: could not find original function %s", name); exit(1); }
    return reinterpret_cast<T>(p);
}

// 使用示例（在函数体内部）：
static connect_fn_t real_connect = nullptr;  // 初始为 nullptr
extern "C" int connect(int sockfd, ...) {
    if (!real_connect) real_connect = must_resolve<connect_fn_t>("connect");
    // ...
}
```

**为什么用延迟解析而不是静态初始化：**
1. `LD_PRELOAD` 加载时，动态链接器可能尚未完成全局构造函数
2. 静态初始化顺序不确定——可能 `real_connect` 在 `dlsym` 可用前就被初始化
3. 延迟解析保证在首次调用时 `dlsym` 一定可用

#### 3.2 connect() —— 最复杂的劫持（核心分发逻辑）

```cpp
extern "C" int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    // Step 1: 解析目标地址族
    //   AF_INET  → 提取 IPv4 + port
    //   AF_INET6 → 提取 IPv6；如果是 IPv4-mapped IPv6 (::ffff:x.x.x.x)，提取嵌入的 IPv4
    //   其他     → 直接透传

    // Step 2: 检查目标 IPv4 是否在 240.0.0.0/8（VIP 范围）内
    //   不在 → 直接透传给真实 connect()

    // Step 3: 对 IPv6 输入检查 "Magic Number"（8 字节全 0x77）
    //   通过 → 表明数据包来自 fakedns 内部的重定向（dup2 后的连接）

    // Step 4: 调用 DnsMapper::get_real_target() 获取真实目标
    //   失败 → 透传

    // Step 5: 根据 target.kind 分发
    //   kind=1 (IPv4+ext):
    //     - 如果是 AF_INET 输入：设置 IP_OPTIONS，连到真实 IPv4
    //     - 如果是 AF_INET6 输入：创建 AF_INET socket，dup2 替换
    //   kind=0 (IPv6):
    //     - 如果是 AF_INET6 输入：直接连到真实 IPv6
    //     - 如果是 AF_INET 输入：创建 AF_INET6 socket，dup2 替换
}
```

**dup2 替换方案的设计精妙之处：**

当应用程序用 `AF_INET` socket 调用 `connect()`，但真实目标是 IPv6 时，不能直接改变已有 socket 的地址族。解决方案是：
1. 创建一个新的 `AF_INET6` socket（`tmp`）
2. 在新 socket 上执行 `connect()` 到真实 IPv6
3. 用 `dup2(tmp, sockfd)` 将新 socket 的文件描述符复制到原 fd 上
4. 关闭临时 socket `tmp`
5. 应用程序继续使用原来的 fd 号，但底层已经是 AF_INET6 socket

```cpp
// 示例：AF_INET → IPv6 的转换
int tmp = socket(AF_INET6, type, 0);         // 创建 IPv6 socket
int flags = fcntl(sockfd, F_GETFL, 0);        // 保存原 fd 的非阻塞标志
if (flags & O_NONBLOCK) fcntl(tmp, F_SETFL, flags);  // 同步非阻塞标志
int rc = real_connect(tmp, ..., sizeof(sin6)); // IPv6 连接
int e = errno;
if (rc == 0 || (rc < 0 && e == EINPROGRESS)) {
    dup2(tmp, sockfd);  // ★ 关键：替换 fd
    DnsMapper::instance().track_socket(sockfd, target);
}
close(tmp);
errno = e;
return rc;
```

**这种方法的局限性：**
- 仅适用于 TCP socket（`SOCK_STREAM`），对于 `SOCK_DGRAM`（UDP）可能需要不同的策略
- `dup2` 后原 socket 的某些 socket 选项可能丢失
- 如果应用程序在 connect 之前设置了特定的 socket 选项（如 `SO_RCVBUF`），这些选项不会自动传递到新 socket

#### 3.3 getaddrinfo() —— DNS 解析劫持

```cpp
extern "C" int getaddrinfo(const char *node, const char *service,
                            const struct addrinfo *hints, struct addrinfo **res) {
    // Step 1: 检查是否是 IPSWEN 扩展地址格式（"1.2.3.4(2)0.99"）
    if (parse_ipv4_with_ext_target(node, direct)) {
        // 直接分配一个 VIP 并返回 AF_INET addrinfo
        uint32_t fake = DnsMapper::instance().get_fake_ip(direct);
        *res = create_ipv4_addrinfo(fake, ...);
        return 0;
    }

    // Step 2: 如果 hints 限制为 AF_INET，放宽为 AF_UNSPEC
    //   （否则无法获取 IPv6 地址，也就无法做映射）
    if (hints && hints->ai_family == AF_INET) {
        hints_copy = *hints;
        hints_copy.ai_family = AF_UNSPEC;
    }

    // Step 3: 调用真实 getaddrinfo 获取所有地址族的解析结果
    real_getaddrinfo(node, service, &hints_copy, &orig);

    // Step 4: 对每个 IPv6 结果，分配一个 VIP
    //   对每个 IPv4 结果，直接保留
    //   返回的 addrinfo 链表全是 AF_INET
}
```

**为什么需要放宽 hints：** 如果应用程序明确请求 `AF_INET`，glibc 的 `getaddrinfo` 就不会返回 IPv6 地址。通过将 `ai_family` 改为 `AF_UNSPEC`，我们让 glibc 返回所有地址，然后在我们的 wrapper 中将 IPv6 映射到虚拟 IPv4。

#### 3.4 gethostbyname() —— 老式 DNS API 劫持

```cpp
extern "C" struct hostent *gethostbyname(const char *name) {
    // 使用 getaddrinfo 获取结果（复用上面的逻辑）
    // 将结果转换为 hostent 格式（静态 thread_local 缓冲区）
    // 返回指向静态缓冲区的指针（兼容 POSIX 规范）
}
```

**为什么用 `thread_local` 存储 `hostent`：** `gethostbyname` 的 POSIX 规范规定返回值可能指向静态存储，非线程安全。使用 `thread_local` 保证每个线程有自己的缓冲区，避免了多线程并发调用时的数据竞争。

#### 3.5 close() —— Socket 清理

```cpp
extern "C" int close(int fd) {
    DnsMapper::instance().untrack_socket(fd);  // 清理 socket_targets_
    return real_close(fd);                     // 调用真实 close
}
```

#### 3.6 read/recv/send/write —— 透明透传

这四个函数目前简单地透传到真实系统调用，不修改数据：

```cpp
extern "C" ssize_t read(int fd, void *buf, size_t count) {
    return real_read(fd, buf, count);
}
```

**为什么 hook 它们：** 虽然当前实现是透传，但 hook 的存在为未来扩展提供了可能性：
- 在 `read()` 中透明修改接收数据的地址信息
- 在 `write()` 中透明修改发送数据的目标地址
- 收集 I/O 性能指标

---

### 4. IP 选项注入 —— IPSWEN 扩展地址支持

```cpp
bool inject_dest_ext_option(int fd, uint32_t ext) {
    if (ext == 0) return true;  // ext=0 意味着基础 IPv4，不需要选项
    unsigned char opt[8] = {
        0xE9, 0x03,             // 选项 1: type=0xE9, len=3
        0x00,                    // 选项 1 payload（填充）
        0xF1, 0x03,             // 选项 2: type=0xF1, len=3
        static_cast<unsigned char>(ext & 0xff),  // 选项 2 payload（目标扩展字节）
        0x00, 0x00              // 对齐填充（EOL + NOP）
    };
    return setsockopt(fd, IPPROTO_IP, IP_OPTIONS, opt, sizeof(opt)) >= 0;
}
```

**IP 选项字节结构解析：**

| 偏移 | 值 | 含义 |
|------|-----|------|
| 0 | 0xE9 | IP Option Type: Copy=1, Class=1, Number=9 |
| 1 | 0x03 | Option Length = 3 字节（含自身） |
| 2 | 0x00 | Option Data（保留） |
| 3 | 0xF1 | IP Option Type: Copy=1, Class=2, Number=17 |
| 4 | 0x03 | Option Length = 3 字节 |
| 5 | ext & 0xFF | **目标扩展地址字节**（核心数据） |
| 6-7 | 0x00 | NOP + EOL（IP 选项列表终止和填充） |

选项类型 0xE9 的位分解：
- Bit 7 (0x80): Copy-on-Fragmentation = 1（所有 IP 分片都携带）
- Bit 6-5: Option Class = 2（Debug/Measurement）
- Bit 4-0: Option Number = 9

选项类型 0xF1 的位分解：
- Bit 7 (0x80): Copy-on-Fragmentation = 1
- Bit 6-5: Option Class = 2
- Bit 4-0: Option Number = 17

这些自定义选项类型（233 / 241 十进制）在 IANA 的 IP Option Numbers 注册表中属于未分配范围，不与任何标准或已注册的选项冲突。

---

### 5. addrinfo 内存管理 —— 链表构建

三个辅助函数管理 `addrinfo` 链表：

```cpp
// 创建一个新的 AF_INET addrinfo 节点
addrinfo *create_ipv4_addrinfo(uint32_t ip_host_order, int socktype, ...);

// 深拷贝一个 addrinfo 节点（连同 ai_addr 和 ai_canonname）
addrinfo *duplicate_addrinfo(const addrinfo *src);

// 将节点追加到链表尾部
void append_addrinfo(addrinfo *&head, addrinfo *&tail, addrinfo *node);
```

**内存布局：** 每个 `addrinfo` 节点的 `ai_addr` 指向一个 `sockaddr_in`（16 字节），其中 `sin_addr.s_addr` 存储虚拟 IPv4（网络字节序）。

---

### 6. 端口解析 —— resolve_service_port()

```cpp
bool resolve_service_port(const char *service, const addrinfo *hints, uint16_t &port_be) {
    // 方法 1: 直接解析为数字（"8080" → 8080）
    long v = strtol(service, &end, 10);
    if (v >= 0 && v <= 65535) return htons(v);

    // 方法 2: getservbyname() 查询 /etc/services
    const char *proto = (hints->ai_socktype == SOCK_STREAM) ? "tcp" : "udp";
    servent *se = getservbyname(service, proto);
    if (se) return se->s_port;

    // 方法 3: 如果 UDP 查不到，回退到 TCP 再试
    if (strcmp(proto, "tcp") != 0) {
        se = getservbyname(service, "tcp");
        if (se) return se->s_port;
    }
    return false;
}
```

---

## 测试程序详解

### test_server.cpp —— 纯 IPv6 TCP 服务器

```cpp
int main() {
    int server_fd = socket(AF_INET6, SOCK_STREAM, 0);       // ★ AF_INET6
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;                            // 监听所有 IPv6 地址
    addr.sin6_port = htons(8080);
    bind(server_fd, ...);
    listen(server_fd, 1);

    // accept → read → send response → close
}
```

**关键点：** 服务器**仅**绑定 IPv6（`AF_INET6`），没有 IPv4 监听。这是测试 `fakedns` 透明转换的最小可行目标。

### test_client.cpp —— getaddrinfo 客户端

```cpp
hints.ai_family = AF_INET;  // ★ 明确请求 IPv4 地址（但仍能访问 IPv6 服务！）
getaddrinfo("ip6-localhost", "8080", &hints, &res);
connect(sock, res->ai_addr, res->ai_addrlen);
```

### test_client_legacy.cpp —— gethostbyname 老式客户端

```cpp
struct hostent* he = gethostbyname("ip6-localhost");  // 老式 API
// he->h_addrtype 被确保为 AF_INET
connect(sock, ...);
```

---

## 性能基准测试

### 基准测试 1：系统调用级延迟（benchmark.cpp）

测量 8 个 hook 的每次调用延迟，对比原生系统调用。每个操作执行 500-1000 次采样，取 mean/p50/p90。

```
======================================================================================
                  FakeDNS Syscall Benchmark (Latency in ns)
======================================================================================
Hook Name              |   Orig Mean/P50  |  Hook Mean/P50  |   Overhead Mean/P50/P90
--------------------------------------------------------------------------------------
getaddrinfo (IPv4 std) |          X / Y   |         X / Y   |              X / Y / Z
getaddrinfo (fake map) |          X / Y   |         X / Y   |              X / Y / Z
gethostbyname (std)    |          X / Y   |         X / Y   |              X / Y / Z
connect (normal IP)    |          X / Y   |         X / Y   |              X / Y / Z
connect (fake map)     |          X / Y   |         X / Y   |              X / Y / Z
connect (v4 mapped)    |          X / Y   |         X / Y   |              X / Y / Z
write (data bypass)    |          X / Y   |         X / Y   |              X / Y / Z
read (data bypass)     |          X / Y   |         X / Y   |              X / Y / Z
close (untrack)        |          X / Y   |         X / Y   |              X / Y / Z
```

### 基准测试 2：映射表数据结构性能（benchmark_mapper.cpp）

在不需要 LD_PRELOAD 的独立环境中测试 DnsMapper 的性能：

```cpp
void measure_at_size(int N) {
    // 1. 预填充 N 个映射
    // 2. Hit Test: 查询已有映射 → map.find() + LRU list move 的延迟
    // 3. Rev Test: 反向查询 VIP → RealTarget 的延迟
    // 4. Miss Test: 查询不存在的目标 → 分配新 VIP 的延迟
}
```

输出 CSV 格式（size, hit, miss, rev），由 `plot_benchmark.py` 读取并生成 `DnsMapper_Performance.png` 和 `DnsMapper_Performance.pdf`。

---

## 编译完整指南

### 环境要求

```bash
# 系统依赖
sudo apt update && sudo apt install -y \
    build-essential g++ gcc make \
    python3 python3-pip

# Python 绘图依赖（仅 benchmark 可视化需要）
pip3 install pandas matplotlib

# 验证编译器版本
g++ --version   # 需要 GCC 7+ (支持 C++17)
```

### 第一步：编译所有目标

```bash
cd fakedns

# 方法 A：使用 Makefile 编译全部目标
make all

# 方法 B：分步编译
# 编译 fakedns.so（Debug 版，含调试日志）
g++ -shared -fPIC -O2 -o fakedns.so shim.cpp -ldl -lpthread -DENABLE_DEBUG

# 编译测试程序
g++ -o test_server test_server.cpp
g++ -o test_client test_client.cpp
g++ -o test_client_legacy test_client_legacy.cpp

# 编译基准测试程序
g++ -O3 -std=c++17 -o benchmark benchmark.cpp -ldl
g++ -O3 -std=c++17 -o benchmark_mapper benchmark_mapper.cpp
```

### 第二步：编译发布版（无调试输出）

```bash
# 关闭 ENABLE_DEBUG 宏（不要在编译命令中加 -DENABLE_DEBUG）
g++ -O3 -shared -fPIC -o fakedns_release.so shim.cpp -ldl -lpthread

# 或创建符号链接作为 libfakedns.so
ln -sf fakedns_release.so libfakedns.so
```

### 第三步：验证编译产物

```bash
ls -lh fakedns.so libfakedns.so fakedns_release.so
file fakedns.so       # ELF 64-bit LSB shared object

# 验证导出的符号（应看到 8 个系统调用函数）
nm -D fakedns.so | grep " T "
# 预期输出包含：close, connect, getaddrinfo, gethostbyname, read, recv, send, write

# 验证依赖库
ldd fakedns.so
# 应包含：libdl.so, libpthread.so, libstdc++.so, libc.so
```

### 常见编译错误

| 错误 | 原因 | 解决 |
|------|------|------|
| `fatal error: dlfcn.h: No such file` | libc6-dev 未安装 | `sudo apt install libc6-dev` |
| `undefined reference to 'pthread_mutex_lock'` | 缺少 `-lpthread` | 在编译命令中加上 `-lpthread` |
| `error: 'constexpr' needed for in-class initialization` | C++ 标准版本过低 | 使用 `-std=c++17` 或更高 |
| `dlsym: undefined symbol` | 函数名拼写错误 | 检查 `must_resolve` 中的字符串 |
| `fakedns.so: undefined symbol: _ZN9DnsMapper...` | C++ ABI 问题 | 确保使用相同的编译器编译和链接 |

---

## 运行与测试

### 端到端功能测试

```bash
cd fakedns

# 方法 A：一键运行测试脚本
bash run_test.sh

# 方法 B：手动分步测试
# Step 1: 启动 IPv6 服务器
./test_server &
SERVER_PID=$!
sleep 1

# Step 2: 用 LD_PRELOAD 运行 AF_INET 客户端（getaddrinfo 版）
LD_PRELOAD=./libfakedns.so ./test_client ip6-localhost

# 预期输出：
#   [FakeDNS] getaddrinfo: Intercepted request for ip6-localhost
#   [FakeDNS] get_fake_ip: Created new mapping 240.0.0.1 -> ::1
#   [Client] Resolved to IPv4: 240.0.0.1
#   [FakeDNS] connect: Redirecting to real IPv6 ::1
#   [Client] Received: Hello from IPv6 Server!

# Step 3: 测试老式 gethostbyname 客户端
./test_server &
LD_PRELOAD=./libfakedns.so ./test_client_legacy ip6-localhost

# Step 4: 测试不经过 fakedns 的行为（对比组）
./test_client localhost
# 预期失败或走不同路径（因为没有 IPv6 映射）

# Step 5: 清理
kill %1 %2 2>/dev/null
```

**预期观察：**
- 客户端打印 `[FakeDNS]` 开头的调试日志 → fakedns 正在工作
- 客户端收到的响应来自 IPv6 服务器 → 转换成功
- 不加载 fakedns 时，客户端无法连接纯 IPv6 服务器 → 验证转换的必要性

### 基准测试

```bash
# 系统调用延迟基准测试
bash run_benchmark.sh

# 手动运行：
g++ -O3 -shared -fPIC -o fakedns_release.so shim.cpp -ldl -lpthread
g++ -O3 -o benchmark benchmark.cpp -ldl
LD_PRELOAD=./fakedns_release.so ./benchmark

# 映射表数据结构基准测试
g++ -O3 -std=c++17 -o benchmark_mapper benchmark_mapper.cpp
./benchmark_mapper

# 生成性能图表
python3 plot_benchmark.py
ls DnsMapper_Performance.png DnsMapper_Performance.pdf
```

---

## 实验定制指南

### 实验 1：修改 VIP 地址池大小

```cpp
// 在 shim.cpp 顶部修改常量
constexpr uint32_t VIP_MASK_LIMIT = 0x00020000u;  // 从 65536 增加到 131072

// DnsMapper 构造函数中需要同步修改循环范围
for (uint32_t ip = 0xF0020000u; ip != 0xF0000000u; --ip) {
    free_ips_.push_back(ip);
}

// 重新编译
g++ -O3 -shared -fPIC -o libfakedns.so shim.cpp -ldl -lpthread
```

### 实验 2：使用不同的虚拟 IP 地址段

```cpp
// 使用 203.0.113.0/24（RFC 5737 TEST-NET-3 文档地址，不会在公网路由）
constexpr uint32_t VIP_FIRST_HOST = 0xCB007101u;  // 203.0.113.1
constexpr uint32_t VIP_MASK_BASE = 0xCB007100u;   // 203.0.113.0
constexpr uint32_t VIP_MASK_LIMIT = 0x00000100u;  // 256 个

// 同步修改 DnsMapper 构造函数中的初始化范围
```

### 实验 3：修改映射过期时间

```cpp
// 从 5 分钟改为 30 分钟
constexpr long MAPPING_TTL = 1800;

// 或改为永不过期（仅在 VIP 池满时回收）
constexpr long MAPPING_TTL = 86400 * 365;  // 约 1 年
```

### 实验 4：添加对 UDP socket 转换的支持

```cpp
// 当前实现只支持 SOCK_STREAM 到 AF_INET6 的 dup2 转换
// 对于 SOCK_DGRAM，需要不同的策略：

// 方案 A：在 sendto/recvfrom 中做地址转换（推荐）
extern "C" ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
                           const struct sockaddr *dest_addr, socklen_t addrlen) {
    // 检查 dest_addr 中的 VIP，替换为真实 IPv6 sockaddr_in6
    // ...
}
extern "C" ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                             struct sockaddr *src_addr, socklen_t *addrlen) {
    // 调用真实 recvfrom，然后修改 src_addr 为虚拟 IPv4
    // ...
}
```

### 实验 5：添加连接统计和监控

```cpp
// 在 DnsMapper 中添加统计字段
struct Stats {
    uint64_t getaddrinfo_calls = 0;
    uint64_t connect_calls = 0;
    uint64_t connect_ipv4 = 0;
    uint64_t connect_ipv6 = 0;
    uint64_t connect_ipv4_ext = 0;
    uint64_t mapping_hits = 0;
    uint64_t mapping_misses = 0;
    uint64_t expired_entries = 0;
};

// 在 main 不退出时，通过 /tmp/fakedns_stats 输出
// 或通过 HTTP 端点暴露 Prometheus 指标
```

### 实验 6：测试多线程并发安全

```bash
# 编写多线程测试程序，N 个线程同时调用 getaddrinfo + connect
cat > mt_test.cpp << 'EOF'
#include <thread>
#include <vector>
// ... N 个线程并发执行 getaddrinfo + connect
EOF

g++ -std=c++17 -pthread -o mt_test mt_test.cpp
LD_PRELOAD=./libfakedns.so ./mt_test 100  # 100 个并发线程
```

---

## 注意事项

- **线程安全：** `DnsMapper` 使用 `pthread_mutex_t` 保护所有共享数据结构，在多线程应用中安全
- **LD_PRELOAD 作用域：** `LD_PRELOAD` 只影响当前进程及其子进程，不影响系统其他进程
- **SUID 限制：** Linux 对 SUID 程序禁用 `LD_PRELOAD`（安全限制），`fakedns` 不能用于 SUID 程序
- **调试输出：** Debug 版（`-DENABLE_DEBUG`）每次系统调用都写入 stderr，高吞吐场景下会严重影响性能
- **VIP 池大小：** 65536 个 VIP 通常足够，但如果应用需要同时维持大量连接且 MAPPING_TTL 较长，可能耗尽。增加 VIP_MASK_LIMIT 即可扩容
- **dup2 副作用：** `dup2(tmp, sockfd)` 后，原 socket 的某些属性（如 `SO_RCVTIMEO`）可能丢失
- **IPv4-mapped IPv6 地址：** 解析 `::ffff:A.B.C.D` 格式的地址时，只提取内嵌的 IPv4 部分，忽略 IPv6 前缀
- **魔法字节检查：** IPv6 输入地址前 8 字节全为 0x77 时通过检查，用于识别是否来自 fakedns 内部重定向
- **errno 保持：** 所有系统调用在返回前正确保存和恢复 `errno`，保证错误码对调用方透明

---

## 相关项目

- **optmap** —— 基于 eBPF/XDP 的 IP 选项路由和快速转发，fakedns 注入的 IP_OPTIONS 由 optmap 在内核层处理
- **happyfootball** —— Happy Eyeballs 多路径连接 + IP 选项实验，fakedns 可为其提供地址转换
- **autoconfig** —— GRE 隧道自动配置，fakedns 可透明代理隧道内的应用
- **mrtdb** —— BGP 路由数据平台，fakedns 的 IPv4→IPv6 转换可用于 BGP 测试床

---

## 附录 A：快速操作卡片

### 编译与运行
```bash
# 编译
make all

# 运行测试
bash run_test.sh

# 手动测试
./test_server &
LD_PRELOAD=./libfakedns.so ./test_client ip6-localhost
```

### 基准测试
```bash
bash run_benchmark.sh
./benchmark_mapper
python3 plot_benchmark.py
```

### 调试模式
```bash
# 启用调试输出（stderr）
LD_PRELOAD=./fakedns.so ./test_client ip6-localhost 2>&1 | grep '\[FakeDNS\]'

# 统计 hook 调用次数
LD_PRELOAD=./fakedns.so ./test_client ip6-localhost 2>&1 | grep -c '\[FakeDNS\] connect'
```

### 文件位置速查
| 内容 | 路径 |
|------|------|
| 核心源码 | `fakedns/shim.cpp` |
| 劫持宏模板 | `fakedns/patch.h` |
| 编译脚本 | `fakedns/Makefile` |
| Debug 共享库 | `fakedns/fakedns.so` |
| Release 共享库 | `fakedns/libfakedns.so` |
| 测试服务器 | `fakedns/test_server` |
| 测试客户端 | `fakedns/test_client` |
| 旧式客户端 | `fakedns/test_client_legacy` |
| 系统调用基准 | `fakedns/benchmark` |
| 映射表基准 | `fakedns/benchmark_mapper` |

## 附录 B：LD_PRELOAD 工作原理

```
程序启动
  │
  ▼
动态链接器 (ld.so) 加载共享库
  │
  ├── 读取 LD_PRELOAD 环境变量
  │   └── 在搜索路径最前面插入 libfakedns.so
  │       └── libfakedns.so 中的符号覆盖 libc.so 的同名符号
  │
  ├── 解析符号引用
  │   └── 当程序调用 connect() 时：
  │       libc.so 中的 connect → 被 libfakedns.so 中的 connect 覆盖
  │
  └── 程序运行
      └── 每个 connect() 调用都先经过 fakedns 的 wrapper
```

## 附录 C：虚拟 IPv4 地址段选择参考

| 地址段 | 用途 | 是否推荐 |
|--------|------|---------|
| 240.0.0.0/4 | Class E（实验/保留） | ★ 推荐（默认） |
| 203.0.113.0/24 | TEST-NET-3（文档） | ✓ 可用 |
| 198.51.100.0/24 | TEST-NET-2（文档） | ✓ 可用 |
| 100.64.0.0/10 | CGNAT（运营商级 NAT） | ⚠ 可能冲突 |
| 127.0.0.0/8 | Loopback | ✗ 不推荐（localhost 冲突） |
| 10.0.0.0/8 | 私有地址 | ⚠ 可能与内网冲突 |

## 附录 D：性能数据参考（基于 benchmark.cpp 实测）

以下为参考量级数据（实际值取决于硬件和内核版本）：

| 操作 | 原生延迟 | Hook 延迟 | 额外开销 | 备注 |
|------|---------|-----------|---------|------|
| getaddrinfo (std) | ~5-20 μs | ~6-22 μs | ~1-2 μs | 映射命中时 |
| getaddrinfo (fake map) | N/A | ~6-22 μs | N/A | 首次映射创建 |
| connect (normal) | ~3-10 μs | ~4-11 μs | ~1 μs | VIP 范围外，直接透传 |
| connect (fake map) | N/A | ~8-20 μs | N/A | 含 dup2 开销 |
| read/write | ~0.5-1 μs | ~0.5-1 μs | ~0 ns | 透明透传 |
| close | ~0.3-0.5 μs | ~0.4-0.7 μs | ~0.1 μs | 含 untrack 开销 |
