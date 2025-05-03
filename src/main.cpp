#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
#endif
#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))
#include <iostream>
#include <fstream>
#include <string>
#include "../include/CRC.h"

int main(int argc, char* argv[]) {
    if(argc < 4){
        std::cerr << "Usage : uploader <ip> <port> <firmware_path>" << std::endl;
        return 1;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    // Create socket
#ifdef _WIN32
SOCKET sock;
#else
    int sock;
#endif
#ifdef _WIN32
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }
#else
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }
#endif

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(std::stoi(argv[2]));
    inet_pton(AF_INET, argv[1], &serverAddr.sin_addr);

    if (
#ifdef _WIN32
connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR
#else
        connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0
#endif
            ) {
        std::cerr << "Connection failed.\n";
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }
    char receiveBuffer[1024];





    std::ifstream file(argv[3], std::ios::binary);
    if (!file) {
        std::cerr << "Could not open firmware file.\n";
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }
    char buffer[1032];
    while (file.read(buffer, sizeof(buffer)-8) || file.gcount() > 0) {
        uint64_t data = algoCRC_64_JONES.computeCRC(reinterpret_cast<const uint8_t *>(buffer), file.gcount());
        std::cout << "CRC : " << data << " file : " << file.gcount() << std::endl;
        *((uint64_t*)&buffer[file.gcount()]) = htonll(data);
#ifdef _WIN32
        send(sock, buffer, static_cast<int>(file.gcount()+8), 0);
#else
        send(sock, buffer, file.gcount(), 0);
#endif
    }

    std::cout << "Upload complete.\n";

    file.close();

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return 0;
}
