#include <iostream>
#include <chrono>
#include <map>
#include <mutex>
#include <list>
#include <vector>
#include <cstring>
#include <iomanip>
#include <arpa/inet.h>

using namespace std;
using namespace std::chrono;

static const uint32_t FAKE_IP_START = 0xF0000001; 
static const uint32_t MAX_MAPPINGS  = 65536;
static const time_t MAPPING_TIMEOUT = 3600; 

struct IPv6Address {
    struct in6_addr addr;
    bool operator<(const IPv6Address& other) const {
        return memcmp(&addr, &other.addr, sizeof(addr)) < 0;
    }
};

struct MappingEntry {
    IPv6Address v6_addr;
    time_t last_access;
    std::list<uint32_t>::iterator lru_it;
};

// 提取并解除了单例模式的纯粹的数据结构测试类
class DnsMapperMock {
public:
    DnsMapperMock() {
        free_ips_.reserve(MAX_MAPPINGS);
        for (int32_t i = MAX_MAPPINGS - 1; i >= 0; --i) {
            free_ips_.push_back(FAKE_IP_START + i);
        }
    }

    uint32_t get_fake_ip(const struct in6_addr& v6_addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        IPv6Address key{v6_addr};
        time_t now = time(nullptr);
        
        auto it = v6_to_v4_.find(key);
        if (it != v6_to_v4_.end()) {
            update_lru(it->second, now);
            return it->second;
        }

        recycle_expired(now);

        if (!free_ips_.empty()) {
            uint32_t fake_ip = free_ips_.back();
            free_ips_.pop_back();
            create_mapping(fake_ip, key, now);
            return fake_ip;
        }
        return 0; 
    }

    bool get_real_ip(uint32_t fake_ip, struct in6_addr& v6_addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mappings_.find(fake_ip);
        if (it != mappings_.end()) {
            v6_addr = it->second.v6_addr.addr;
            update_lru(fake_ip, time(nullptr));
            return true;
        }
        return false;
    }

private:
    void update_lru(uint32_t ip, time_t now) {
        auto it = mappings_.find(ip);
        if (it != mappings_.end()) {
            it->second.last_access = now;
            lru_list_.erase(it->second.lru_it);
            lru_list_.push_back(ip);
            it->second.lru_it = --lru_list_.end();
        }
    }

    void recycle_expired(time_t now) {
        while (!lru_list_.empty()) {
            uint32_t candidate_ip = lru_list_.front();
            auto it = mappings_.find(candidate_ip);
            if (it == mappings_.end()) {
                lru_list_.pop_front();
                continue;
            }

            if (now - it->second.last_access > MAPPING_TIMEOUT) {
                v6_to_v4_.erase(it->second.v6_addr);
                lru_list_.pop_front();
                mappings_.erase(it);
                free_ips_.push_back(candidate_ip);
            } else {
                break;
            }
        }
    }

    void create_mapping(uint32_t ip, const IPv6Address& v6, time_t now) {
        lru_list_.push_back(ip);
        MappingEntry entry;
        entry.v6_addr = v6;
        entry.last_access = now;
        entry.lru_it = --lru_list_.end();
        
        mappings_[ip] = entry;
        v6_to_v4_[v6] = ip;
    }

    std::mutex mutex_;
    std::vector<uint32_t> free_ips_;
    std::list<uint32_t> lru_list_;
    std::map<uint32_t, MappingEntry> mappings_;
    std::map<IPv6Address, uint32_t> v6_to_v4_;
};

void measure_at_size(int N) {
    if (N > 65000) N = 65000;
    DnsMapperMock mapper;
    
    // 1. Prefill
    vector<struct in6_addr> prefilled_addrs;
    for(int i = 0; i < N; i++) {
        struct in6_addr addr;
        memset(&addr, 0, sizeof(addr));
        // Fill some data to make them unique
        addr.s6_addr[15] = i & 0xFF;
        addr.s6_addr[14] = (i >> 8) & 0xFF;
        addr.s6_addr[13] = (i >> 16) & 0xFF;
        mapper.get_fake_ip(addr);
        prefilled_addrs.push_back(addr);
    }

    const int SAMPLES = 10000;

    // Hit Test: query an existing mapped IPv6 (std::map find + LRU list move)
    auto t1 = high_resolution_clock::now();
    for(int i = 0; i < SAMPLES; i++) {
        mapper.get_fake_ip(prefilled_addrs[i % N]);
    }
    auto t2 = high_resolution_clock::now();
    double hit_ns = duration_cast<nanoseconds>(t2 - t1).count() / (double)SAMPLES;

    // Rev Test: lookup fake ipv4 to target ipv6
    uint32_t target_fake_ip = FAKE_IP_START + (N / 2);
    struct in6_addr out_addr;
    auto t3 = high_resolution_clock::now();
    for(int i = 0; i < SAMPLES; i++) {
        mapper.get_real_ip(target_fake_ip, out_addr);
    }
    auto t4 = high_resolution_clock::now();
    double rev_ns = duration_cast<nanoseconds>(t4 - t3).count() / (double)SAMPLES;

    // Miss Test: completely new IPv6
    const int MISS_SAMPLES = 500;
    auto t5 = high_resolution_clock::now();
    for(int i = 0; i < MISS_SAMPLES; i++) {
        struct in6_addr addr;
        memset(&addr, 0, sizeof(addr));
        addr.s6_addr[15] = (N + i) & 0xFF;
        addr.s6_addr[14] = ((N + i) >> 8) & 0xFF;
        addr.s6_addr[13] = 0xAA; 
        mapper.get_fake_ip(addr);
    }
    auto t6 = high_resolution_clock::now();
    double miss_ns = duration_cast<nanoseconds>(t6 - t5).count() / (double)MISS_SAMPLES;

    cout << N << "," << hit_ns << "," << miss_ns << "," << rev_ns << "\n";
}

int main() {
    cout << "size,hit,miss,rev\n";
    std::vector<int> sizes = {10, 50, 100, 500, 1000, 2500, 5000, 10000, 15000, 20000, 30000, 40000, 50000, 60000, 65000};
    for (int s : sizes) {
        measure_at_size(s);
    }
    return 0;
}