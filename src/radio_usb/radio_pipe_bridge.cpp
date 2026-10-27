/* radio_pipe_bridge: connect WinAWDL's awdld (named pipe) to our TCP radio.
 *
 * awdld opens \\.\pipe\winawdl-radio and speaks radio_link v3. Our radio listens
 * on 127.0.0.1:49876. This bridge is a byte pump; it does not parse or change
 * the protocol.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <thread>

#include "radio_link.h"

namespace {

constexpr const char *kTcpHost = "127.0.0.1";
constexpr int kTcpPort = 49876;

SOCKET connect_tcp() {
    for (int attempt = 0; attempt < 120; attempt++) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return INVALID_SOCKET;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kTcpPort);
        inet_pton(AF_INET, kTcpHost, &addr.sin_addr);
        if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
            return sock;
        }
        closesocket(sock);
        Sleep(250);
    }
    return INVALID_SOCKET;
}

bool send_all(SOCKET sock, const char *data, int len) {
    while (len > 0) {
        const int n = send(sock, data, len, 0);
        if (n <= 0) return false;
        data += n;
        len -= n;
    }
    return true;
}

}  // namespace

int main() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "bridge: WSAStartup failed\n");
        return 1;
    }

    HANDLE pipe = CreateNamedPipeA(
        RADIO_LINK_PIPE_NAME, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1 << 20, 1 << 20,
        0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "bridge: CreateNamedPipe failed (%lu)\n",
                     GetLastError());
        WSACleanup();
        return 1;
    }
    std::printf("bridge: waiting for awdld on %s\n", RADIO_LINK_PIPE_NAME);
    if (!ConnectNamedPipe(pipe, nullptr) &&
        GetLastError() != ERROR_PIPE_CONNECTED) {
        std::fprintf(stderr, "bridge: ConnectNamedPipe failed (%lu)\n",
                     GetLastError());
        CloseHandle(pipe);
        WSACleanup();
        return 1;
    }
    std::printf("bridge: awdld connected; connecting to radio TCP\n");

    const SOCKET tcp = connect_tcp();
    if (tcp == INVALID_SOCKET) {
        std::fprintf(stderr, "bridge: cannot connect to radio at %s:%d\n",
                     kTcpHost, kTcpPort);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        WSACleanup();
        return 1;
    }
    std::printf("bridge: connected; pumping bytes\n");

    std::atomic<bool> stop{false};
    auto shutdown_all = [&] {
        if (!stop.exchange(true)) {
            shutdown(tcp, SD_BOTH);
            DisconnectNamedPipe(pipe);
        }
    };

    std::thread up([&] {
        char buffer[16384];
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(pipe, buffer, sizeof(buffer), &got, nullptr) ||
                got == 0) {
                break;
            }
            if (!send_all(tcp, buffer, static_cast<int>(got))) break;
        }
        shutdown_all();
    });

    char buffer[16384];
    for (;;) {
        const int n = recv(tcp, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        DWORD wrote = 0;
        if (!WriteFile(pipe, buffer, static_cast<DWORD>(n), &wrote, nullptr) ||
            wrote == 0) {
            break;
        }
    }
    shutdown_all();
    up.join();

    closesocket(tcp);
    CloseHandle(pipe);
    WSACleanup();
    return 0;
}
