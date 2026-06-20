#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

int main(int argc, char* argv[]) {
    const char* hostname = "ip6-localhost"; 
    if (argc > 1) hostname = argv[1];

    std::cout << "[Legacy Client] Resolving " << hostname << " with gethostbyname..." << std::endl;
    struct hostent* he = gethostbyname(hostname);
    if (he == nullptr) {
        herror("[Legacy Client] gethostbyname failed");
        return 1;
    }

    if (he->h_addrtype != AF_INET) {
        std::cerr << "[Legacy Client] Expected AF_INET address type" << std::endl;
        return 1;
    }

    struct in_addr** addr_list = (struct in_addr**)he->h_addr_list;
    if (addr_list[0] == nullptr) {
        std::cerr << "[Legacy Client] No address found" << std::endl;
        return 1;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, addr_list[0], ip_str, sizeof(ip_str));
    std::cout << "[Legacy Client] Resolved to IPv4: " << ip_str << std::endl;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[Legacy Client] socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr = *addr_list[0];

    std::cout << "[Legacy Client] Connecting to " << ip_str << ":8080..." << std::endl;
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Legacy Client] connect");
        return 1;
    }

    const char* msg = "Hello from Legacy IPv4 Client!";
    send(sock, msg, strlen(msg), 0);
    std::cout << "[Legacy Client] Sent message" << std::endl;

    char buffer[1024] = {0};
    ssize_t n = read(sock, buffer, 1024);
    if (n > 0) {
        std::cout << "[Legacy Client] Received: " << buffer << std::endl;
    }

    close(sock);
    return 0;
}
