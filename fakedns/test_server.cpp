#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {
    int server_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        return 1;
    }

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any; // ::1
    addr.sin6_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "[Server] Listening on [::]:8080 (IPv6 only)" << std::endl;

    struct sockaddr_in6 client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept");
        return 1;
    }

    char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &client_addr.sin6_addr, str, sizeof(str));
    std::cout << "[Server] Accepted connection from " << str << std::endl;

    char buffer[1024] = {0};
    ssize_t n = read(client_fd, buffer, 1024);
    if (n > 0) {
        std::cout << "[Server] Received: " << buffer << std::endl;
    }

    const char* msg = "Hello from IPv6 Server!";
    send(client_fd, msg, strlen(msg), 0);
    std::cout << "[Server] Sent response" << std::endl;

    close(client_fd);
    close(server_fd);
    return 0;
}
