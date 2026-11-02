#include "icecast_source_client.h"

#include <cstring>

#include "base64.h"

namespace ucs {

bool IcecastSourceClient::connect(const IcecastConfig& cfg, const std::string& content_type) {
    close();
    send_buffer_.clear();
    send_buffer_threshold_ = static_cast<size_t>(cfg.buffer_kb) * 1024;

    if (!socket_.connectTo(cfg.host, cfg.port)) {
        last_error_ = socket_.lastError();
        return false;
    }

    std::string mount = cfg.mount;
    if (mount.empty() || mount[0] != '/') mount = "/" + mount;

    std::string auth = Base64Encode(cfg.user + ":" + cfg.password);

    std::string request;
    request += "SOURCE " + mount + " HTTP/1.0\r\n";
    request += "Authorization: Basic " + auth + "\r\n";
    request += "Content-Type: " + content_type + "\r\n";
    request += "ice-name: " + cfg.name + "\r\n";
    request += "ice-public: 0\r\n";
    request += "\r\n";

    if (!socket_.sendAll(request)) {
        last_error_ = socket_.lastError();
        close();
        return false;
    }

    // Read the response byte-by-byte until the header terminator. Unlike
    // mp2anc's original (a plain blocking recv() with no timeout at all),
    // each byte here is bounded by TcpSocket::recvSome's timeout -- a
    // silent/misbehaving Icecast server can't hang this indefinitely, which
    // matters more here than in a one-shot CLI tool since this runs on a
    // thread the GUI needs to be able to stop promptly.
    char resp[1024] = {};
    int rlen = 0;
    while (rlen < static_cast<int>(sizeof(resp)) - 1) {
        uint8_t byte = 0;
        int n = socket_.recvSome(&byte, 1, 5000);
        if (n <= 0) break;
        resp[rlen++] = static_cast<char>(byte);
        if (rlen >= 4 && std::memcmp(resp + rlen - 4, "\r\n\r\n", 4) == 0) break;
    }
    if (!std::strstr(resp, "200 OK")) {
        last_error_ = std::string("Icecast rejected connection: ") + resp;
        close();
        return false;
    }

    return true;
}

bool IcecastSourceClient::sendBlock(const uint8_t* data, size_t len) {
    if (send_buffer_threshold_ == 0) {
        // Coalescing disabled -- send immediately.
        if (!socket_.sendAll(data, len)) {
            last_error_ = socket_.lastError();
            return false;
        }
        return true;
    }

    send_buffer_.insert(send_buffer_.end(), data, data + len);
    if (send_buffer_.size() < send_buffer_threshold_)
        return true; // accumulating -- nothing written yet, so no I/O failure possible

    return flush();
}

bool IcecastSourceClient::flush() {
    if (send_buffer_.empty()) return true;
    bool ok = socket_.sendAll(send_buffer_.data(), send_buffer_.size());
    if (!ok) last_error_ = socket_.lastError();
    send_buffer_.clear();
    return ok;
}

void IcecastSourceClient::close() {
    flush(); // best-effort -- socket_.sendAll() is already a safe no-op if not open
    socket_.close();
}

} // namespace ucs
