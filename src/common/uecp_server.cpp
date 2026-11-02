#include "uecp_server.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#if defined(_WIN32)
// winsock2.h/ws2tcpip.h already pulled in transitively via net_common.h.
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace ucs {

namespace {
constexpr uint8_t kUecpStart = 0xFE;
constexpr uint8_t kUecpEnd = 0xFF;
} // namespace

UecpServer::UecpServer(int port) : port_(port) {}

UecpServer::~UecpServer() {
    stop();
}

void UecpServer::start() {
    if (running_.exchange(true)) return; // already running
    thread_ = std::thread(&UecpServer::run, this);
}

void UecpServer::stop() {
    if (!running_.exchange(false)) return; // wasn't running

    // Force any accept()/recv() call the run() thread might be blocked in to
    // return immediately. Closing a socket another thread is blocked on
    // makes that call fail/return on both POSIX and Windows -- this is what
    // lets stop() be called safely from the GUI thread without risking it
    // hanging until a UECP client happens to connect or send something.
    socket_t client = client_fd_.load();
    if (client != kInvalidSocket) CloseSocket(client);
    socket_t listener = listen_fd_.load();
    if (listener != kInvalidSocket) CloseSocket(listener);

    if (thread_.joinable()) thread_.join();
}

size_t UecpServer::popChunk(uint8_t* buf, size_t max_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = std::min(max_bytes, pending_.size());
    for (size_t i = 0; i < n; i++) buf[i] = pending_[i];
    pending_.erase(pending_.begin(), pending_.begin() + n);
    return n;
}

void UecpServer::unpopChunk(const uint8_t* buf, size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.insert(pending_.begin(), buf, buf + n);
}

void UecpServer::run() {
    socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == kInvalidSocket) {
        std::fprintf(stderr, "UECP socket() failed: %s\n", LastSocketError().c_str());
        running_ = false;
        return;
    }
    listen_fd_ = server_fd;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "UECP bind() on port %d failed: %s\n", port_, LastSocketError().c_str());
        CloseSocket(server_fd);
        listen_fd_ = kInvalidSocket;
        running_ = false;
        return;
    }
    if (listen(server_fd, 1) < 0) {
        std::fprintf(stderr, "UECP listen() failed: %s\n", LastSocketError().c_str());
        CloseSocket(server_fd);
        listen_fd_ = kInvalidSocket;
        running_ = false;
        return;
    }

    std::fprintf(stderr, "UECP server listening on port %d\n", port_);

    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        socket_t conn = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (conn == kInvalidSocket) {
            // Either a real accept() error, or (the common case when
            // stopping) stop() just closed server_fd out from under us.
            break;
        }
        client_fd_ = conn;

        char ip_buf[64] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::fprintf(stderr, "UECP client connected from %s\n", ip_buf);
        client_connected_ = true;

        // Receive bytes, reassemble complete 0xFE...0xFF packets.
        std::vector<uint8_t> frame_buf;
        bool in_frame = false;
        uint8_t byte;

        while (running_.load()) {
            int n = recv(conn, reinterpret_cast<char*>(&byte), 1, 0);
            if (n <= 0) break;

            if (byte == kUecpStart) {
                // Start of a new packet -- discard any incomplete previous one.
                frame_buf.clear();
                frame_buf.push_back(byte);
                in_frame = true;
            } else if (in_frame) {
                frame_buf.push_back(byte);
                if (byte == kUecpEnd) {
                    // Complete packet -- queue all its bytes.
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        for (uint8_t b : frame_buf) pending_.push_back(b);
                    }
                    packets_received_.fetch_add(1, std::memory_order_relaxed);
                    frame_buf.clear();
                    in_frame = false;
                }
            }
        }

        std::fprintf(stderr, "UECP client disconnected\n");
        client_connected_ = false;
        CloseSocket(conn);
        client_fd_ = kInvalidSocket;
        // frame_buf goes out of scope here, discarding any partial incoming
        // packet. pending_ is left intact so complete packets already
        // received continue draining into the encoded stream.
    }

    CloseSocket(server_fd);
    listen_fd_ = kInvalidSocket;
}

} // namespace ucs
