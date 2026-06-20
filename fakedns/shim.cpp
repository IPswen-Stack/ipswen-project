#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint32_t VIP_FIRST_HOST = 0xF0000001u;     // 240.0.0.1 host order
constexpr uint32_t VIP_LAST_EXCLUSIVE = 0xF0010001u; // binary constructor fills down to 240.0.0.1
constexpr uint32_t VIP_MASK_BASE = 0xF0000000u;      // 240.0.0.0/8 check as used by connect()
constexpr uint32_t VIP_MASK_LIMIT = 0x00010000u;     // implementation only populates 65536 VIPs
constexpr long     MAPPING_TTL = 300;

// Function pointers resolved through dlsym(RTLD_NEXT, ...).
using close_fn_t       = int (*)(int);
using connect_fn_t     = int (*)(int, const struct sockaddr *, socklen_t);
using read_fn_t        = ssize_t (*)(int, void *, size_t);
using recv_fn_t        = ssize_t (*)(int, void *, size_t, int);
using send_fn_t        = ssize_t (*)(int, const void *, size_t, int);
using write_fn_t       = ssize_t (*)(int, const void *, size_t);
using getaddrinfo_fn_t = int (*)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
using gethostbyname_fn_t = struct hostent *(*)(const char *);

static close_fn_t       real_close       = nullptr;
static connect_fn_t     real_connect     = nullptr;
static read_fn_t        real_read        = nullptr;
static recv_fn_t        real_recv        = nullptr;
static send_fn_t        real_send        = nullptr;
static write_fn_t       real_write       = nullptr;
static getaddrinfo_fn_t real_getaddrinfo = nullptr;
static gethostbyname_fn_t real_gethostbyname = nullptr;

template <typename T>
T must_resolve(const char *name) {
    void *p = dlsym(RTLD_NEXT, name);
    if (!p) {
        fprintf(stderr, "Error: could not find original function %s", name);
        exit(1);
    }
    return reinterpret_cast<T>(p);
}

static inline uint16_t host_to_net_port(uint16_t p) {
    return htons(p);
}


struct RealTarget {
    uint8_t  kind = 0;
    uint8_t  pad1[3]{};
    union { struct { uint32_t base_ipv4_host_order; uint32_t ext; uint32_t pad2; uint32_t pad3; } v4; uint8_t ipv6[16]; } u{};
    std::string to_string() const {
        char buf[INET6_ADDRSTRLEN]{};
        if (kind == 0) { inet_ntop(AF_INET6, u.ipv6, buf, sizeof(buf)); return std::string(buf); }
        uint32_t be = htonl(u.v4.base_ipv4_host_order);
        inet_ntop(AF_INET, &be, buf, sizeof(buf));
        return std::string(buf) + "(2)" + std::to_string(u.v4.ext);
    }
};

static bool operator<(const RealTarget &a, const RealTarget &b) {
    if (a.kind != b.kind) return a.kind < b.kind;
    if (a.kind == 0) return memcmp(a.u.ipv6, b.u.ipv6, 16) < 0;
    if (a.u.v4.base_ipv4_host_order != b.u.v4.base_ipv4_host_order) return a.u.v4.base_ipv4_host_order < b.u.v4.base_ipv4_host_order;
    return a.u.v4.ext < b.u.v4.ext;
}

struct MappingEntry {
    RealTarget target;
    long last_used = 0;
    std::list<uint32_t>::iterator lru_it;
};

class DnsMapper {
public:
    static DnsMapper &instance() {
        static DnsMapper inst;
        return inst;
    }

    uint32_t get_fake_ip(const RealTarget &target) {
        pthread_mutex_lock(&lock_);
        const long now = time(nullptr);

        auto back = reverse_.find(target);
        if (back != reverse_.end()) {
            uint32_t vip = back->second;
            auto it = mappings_.find(vip);
            if (it != mappings_.end()) {
                touch_locked(vip, it->second, now);
                fprintf(stderr, "[FakeDNS] get_fake_ip: Found existing mapping %s -> %s\n",
                        ipv4_to_text(vip).c_str(), target.to_string().c_str());
                pthread_mutex_unlock(&lock_);
                return vip;
            }
        }

        if (free_ips_.empty()) {
            recycle_expired_locked(now);
        }
        if (free_ips_.empty()) {
            fprintf(stderr, "[FakeDNS] get_fake_ip: No free IPs and no expired IPs available\n");
            pthread_mutex_unlock(&lock_);
            return 0;
        }

        uint32_t vip = free_ips_.back();
        free_ips_.pop_back();
        create_mapping_locked(vip, target, now);
        fprintf(stderr, "[FakeDNS] get_fake_ip: Created new mapping %s -> %s\n",
                ipv4_to_text(vip).c_str(), target.to_string().c_str());
        pthread_mutex_unlock(&lock_);
        return vip;
    }

    bool get_real_target(uint32_t fake_ip_host_order, RealTarget &out) {
        pthread_mutex_lock(&lock_);
        auto it = mappings_.find(fake_ip_host_order);
        if (it == mappings_.end()) {
            fprintf(stderr, "[FakeDNS] get_real_target: Failed to resolve %s\n",
                    ipv4_to_text(fake_ip_host_order).c_str());
            pthread_mutex_unlock(&lock_);
            return false;
        }
        out = it->second.target;
        touch_locked(fake_ip_host_order, it->second, time(nullptr));
        fprintf(stderr, "[FakeDNS] get_real_target: Resolved %s -> %s\n",
                ipv4_to_text(fake_ip_host_order).c_str(), out.to_string().c_str());
        pthread_mutex_unlock(&lock_);
        return true;
    }

    void track_socket(int fd, const RealTarget &target) {
        pthread_mutex_lock(&lock_);
        socket_targets_[fd] = target;
        pthread_mutex_unlock(&lock_);
    }

    bool socket_target(int fd, RealTarget &target) {
        pthread_mutex_lock(&lock_);
        auto it = socket_targets_.find(fd);
        if (it == socket_targets_.end()) {
            pthread_mutex_unlock(&lock_);
            return false;
        }
        target = it->second;
        pthread_mutex_unlock(&lock_);
        return true;
    }

    void untrack_socket(int fd) {
        pthread_mutex_lock(&lock_);
        socket_targets_.erase(fd);
        pthread_mutex_unlock(&lock_);
    }

private:
    DnsMapper() {
        pthread_mutex_init(&lock_, nullptr);
        free_ips_.reserve(65536);
        for (uint32_t ip = 0xF0010000u; ip != 0xF0000000u; --ip) {
            free_ips_.push_back(ip);
        }
    }

    ~DnsMapper() {
        pthread_mutex_destroy(&lock_);
    }

    static std::string ipv4_to_text(uint32_t host_order) {
        char buf[INET_ADDRSTRLEN]{};
        uint32_t be = htonl(host_order);
        inet_ntop(AF_INET, &be, buf, sizeof(buf));
        return std::string(buf);
    }

    void touch_locked(uint32_t vip, MappingEntry &entry, long now) {
        entry.last_used = now;
        lru_.erase(entry.lru_it);
        lru_.push_front(vip);
        entry.lru_it = lru_.begin();
    }

    void recycle_expired_locked(long now) {
        while (!lru_.empty()) {
            uint32_t vip = lru_.back();
            auto it = mappings_.find(vip);
            if (it == mappings_.end()) {
                lru_.pop_back();
                continue;
            }
            if (now - it->second.last_used < MAPPING_TTL) break;
            fprintf(stderr, "[FakeDNS] recycle_expired: Evicting %s -> %s\n",
                    ipv4_to_text(vip).c_str(), it->second.target.to_string().c_str());
            reverse_.erase(it->second.target);
            lru_.pop_back();
            mappings_.erase(it);
            free_ips_.push_back(vip);
        }
    }

    void create_mapping_locked(uint32_t vip, const RealTarget &target, long now) {
        lru_.push_front(vip);
        MappingEntry e;
        e.target = target;
        e.last_used = now;
        e.lru_it = lru_.begin();
        mappings_[vip] = e;
        reverse_[target] = vip;
    }

    pthread_mutex_t lock_{};
    std::vector<uint32_t> free_ips_;
    std::list<uint32_t> lru_;
    std::map<uint32_t, MappingEntry> mappings_;
    std::map<RealTarget, uint32_t> reverse_;
    std::map<int, RealTarget> socket_targets_;
};

bool parse_decimal_token(const std::string &s, uint32_t &out) {
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
        if (v > 0xffffffffULL) return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

// Parses extension text like "5.6" or "99" into a big-endian-ish integer.
// Example: "0.99" -> 0x0063; "5.6" -> 0x0506.
bool parse_ext_components(const std::string &s, uint32_t &out) {
    if (s.empty()) return false;
    out = 0;
    size_t start = 0;
    while (start <= s.size()) {
        size_t dot = s.find('.', start);
        std::string part = s.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        uint32_t byte = 0;
        if (!parse_decimal_token(part, byte) || byte > 255) return false;
        out = (out << 8) | byte;
        if (dot == std::string::npos) return true;
        start = dot + 1;
    }
    return false;
}

// Accepts the pseudo extended IPv4 notations:
//   1.2.3.4(2)0.99
//   1.2.3.4.ext99
bool parse_ipv4_with_ext_target(const char *name, RealTarget &out) {
    if (!name) return false;
    std::string s(name);
    if (s.empty()) return false;

    uint32_t level = 0;
    std::string base;
    std::string ext_s;

    size_t p = s.find('(');
    size_t ext_marker = s.rfind(".ext");
    if (p != std::string::npos) {
        size_t q = s.find(')', p + 1);
        if (q == std::string::npos || p == 0) return false;
        if (!parse_decimal_token(s.substr(p + 1, q - p - 1), level)) return false;
        base = s.substr(0, p);
        ext_s = s.substr(q + 1);
    } else if (ext_marker != std::string::npos && ext_marker > 0) {
        base = s.substr(0, ext_marker);
        ext_s = s.substr(ext_marker + 4);
        level = 1;
    } else {
        return false;
    }

    uint32_t base_be = 0;
    if (inet_pton(AF_INET, base.c_str(), &base_be) != 1) return false;
    uint32_t ext = 0;
    if (!parse_ext_components(ext_s, ext)) return false;

    memset(&out, 0, sizeof(out));
    out.kind = 1;
    out.u.v4.base_ipv4_host_order = ntohl(base_be);
    out.u.v4.ext = ext;
    return true;
}

struct addrinfo *create_ipv4_addrinfo(uint32_t ip_host_order, int socktype, int protocol,
                                      const char *canonname, uint16_t port_be) {
    addrinfo *ai = static_cast<addrinfo *>(calloc(1, sizeof(addrinfo)));
    if (!ai) return nullptr;
    sockaddr_in *sa = static_cast<sockaddr_in *>(calloc(1, sizeof(sockaddr_in)));
    if (!sa) { free(ai); return nullptr; }

    ai->ai_family = AF_INET;
    ai->ai_socktype = socktype;
    ai->ai_protocol = protocol;
    ai->ai_addrlen = sizeof(sockaddr_in);
    ai->ai_addr = reinterpret_cast<sockaddr *>(sa);
    if (canonname) ai->ai_canonname = strdup(canonname);

    sa->sin_family = AF_INET;
    sa->sin_port = port_be;
    sa->sin_addr.s_addr = htonl(ip_host_order);
    return ai;
}

addrinfo *duplicate_addrinfo(const addrinfo *src) {
    if (!src) return nullptr;
    addrinfo *ai = static_cast<addrinfo *>(calloc(1, sizeof(addrinfo)));
    if (!ai) return nullptr;
    memcpy(ai, src, sizeof(addrinfo));
    ai->ai_next = nullptr;
    if (src->ai_addr && src->ai_addrlen) {
        ai->ai_addr = static_cast<sockaddr *>(malloc(src->ai_addrlen));
        if (!ai->ai_addr) { free(ai); return nullptr; }
        memcpy(ai->ai_addr, src->ai_addr, src->ai_addrlen);
    }
    if (src->ai_canonname) ai->ai_canonname = strdup(src->ai_canonname);
    return ai;
}

void append_addrinfo(addrinfo *&head, addrinfo *&tail, addrinfo *node) {
    if (!node) return;
    if (!head) head = tail = node;
    else { tail->ai_next = node; tail = node; }
}

bool resolve_service_port(const char *service, const addrinfo *hints, uint16_t &port_be) {
    port_be = 0;
    if (!service || !*service) return true;

    char *end = nullptr;
    long v = strtol(service, &end, 10);
    if (end && *end == '\0' && v >= 0 && v <= 65535) {
        port_be = htons(static_cast<uint16_t>(v));
        return true;
    }

    const char *proto = nullptr;
    if (hints) {
        if (hints->ai_socktype == SOCK_STREAM) proto = "tcp";
        else if (hints->ai_socktype == SOCK_DGRAM) proto = "udp";
    }
    if (!proto) proto = "tcp";
    if (servent *se = getservbyname(service, proto)) {
        port_be = static_cast<uint16_t>(se->s_port);
        return true;
    }
    if (strcmp(proto, "tcp") != 0) {
        if (servent *se = getservbyname(service, "tcp")) {
            port_be = static_cast<uint16_t>(se->s_port);
            return true;
        }
    }
    return false;
}

// Reconstructed literally from stores in inject_dest_ext_option():
//   bytes: e9 03 00 f1 03 <low(ext)> 00 00
// Then setsockopt(fd, IPPROTO_IP, IP_OPTIONS, opt, 8).
bool inject_dest_ext_option(int fd, uint32_t ext) {
    if (ext == 0) return true;
    unsigned char opt[8] = {0xE9, 0x03, 0x00, 0xF1, 0x03, static_cast<unsigned char>(ext & 0xff), 0x00, 0x00};
    if (setsockopt(fd, IPPROTO_IP, IP_OPTIONS, opt, sizeof(opt)) >= 0) return true;
    fprintf(stderr, "[FakeDNS] connect: setsockopt(IP_OPTIONS) failed for fd %d, ext=%u, errno=%d\n",
            fd, ext, errno);
    return false;
}

static bool is_managed_vip(uint32_t host_order) {
    return host_order >= VIP_MASK_BASE && host_order < VIP_MASK_BASE + VIP_MASK_LIMIT;
}

} // namespace

extern "C" int close(int fd) {
    if (!real_close) real_close = must_resolve<close_fn_t>("close");
    DnsMapper::instance().untrack_socket(fd);
    return real_close(fd);
}

extern "C" int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (!real_connect) real_connect = must_resolve<connect_fn_t>("connect");

    fprintf(stderr, "[FakeDNS] connect: sockfd=%d, addrlen=%d\n", sockfd, (int)addrlen);

    if (!addr) return real_connect(sockfd, addr, addrlen);

    bool input_is_v4 = false;
    bool input_is_v6 = false;
    uint16_t port_be = 0;
    uint32_t dst_v4_host = 0;

    if (addr->sa_family == AF_INET) {
        const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(addr);
        char text[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text));
        fprintf(stderr, "[FakeDNS] connect: AF_INET address %s:%d\n", text, ntohs(sin->sin_port));
        input_is_v4 = true;
        port_be = sin->sin_port;
        dst_v4_host = ntohl(sin->sin_addr.s_addr);
    } else if (addr->sa_family == AF_INET6) {
        const sockaddr_in6 *sin6 = reinterpret_cast<const sockaddr_in6 *>(addr);
        char text[INET6_ADDRSTRLEN]{};
        inet_ntop(AF_INET6, &sin6->sin6_addr, text, sizeof(text));
        fprintf(stderr, "[FakeDNS] connect: AF_INET6 address %s:%d\n", text, ntohs(sin6->sin6_port));
        input_is_v6 = true;
        port_be = sin6->sin6_port;
        const uint8_t *b = sin6->sin6_addr.s6_addr;
        bool mapped = memcmp(b, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12) == 0;
        if (!mapped) return real_connect(sockfd, addr, addrlen);
        memcpy(&dst_v4_host, b + 12, 4);
        dst_v4_host = ntohl(dst_v4_host);
    } else {
        fprintf(stderr, "[FakeDNS] connect: Unknown address family %d\n", addr->sa_family);
        return real_connect(sockfd, addr, addrlen);
    }

    if (!is_managed_vip(dst_v4_host)) return real_connect(sockfd, addr, addrlen);

    if (input_is_v6) {
        const uint8_t *b = reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr.s6_addr;
        bool magic_ok = std::all_of(b, b + 8, [](uint8_t c) { return c == 0x77; });
        fprintf(stderr, "[FakeDNS] connect: Magic number check: %s\n", magic_ok ? "PASSED" : "FAILED");
    }

    RealTarget target;
    if (!DnsMapper::instance().get_real_target(dst_v4_host, target)) {
        return real_connect(sockfd, addr, addrlen);
    }

    if (target.kind == 1) {
        char text[INET_ADDRSTRLEN]{};
        uint32_t base_be = htonl(target.u.v4.base_ipv4_host_order);
        inet_ntop(AF_INET, &base_be, text, sizeof(text));
        fprintf(stderr, "[FakeDNS] connect: Redirecting to real IPv4 %s with DEST_EXT=%u\n", text, target.u.v4.ext);

        sockaddr_in sin{};
        sin.sin_family = AF_INET;
        sin.sin_port = port_be;
        sin.sin_addr.s_addr = base_be;

        if (input_is_v4) {
            if (!inject_dest_ext_option(sockfd, target.u.v4.ext)) return -1;
            int rc = real_connect(sockfd, reinterpret_cast<sockaddr *>(&sin), sizeof(sin));
            if (rc == 0 || (rc < 0 && errno == EINPROGRESS)) DnsMapper::instance().track_socket(sockfd, target);
            return rc;
        }

        // IPv4-mapped IPv6
        int type = 0; socklen_t optlen = sizeof(type);
        if (getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &type, &optlen) < 0) return -1;
        int tmp = socket(AF_INET, type, 0);
        if (tmp < 0) return -1;
        int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags & O_NONBLOCK) fcntl(tmp, F_SETFL, flags);
        if (!inject_dest_ext_option(tmp, target.u.v4.ext)) { int e = errno; close(tmp); errno = e; return -1; }
        int rc = real_connect(tmp, reinterpret_cast<sockaddr *>(&sin), sizeof(sin));
        int e = errno;
        if (rc == 0 || (rc < 0 && e == EINPROGRESS)) {
            if (dup2(tmp, sockfd) < 0) { e = errno; close(tmp); errno = e; return -1; }
            DnsMapper::instance().track_socket(sockfd, target);
            if (!(flags & O_NONBLOCK)) fcntl(sockfd, F_SETFL, flags);
        }
        close(tmp);
        errno = e;
        return rc;
    }

    // IPv6 real target path.
    char text[INET6_ADDRSTRLEN]{};
    inet_ntop(AF_INET6, target.u.ipv6, text, sizeof(text));
    fprintf(stderr, "[FakeDNS] connect: Redirecting to real IPv6 %s\n", text);

    sockaddr_in6 sin6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_port = port_be;
    memcpy(&sin6.sin6_addr, target.u.ipv6, 16);

    if (input_is_v6) {
        int rc = real_connect(sockfd, reinterpret_cast<sockaddr *>(&sin6), sizeof(sin6));
        if (rc == 0 || (rc < 0 && errno == EINPROGRESS)) DnsMapper::instance().track_socket(sockfd, target);
        return rc;
    }

    int type = 0; socklen_t optlen = sizeof(type);
    if (getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &type, &optlen) < 0) return -1;
    int tmp = socket(AF_INET6, type, 0);
    if (tmp < 0) return -1;
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags & O_NONBLOCK) fcntl(tmp, F_SETFL, flags);
    int rc = real_connect(tmp, reinterpret_cast<sockaddr *>(&sin6), sizeof(sin6));
    int e = errno;
    if (rc == 0 || (rc < 0 && e == EINPROGRESS)) {
        if (dup2(tmp, sockfd) < 0) { e = errno; close(tmp); errno = e; return -1; }
        DnsMapper::instance().track_socket(sockfd, target);
        if (!(flags & O_NONBLOCK)) fcntl(sockfd, F_SETFL, flags);
    }
    close(tmp);
    errno = e;
    return rc;
}

extern "C" ssize_t read(int fd, void *buf, size_t count) {
    if (!real_read) real_read = must_resolve<read_fn_t>("read");
    return real_read(fd, buf, count);
}
extern "C" ssize_t recv(int fd, void *buf, size_t len, int flags) {
    if (!real_recv) real_recv = must_resolve<recv_fn_t>("recv");
    return real_recv(fd, buf, len, flags);
}
extern "C" ssize_t send(int fd, const void *buf, size_t len, int flags) {
    if (!real_send) real_send = must_resolve<send_fn_t>("send");
    return real_send(fd, buf, len, flags);
}
extern "C" ssize_t write(int fd, const void *buf, size_t count) {
    if (!real_write) real_write = must_resolve<write_fn_t>("write");
    return real_write(fd, buf, count);
}

extern "C" int getaddrinfo(const char *node, const char *service,
                            const struct addrinfo *hints, struct addrinfo **res) {
    if (!real_getaddrinfo) real_getaddrinfo = must_resolve<getaddrinfo_fn_t>("getaddrinfo");

    fprintf(stderr, "[FakeDNS] getaddrinfo: Intercepted request for %s\n", node ? node : "null");
    if (!res) return EAI_FAIL;
    *res = nullptr;

    RealTarget direct;
    if (parse_ipv4_with_ext_target(node, direct)) {
        if (hints && hints->ai_family == AF_INET6) return EAI_FAMILY;
        uint16_t port_be = 0;
        if (!resolve_service_port(service, hints, port_be)) return EAI_NONAME;
        uint32_t fake = DnsMapper::instance().get_fake_ip(direct);
        if (!fake) return EAI_NONAME;
        std::string t = direct.to_string();
        fprintf(stderr, "[FakeDNS] getaddrinfo: Parsed pseudo IPv4+ext %s -> fake IPv4 %u\n", t.c_str(), fake);
        int socktype = hints ? hints->ai_socktype : 0;
        int proto = hints ? hints->ai_protocol : 0;
        const char *canon = (hints && (hints->ai_flags & AI_CANONNAME)) ? t.c_str() : nullptr;
        *res = create_ipv4_addrinfo(fake, socktype, proto, canon, port_be);
        return *res ? 0 : EAI_MEMORY;
    }

    addrinfo hints_copy;
    const addrinfo *actual_hints = hints;
    if (hints && hints->ai_family == AF_INET) {
        hints_copy = *hints;
        hints_copy.ai_family = AF_UNSPEC;
        actual_hints = &hints_copy;
    }

    addrinfo *orig = nullptr;
    int rc = real_getaddrinfo(node, service, actual_hints, &orig);
    if (rc != 0) return rc;

    bool has_ipv6 = false;
    for (addrinfo *p = orig; p; p = p->ai_next) {
        if (p->ai_family == AF_INET6) { has_ipv6 = true; break; }
    }
    if (!has_ipv6) {
        *res = orig;
        return 0;
    }

    addrinfo *head = nullptr, *tail = nullptr;
    for (addrinfo *p = orig; p; p = p->ai_next) {
        if (p->ai_family == AF_INET6 && p->ai_addr) {
            sockaddr_in6 *sa6 = reinterpret_cast<sockaddr_in6 *>(p->ai_addr);
            RealTarget rt{};
            rt.kind = 0;
            memcpy(rt.u.ipv6, &sa6->sin6_addr, 16);
            uint32_t fake = DnsMapper::instance().get_fake_ip(rt);
            if (fake) {
                append_addrinfo(head, tail, create_ipv4_addrinfo(fake, p->ai_socktype, p->ai_protocol,
                                                                p->ai_canonname, sa6->sin6_port));
            }
        } else if (p->ai_family == AF_INET) {
            sockaddr_in *sa = reinterpret_cast<sockaddr_in *>(p->ai_addr);
            fprintf(stderr, "[FakeDNS] getaddrinfo: Keeping existing IPv4 address %s\n", inet_ntoa(sa->sin_addr));
            append_addrinfo(head, tail, duplicate_addrinfo(p));
        }
    }

    freeaddrinfo(orig);
    if (!head) return EAI_NONAME;
    *res = head;
    return 0;
}

extern "C" struct hostent *gethostbyname(const char *name) {
    if (!real_gethostbyname) real_gethostbyname = must_resolve<gethostbyname_fn_t>("gethostbyname");

    fprintf(stderr, "[FakeDNS] gethostbyname: Intercepted request for %s\n", name ? name : "null");

    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo *res = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &res) != 0 || !res) {
        return real_gethostbyname(name);
    }

    addrinfo *p = res;
    while (p && p->ai_family != AF_INET) p = p->ai_next;
    if (!p || !p->ai_addr) {
        freeaddrinfo(res);
        return real_gethostbyname(name);
    }

    static thread_local char h_name_buf[256];
    static thread_local uint32_t h_addr_buf;
    static thread_local char *h_aliases[1];
    static thread_local char *h_addr_list[2];
    static thread_local struct hostent h;

    const char *canon = res->ai_canonname ? res->ai_canonname : name;
    strncpy(h_name_buf, canon ? canon : "", sizeof(h_name_buf) - 1);
    h_name_buf[sizeof(h_name_buf) - 1] = '\0';

    sockaddr_in *sa = reinterpret_cast<sockaddr_in *>(p->ai_addr);
    h_addr_buf = sa->sin_addr.s_addr;
    h_aliases[0] = nullptr;
    h_addr_list[0] = reinterpret_cast<char *>(&h_addr_buf);
    h_addr_list[1] = nullptr;

    h.h_name = h_name_buf;
    h.h_aliases = h_aliases;
    h.h_addrtype = AF_INET;
    h.h_length = 4;
    h.h_addr_list = h_addr_list;

    freeaddrinfo(res);
    return &h;
}
