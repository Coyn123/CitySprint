#include <iostream>
#include <thread>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define WSAGetLastError() (errno)
#endif

#include "server.h"

SOCKET server_fd;
int port = 6969;

#ifdef _WIN32
unsigned __stdcall client_thread(void* args) {
    SOCKET client_socket = (SOCKET)(intptr_t)((void**)args)[0];
    delete[] static_cast<void**>(args);

    handle_client(client_socket);
    return 0;
}
#else
void* client_thread(void* args) {
    SOCKET client_socket = (SOCKET)(intptr_t)((void**)args)[0];
    delete[] static_cast<void**>(args);

    handle_client(client_socket);
    return nullptr;
}
#endif

int main(int argc, char* argv[]) {
    // See if we got a port from our user, set it if not
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
            if (port < 1 || port > 65535) {
                throw std::out_of_range("Port number out of range");
            }
        } catch (const std::exception& e) {
            std::cerr << "Invalid port number. Using default port 6969" << std::endl;
        }
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return 1;
    }
#endif

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "Server listening on port " << port << std::endl;

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_addr_size = sizeof(client_addr);
        SOCKET client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_size);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        void** args = new void*[1];
        args[0] = (void*)(intptr_t)client_socket;

#ifdef _WIN32
        _beginthreadex(nullptr, 0, client_thread, args, 0, nullptr);
#else
        pthread_t thread_id;
        pthread_create(&thread_id, nullptr, client_thread, args);
        pthread_detach(thread_id);
#endif
    }

    closesocket(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
