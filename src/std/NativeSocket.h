#ifndef NATIVE_SOCKET_H
#define NATIVE_SOCKET_H

#include "core/Native.h"
#include "core/Value.h"
#include <iostream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

namespace iris::std_lib {

    inline void initWSA() {
#ifdef _WIN32
        static bool wsaInitialized = false;
        if (!wsaInitialized) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
                wsaInitialized = true;
            }
        }
#endif
    }

    class NativeSocket : public iris::core::NativeObject {
        SOCKET sock;
        bool connected;

    public:
        NativeSocket() : sock(INVALID_SOCKET), connected(false) {
            initWSA();
            sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        }

        explicit NativeSocket(SOCKET s) : sock(s), connected(s != INVALID_SOCKET) {
            initWSA();
        }

        ~NativeSocket() override {
            closeSocket();
        }

        void closeSocket() {
            if (sock != INVALID_SOCKET) {
#ifdef _WIN32
                closesocket(sock);
#else
                close(sock);
#endif
                sock = INVALID_SOCKET;
            }
            connected = false;
        }

        iris::core::Value callMethod(const std::string &name, iris::core::Value *args, int argCount) override {
            if (name == "connect") {
                if (argCount < 2 || !args[0].isString() || !args[1].isInt()) return iris::core::Value(false);
                std::string host = args[0].str();
                int port = args[1].asInt();

                struct addrinfo hints{}, *result = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;

                if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
                    return iris::core::Value(false);
                }

                if (sock == INVALID_SOCKET) {
                    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                }

                if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
                    freeaddrinfo(result);
                    return iris::core::Value(false);
                }

                freeaddrinfo(result);
                connected = true;
                return iris::core::Value(true);
            }

            if (name == "send") {
                if (argCount < 1 || !args[0].isString()) return iris::core::Value(0);
                if (!connected || sock == INVALID_SOCKET) return iris::core::Value(0);
                std::string data = args[0].str();
                int bytesSent = ::send(sock, data.c_str(), (int)data.length(), 0);
                if (bytesSent == SOCKET_ERROR) {
                    connected = false;
                    return iris::core::Value(0);
                }
                return iris::core::Value(bytesSent);
            }

            if (name == "recv") {
                if (argCount < 1 || !args[0].isInt()) return iris::core::Value("");
                if (!connected || sock == INVALID_SOCKET) return iris::core::Value("");
                int size = args[0].asInt();
                if (size <= 0) return iris::core::Value("");

                std::string buffer(size, '\0');
                int bytesReceived = ::recv(sock, &buffer[0], size, 0);
                if (bytesReceived <= 0) {
                    connected = false;
                    return iris::core::Value("");
                }
                buffer.resize(bytesReceived);
                return iris::core::Value(buffer);
            }

            if (name == "close") {
                closeSocket();
                return iris::core::Value();
            }

            if (name == "isOpen") {
                return iris::core::Value(connected && sock != INVALID_SOCKET);
            }

            return iris::core::NativeObject::callMethod(name, args, argCount);
        }

        std::string toString() const override { return "Socket"; }
    };

    class NativeServerSocket : public iris::core::NativeObject {
        SOCKET sock;
        bool listening;

    public:
        NativeServerSocket() : sock(INVALID_SOCKET), listening(false) {
            initWSA();
            sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        }

        ~NativeServerSocket() override {
            closeSocket();
        }

        void closeSocket() {
            if (sock != INVALID_SOCKET) {
#ifdef _WIN32
                closesocket(sock);
#else
                close(sock);
#endif
                sock = INVALID_SOCKET;
            }
            listening = false;
        }

        iris::core::Value callMethod(const std::string &name, iris::core::Value *args, int argCount) override {
            if (name == "listen") {
                if (argCount < 1 || !args[0].isInt()) return iris::core::Value(false);
                int port = args[0].asInt();

                sockaddr_in service{};
                service.sin_family = AF_INET;
                service.sin_addr.s_addr = INADDR_ANY;
                service.sin_port = htons(port);

                int opt = 1;
#ifdef _WIN32
                setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
                setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

                if (bind(sock, (sockaddr*)&service, sizeof(service)) == SOCKET_ERROR) {
                    return iris::core::Value(false);
                }

                if (::listen(sock, SOMAXCONN) == SOCKET_ERROR) {
                    return iris::core::Value(false);
                }

                listening = true;
                return iris::core::Value(true);
            }

            if (name == "accept") {
                if (!listening || sock == INVALID_SOCKET) return iris::core::Value();
                SOCKET clientSock = ::accept(sock, nullptr, nullptr);
                if (clientSock == INVALID_SOCKET) {
                    return iris::core::Value();
                }
                return iris::core::Value(new NativeSocket(clientSock));
            }

            if (name == "close") {
                closeSocket();
                return iris::core::Value();
            }

            return iris::core::NativeObject::callMethod(name, args, argCount);
        }

        std::string toString() const override { return "ServerSocket"; }
    };

}

#endif //NATIVE_SOCKET_H
