#include <iostream>
#include <chrono>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <dlfcn.h>
#include <iomanip>
#include <fcntl.h>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace std;
using namespace std::chrono;

const int SAMPLES = 1000;
const int BATCH_SIZE = 50;

int (*real_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **) = nullptr;
struct hostent* (*real_gethostbyname)(const char *) = nullptr;
int (*real_connect)(int, const struct sockaddr *, socklen_t) = nullptr;
ssize_t (*real_write)(int, const void *, size_t) = nullptr;
ssize_t (*real_read)(int, void *, size_t) = nullptr;
int (*real_close)(int) = nullptr;

void init_real_functions() {
    void* handle = dlopen("libc.so.6", RTLD_LAZY);
    real_getaddrinfo = (decltype(real_getaddrinfo))dlsym(handle, "getaddrinfo");
    real_gethostbyname = (decltype(real_gethostbyname))dlsym(handle, "gethostbyname");
    real_connect = (decltype(real_connect))dlsym(handle, "connect");
    real_write = (decltype(real_write))dlsym(handle, "write");
    real_read = (decltype(real_read))dlsym(handle, "read");
    real_close = (decltype(real_close))dlsym(handle, "close");
}

template<typename FuncReal, typename FuncFake>
void measure(const char* name, FuncReal real_f, FuncFake fake_f, int samples = SAMPLES, int batch_size = BATCH_SIZE) {
    vector<double> real_times(samples);
    vector<double> fake_times(samples);
    vector<double> overhead_times(samples);

    real_f(); fake_f();

    for (int i = 0; i < samples; i++) {
        // Alternate order
        if (i % 2 == 0) {
            auto t1 = high_resolution_clock::now();
            for(int b=0; b<batch_size; ++b) real_f();
            auto t2 = high_resolution_clock::now();
            real_times[i] = duration_cast<nanoseconds>(t2 - t1).count() / (double)batch_size;

            auto t3 = high_resolution_clock::now();
            for(int b=0; b<batch_size; ++b) fake_f();
            auto t4 = high_resolution_clock::now();
            fake_times[i] = duration_cast<nanoseconds>(t4 - t3).count() / (double)batch_size;
        } else {
            auto t1 = high_resolution_clock::now();
            for(int b=0; b<batch_size; ++b) fake_f();
            auto t2 = high_resolution_clock::now();
            fake_times[i] = duration_cast<nanoseconds>(t2 - t1).count() / (double)batch_size;

            auto t3 = high_resolution_clock::now();
            for(int b=0; b<batch_size; ++b) real_f();
            auto t4 = high_resolution_clock::now();
            real_times[i] = duration_cast<nanoseconds>(t4 - t3).count() / (double)batch_size;
        }
        overhead_times[i] = fake_times[i] - real_times[i];
    }

    auto get_stats = [](vector<double> vec) {
        double sum = 0;
        for (double v : vec) sum += v;
        double mean = sum / vec.size();
        
        sort(vec.begin(), vec.end());
        double p50 = vec[vec.size() / 2];
        double p90 = vec[vec.size() * 0.90];
        
        return make_tuple(mean, p50, p90);
    };

    auto [real_mean, real_p50, real_p90] = get_stats(real_times);
    auto [fake_mean, fake_p50, fake_p90] = get_stats(fake_times);
    
    // Calculate overhead based on the statistical aggregates, not individual jittery iteration diffs
    double ov_mean = fake_mean - real_mean;
    double ov_p50 = fake_p50 - real_p50;
    double ov_p90 = fake_p90 - real_p90;

    char real_buf[64], fake_buf[64], diff_buf[64];
    snprintf(real_buf, sizeof(real_buf), "%.0f / %.0f", real_mean, real_p50);
    snprintf(fake_buf, sizeof(fake_buf), "%.0f / %.0f", fake_mean, fake_p50);
    snprintf(diff_buf, sizeof(diff_buf), "%.0f / %.0f / %.0f", ov_mean, ov_p50, ov_p90);

    cout << left << setw(23) << name 
         << "| " << right << setw(17) << real_buf 
         << " | " << right << setw(17) << fake_buf 
         << " | " << right << setw(22) << diff_buf << "\n";
}

int main() {
    init_real_functions();
    cout << "======================================================================================\n";
    cout << "                  FakeDNS Syscall Benchmark (Latency in ns)                           \n";
    cout << "======================================================================================\n";
    cout << left << setw(23) << "Hook Name" 
         << "| " << right << setw(17) << "Orig Mean/P50" 
         << " | " << right << setw(17) << "Hook Mean/P50" 
         << " | " << right << setw(22) << "Overhead Mean/P50/P90\n";
    cout << "--------------------------------------------------------------------------------------\n";

    auto real_gai = [&]() {
        struct addrinfo hints{}, *res=nullptr; hints.ai_family = AF_INET;
        real_getaddrinfo("localhost", nullptr, &hints, &res); if (res) freeaddrinfo(res);
    };
    auto fake_gai_ipv4 = [&]() {
        struct addrinfo hints{}, *res=nullptr; hints.ai_family = AF_INET;
        getaddrinfo("localhost", nullptr, &hints, &res); if (res) freeaddrinfo(res);
    };
    measure("getaddrinfo (IPv4 std)", real_gai, fake_gai_ipv4, 1000, 50);

    auto fake_gai_ipv6 = [&]() {
        struct addrinfo hints{}, *res=nullptr; hints.ai_family = AF_INET; 
        getaddrinfo("ip6-localhost", nullptr, &hints, &res); if (res) freeaddrinfo(res);
    };
    auto real_gai_ipv6 = [&]() {
        struct addrinfo hints{}, *res=nullptr; hints.ai_family = AF_INET6;
        real_getaddrinfo("ip6-localhost", nullptr, &hints, &res); if (res) freeaddrinfo(res);
    };
    measure("getaddrinfo (fake map)", real_gai_ipv6, fake_gai_ipv6, 1000, 50);

    auto real_ghbn = [&]() { real_gethostbyname("localhost"); };
    auto fake_ghbn = [&]() { gethostbyname("localhost"); };
    measure("gethostbyname (std)", real_ghbn, fake_ghbn, 1000, 50);
    
    struct addrinfo hints2{}, *res2=nullptr; hints2.ai_family = AF_INET; getaddrinfo("ip6-localhost", nullptr, &hints2, &res2);
    uint32_t fake_ip = 0; if (res2) { fake_ip = ntohl(((struct sockaddr_in*)res2->ai_addr)->sin_addr.s_addr); freeaddrinfo(res2); }

    int s_r = socket(AF_INET, SOCK_DGRAM, 0), s_f = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin{}; sin.sin_family = AF_INET; sin.sin_addr.s_addr = htonl(0x7F000001); sin.sin_port = htons(80);
    auto real_conn = [&]() { real_connect(s_r, (struct sockaddr*)&sin, sizeof(sin)); };
    auto fake_conn = [&]() { connect(s_f, (struct sockaddr*)&sin, sizeof(sin)); };
    measure("connect (normal IP)", real_conn, fake_conn, 1000, 100);
    real_close(s_r); real_close(s_f);

    struct sockaddr_in sin_fake{}; sin_fake.sin_family = AF_INET; sin_fake.sin_addr.s_addr = htonl(fake_ip); sin_fake.sin_port = htons(8080);
    struct sockaddr_in6 sin6{}; sin6.sin6_family = AF_INET6; sin6.sin6_addr = in6addr_loopback; sin6.sin6_port = htons(8080);
    auto real_ip_conn = [&]() { int s = socket(AF_INET6, SOCK_DGRAM, 0); real_connect(s, (struct sockaddr*)&sin6, sizeof(sin6)); real_close(s); };
    auto fake_ip_conn = [&]() { int s = socket(AF_INET, SOCK_DGRAM, 0); connect(s, (struct sockaddr*)&sin_fake, sizeof(sin_fake)); close(s); };
    measure("connect (fake map std)", real_ip_conn, fake_ip_conn, 500, 50);

    struct sockaddr_in6 sin6_v4mapped{}; 
    memset(&sin6_v4mapped, 0, sizeof(sin6_v4mapped));
    sin6_v4mapped.sin6_family = AF_INET6;
    sin6_v4mapped.sin6_port = htons(8080);
    sin6_v4mapped.sin6_addr.s6_addr[10] = 0xff;
    sin6_v4mapped.sin6_addr.s6_addr[11] = 0xff;
    uint32_t net_fake_ip = htonl(fake_ip);
    memcpy(&sin6_v4mapped.sin6_addr.s6_addr[12], &net_fake_ip, 4);
    
    auto fake_v4mapped_conn = [&]() { 
        int s = socket(AF_INET6, SOCK_DGRAM, 0); 
        connect(s, (struct sockaddr*)&sin6_v4mapped, sizeof(sin6_v4mapped)); 
        close(s); 
    };
    measure("connect (v4 mapped)", real_ip_conn, fake_v4mapped_conn, 500, 50);

    int fd_n = open("/dev/null", O_WRONLY); char c = 'x';
    auto real_wr = [&]() { auto r = real_write(fd_n, &c, 1); (void)r; }; auto fake_wr = [&]() { auto r = write(fd_n, &c, 1); (void)r; };
    measure("write (data bypass)", real_wr, fake_wr, 1000, 1000);

    int fd_z = open("/dev/zero", O_RDONLY); char r;
    auto real_rd = [&]() { auto rs = real_read(fd_z, &r, 1); (void)rs; }; auto fake_rd = [&]() { auto rs = read(fd_z, &r, 1); (void)rs; };
    measure("read (data bypass)", real_rd, fake_rd, 1000, 1000);

    auto real_cl = [&]() { int f = open("/dev/null", O_RDONLY); real_close(f); }; auto fake_cl = [&]() { int f = open("/dev/null", O_RDONLY); close(f); };
    measure("close (untrack)", real_cl, fake_cl, 500, 50);

    return 0;
}
