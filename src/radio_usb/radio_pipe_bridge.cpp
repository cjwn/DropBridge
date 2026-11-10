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

/* The pipe handle is overlapped: on a synchronous handle Windows serialises
 * all I/O, so a radio->awdld WriteFile waited for the pending awdld->radio
 * ReadFile (i.e. for awdld's next transmit), and DisconnectNamedPipe blocked
 * the same way. Each caller passes its own manual-reset event and waits for
 * its own operation only. */
bool pipe_wait(HANDLE pipe, OVERLAPPED &ov, BOOL started, DWORD *done) {
    if (!started && GetLastError() != ERROR_IO_PENDING) return false;
    return GetOverlappedResult(pipe, &ov, done, TRUE) != FALSE;
}

bool pipe_read(HANDLE pipe, HANDLE event, char *data, DWORD len, DWORD *got) {
    OVERLAPPED ov{};
    ov.hEvent = event;
    return pipe_wait(pipe, ov, ReadFile(pipe, data, len, nullptr, &ov), got);
}

bool pipe_write_all(HANDLE pipe, HANDLE event, const char *data, DWORD len) {
    while (len > 0) {
        OVERLAPPED ov{};
        ov.hEvent = event;
        DWORD wrote = 0;
        if (!pipe_wait(pipe, ov, WriteFile(pipe, data, len, nullptr, &ov), &wrote) ||
            wrote == 0) {
            return false;
        }
        data += wrote;
        len -= wrote;
    }
    return true;
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "bridge: WSAStartup failed\n");
        return 1;
    }

    HANDLE pipe = CreateNamedPipeA(
        RADIO_LINK_PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1 << 20, 1 << 20,
        0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "bridge: CreateNamedPipe failed (%lu)\n",
                     GetLastError());
        WSACleanup();
        return 1;
    }
    // One manual-reset event per thread that does pipe I/O.
    HANDLE up_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE down_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (up_event == nullptr || down_event == nullptr) {
        std::fprintf(stderr, "bridge: CreateEvent failed (%lu)\n", GetLastError());
        if (up_event != nullptr) CloseHandle(up_event);
        if (down_event != nullptr) CloseHandle(down_event);
        CloseHandle(pipe);
        WSACleanup();
        return 1;
    }
    std::printf("bridge: waiting for awdld on %s\n", RADIO_LINK_PIPE_NAME);
    {
        // An overlapped pipe needs an OVERLAPPED here too.
        OVERLAPPED ov{};
        ov.hEvent = down_event;
        DWORD unused = 0;
        const BOOL started = ConnectNamedPipe(pipe, &ov);
        if (!started && GetLastError() != ERROR_PIPE_CONNECTED &&
            !pipe_wait(pipe, ov, started, &unused)) {
            std::fprintf(stderr, "bridge: ConnectNamedPipe failed (%lu)\n",
                         GetLastError());
            CloseHandle(up_event);
            CloseHandle(down_event);
            CloseHandle(pipe);
            WSACleanup();
            return 1;
        }
    }
    std::printf("bridge: awdld connected; connecting to radio TCP\n");

    const SOCKET tcp = connect_tcp();
    if (tcp == INVALID_SOCKET) {
        std::fprintf(stderr, "bridge: cannot connect to radio at %s:%d\n",
                     kTcpHost, kTcpPort);
        DisconnectNamedPipe(pipe);
        CloseHandle(up_event);
        CloseHandle(down_event);
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
            // Wake a pending pipe read/write in the other thread.
            CancelIoEx(pipe, nullptr);
        }
    };

    std::thread up([&] {
        char buffer[16384];
        while (!stop.load()) {
            DWORD got = 0;
            if (!pipe_read(pipe, up_event, buffer, sizeof(buffer), &got) ||
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
        if (stop.load() ||
            !pipe_write_all(pipe, down_event, buffer, static_cast<DWORD>(n))) {
            break;
        }
    }
    shutdown_all();
    up.join();

    closesocket(tcp);
    CloseHandle(up_event);
    CloseHandle(down_event);
    CloseHandle(pipe);
    WSACleanup();
    return 0;
}
