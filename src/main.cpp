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
#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <stdlib.h>

#include "PacketHandler.h"
#include "../include/CRC.h"
#include "utils/PacketDispatcher.h"

#define ADD_TO_CALLBACK(packet_type, capture, name, body) packet_type::callbacks.emplace_back([capture](std::shared_ptr<packet_type> name){body});
//#define TEST

const uint16_t MAX_SIZE_FILE = 8192;
void readFile(std::ifstream* file, const std::shared_ptr<std::vector<uint8_t>>& data, uint16_t SIZE = MAX_SIZE_FILE) {
    file->read(reinterpret_cast<char *>(data->data()), SIZE);
}
void flash_packet(PacketDispatcher* dispatcher, std::ifstream* file, std::function<void(std::shared_ptr<IPacket>)>* sendPacket, std::atomic<bool>* done) {
    dispatcher->registerCallBack<StartFlashPacket>([dispatcher, file, sendPacket, done](std::shared_ptr<StartFlashPacket> start_flash_packet) {
        std::shared_ptr<bool> kill_packets = std::make_shared<bool>(false);
        dispatcher->registerCallBack<ReceivedDataPacket>([file, sendPacket, kill_packets](std::shared_ptr<ReceivedDataPacket> received_data_packet) {
            std::cout << "." << std::flush;
            auto data = std::make_shared<std::vector<uint8_t>>();
            data->resize(MAX_SIZE_FILE);
            readFile(file, data, MAX_SIZE_FILE);
            data->resize(file->gcount());
            if (file->gcount() != 0) {
                (*sendPacket)(std::make_shared<DataPacket>(*data));
            }
            return !*kill_packets;
        });        
        auto data = std::make_shared<std::vector<uint8_t>>();
        data->resize(MAX_SIZE_FILE);
        readFile(file, data, MAX_SIZE_FILE);
        std::streamsize count = file->gcount();
        std::cout << "Start flashing" << std::endl;
        std::cout << "Progress.";
        if (count != 0) {

            (*sendPacket)(std::make_shared<DataPacket>(*data));
        }
        dispatcher->registerCallBack<FlashingSoftwarePacket>([kill_packets, done](std::shared_ptr<FlashingSoftwarePacket> flashing_software_packet) {
            std::cout << "Flashing new code" << std::endl;
            *kill_packets = true;
            *done = true;
            return true;
        });
        
        return true;
    });
    dispatcher->registerCallBack<IssueStartingFlashingPacket>([done](std::shared_ptr<IssueStartingFlashingPacket> issue_starting_flashing_packet) {
        std::cout << "Issue while starting flashing" << std::endl;
        *done = true;
        return true;
    });
    dispatcher->registerCallBack<AlreadyFlashingPacket>([done](std::shared_ptr<AlreadyFlashingPacket> already_flashing_packet) {
        std::cout << "Already flashing" << std::endl;
        *done = true;
        return true;
    });
    dispatcher->registerCallBack<IssueFlashingPacket>([done](std::shared_ptr<IssueFlashingPacket> issue_flashing_packet) {
        std::cout << "Issue while flashing" << std::endl;
        *done = true;
        return true;
    });
    std::cout << "Waiting for start flashing..." << std::endl;
    (*sendPacket)(std::make_shared<StartFlashPacket>());
}

#define RECEIVED_PACKET_ANNOUNCER\
    ANNOUNCER(AlreadyFlashingPacket)\
    ANNOUNCER(IssueStartingFlashingPacket)\
    ANNOUNCER(StartFlashPacket)\
    ANNOUNCER(FlashingSoftwarePacket)\
    ANNOUNCER(ReceivedDataPacket)

#ifdef TEST
int main(int argc, char* argv[]) {
    std::cout << std::endl;
    PacketHandler packet_handler;
    TestbitPacket packet(256);
    auto packet_v = packet_handler.createPacket(packet);
    packet_v.at(9) = 0xFF;
    packet_handler.receiveData(packet_v);
    packet_v = packet_handler.createPacket(packet);
    packet_handler.receiveData(packet_v);
    for (auto a : packet_handler.getBuffer()) {
        printf("%02x ", a);
    }
    std::cout << std::endl;
    auto [flag, result] = packet_handler.checkPacket();
    std::cout << "Here" << std::endl;
    std::cout << flag << std::endl;
    auto [flag2, result2] = packet_handler.checkPacket();
    std::cout << flag2 << std::endl;
    std::cout << result2 << std::endl;
    auto [flag3, result3] = packet_handler.checkPacket();
    std::cout << flag3 << std::endl;
    std::cout << result3 << std::endl;




}
#endif

#ifdef TEST
int previous(int argc, char* argv[]){
#else
int main(int argc, char* argv[]){
#endif
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

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(argv[1], argv[2], &hints, &res);
    if (err != 0) {
        std::cerr << "getaddrinfo failed: " << gai_strerror(err) << "\n";
        return 1;
    }

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }
    if (
#ifdef _WIN32
connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR
#else
        connect(sock, res->ai_addr, res->ai_addrlen) < 0
#endif
            ) {
        std::cerr << "Connection failed.\n";
        freeaddrinfo(res);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }

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
    PacketHandler handler;
    std::mutex sending_packet;
    std::function<void(std::shared_ptr<IPacket>)> sendPacket = [&](std::shared_ptr<IPacket> packet) {
        sending_packet.lock();
        auto pp_ = handler.createPacket(packet);
        send(sock,reinterpret_cast<const char *>(pp_.data()), pp_.size(), 0);
        sending_packet.unlock();
    };
    std::vector<std::shared_ptr<IPacket>> dispatcher_packets;
    std::vector<std::shared_ptr<IPacket>> dispatcher_packets_locked;
    PacketDispatcher dispatcher;
    uint8_t buffer[1024];
    std::atomic<bool> disable_thread = false;
    std::mutex loggerMutex;
    std::mutex dispatching_mutex;
    std::mutex dispatching_mutex_locked;
    loggerMutex.lock();//No threads but for the example
    std::cout << "Connection established, starting recv thread" << std::endl;
    loggerMutex.unlock();
    std::thread t([&] {
        while (!disable_thread) {
            int length = recv(sock, reinterpret_cast<char *>(buffer), 1024, 0);
#ifdef WIN32
            if (length == -1) {
                int err = WSAGetLastError();
                switch (err) {
                    case WSAENOTCONN:
                        std::cerr << "recv error: socket not connected\n";
                        disable_thread = true;
                        return;
                    case WSAENOTSOCK:
                        std::cerr << "recv error: not a socket\n";
                        disable_thread= true;
                        return;

                    case WSAEINTR:
                        std::cerr << "recv error: interrupted\n"; break;
                        disable_thread = true;
                        return;
                    case WSAEWOULDBLOCK: break; // OK in non-blocking
                    case WSAEINVAL:
                        std::cerr << "recv error: invalid argument (socket state?)\n";
                        disable_thread = true;
                        return;
                    default: {
                        std::cerr << "recv failed: WSAGetLastError() = " << err << "\n";
                        disable_thread = true;
                        return;
                        break;
                    }
                }
            }
#else
            if (length == -1) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    // Non-blocking socket, no data yet
                } else {
                    perror("recv");
                }
            }
#endif
            handler.receiveData(buffer, length);
            auto checkStatus = CheckStatus::BAD_CRC;
            std::shared_ptr<IPacket> packet;
            while (checkStatus != CheckStatus::WAITING_LENGTH && checkStatus!= CheckStatus::WAITING_DATA) {
                auto r =  handler.checkPacket();
                checkStatus = std::get<0>(r);
                packet = std::get<1>(r);
                if (checkStatus == CheckStatus::EXECUTED_PACKET) {
                    if (dispatching_mutex.try_lock()) {
                        dispatcher_packets.push_back(packet);
                        dispatching_mutex.unlock();
                    }else {
                        dispatching_mutex_locked.lock();
                        dispatcher_packets_locked.push_back(packet);
                        dispatching_mutex_locked.unlock();
                    }
                }

            }
        }
    });

    std::thread dispatcher_thread([&] {
        while (!disable_thread) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            dispatching_mutex.lock();
            for (const auto& packet : dispatcher_packets) {
                dispatcher.dispatch(packet);
            }
            dispatcher_packets.clear();
            dispatching_mutex.unlock();
            dispatching_mutex_locked.lock();
            for (const auto& packet : dispatcher_packets_locked) {
                dispatcher.dispatch(packet);
            }
            dispatcher_packets_locked.clear();
            dispatching_mutex_locked.unlock();
        }
    });

    dispatcher.registerCallBack<PingPacket>([&](std::shared_ptr<PingPacket> ping)->bool {
        loggerMutex.lock();
        std::cout << "Ping received\n";
        loggerMutex.unlock();
        sendPacket(std::make_shared<PongPacket>(ping->getUniqueID()));
        return false;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    loggerMutex.lock();
    std::cout << "Flashing" << std::endl;
    loggerMutex.unlock();
    flash_packet(&dispatcher, &file, &sendPacket, &disable_thread);

    while (!disable_thread) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Ending" << std::endl;
    closesocket(sock);
    file.close();
    t.join();
    dispatcher_thread.join();
    freeaddrinfo(res);
#ifdef _WIN32

    WSACleanup();
#else
    close(sock);
#endif
    return 0;
}
