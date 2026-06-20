#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

int main(int argc, char* argv[]) {
    const char* hostname = "ip6-localhost"; 
    if (argc > 1) hostname = argv[1];

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // Request IPv4 ONLY
    hints.ai_socktype = SOCK_STREAM;

    std::cout << "[Client] Resolving " << hostname << " with AF_INET..." << std::endl;
    int err = getaddrinfo(hostname, "8080", &hints, &res);
    if (err != 0) {
        std::cerr << "[Client] getaddrinfo failed: " << gai_strerror(err) << std::endl;
        return 1;
    }

    struct sockaddr_in* ipv4 = (struct sockaddr_in*)res->ai_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ipv4->sin_addr, ip_str, sizeof(ip_str));
    std::cout << "[Client] Resolved to IPv4: " << ip_str << std::endl;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[Client] socket");
        return 1;
    }

    std::cout << "[Client] Connecting to " << ip_str << ":8080..." << std::endl;
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("[Client] connect");
        return 1;
    }

    const char* msg = "Hello from IPv4 Client!";
    send(sock, msg, strlen(msg), 0);
    std::cout << "[Client] Sent message" << std::endl;

    char buffer[1024] = {0};
    ssize_t n = read(sock, buffer, 1024);
    if (n > 0) {
        std::cout << "[Client] Received: " << buffer << std::endl;
    }

    close(sock);
    freeaddrinfo(res);
    return 0;
}
