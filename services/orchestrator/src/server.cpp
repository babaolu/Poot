// server.cpp — HTTP API server for Poot Orchestrator
// C++23 — adheres to CppCoreGuidelines

#include "orchestrator/orchestrator.hpp"
#include "orchestrator/postgres_store.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

// Third-party: cpp-httplib (single-header HTTP library)
// We'll define a minimal HTTP server inline if cpp-httplib is not available.
// For now, we'll implement a socket-based HTTP server.

namespace Poot::Orchestrator {

// Simple HTTP request parser
struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

// Simple HTTP response builder
struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::string body;
    std::string content_type = "application/json";

    [[nodiscard]] auto to_string() const -> std::string {
        std::string resp = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
        resp += "Content-Type: " + content_type + "\r\n";
        resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        resp += "\r\n";
        resp += body;
        return resp;
    }
};

// JSON helper functions (minimal, for orchestrator API)
namespace json {

[[nodiscard]] inline auto escape(std::string_view s) -> std::string {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    return out;
}

[[nodiscard]] inline auto stringify(const std::string& s) -> std::string {
    return "\"" + escape(s) + "\"";
}

[[nodiscard]] inline auto to_json(const Miner& m) -> std::string {
    std::string j = "{";
    j += "\"id\":";
    j += stringify(m.id);
    j += ",\"wallet_address\":";
    j += stringify(m.wallet_address);
    j += ",\"storage_bytes_available\":";
    j += std::to_string(m.storage_bytes_available);
    j += ",\"storage_bytes_used\":";
    j += std::to_string(m.storage_bytes_used);
    j += ",\"compute_units_available\":";
    j += std::to_string(m.compute_units_available);
    j += ",\"compute_units_used\":";
    j += std::to_string(m.compute_units_used);
    j += ",\"status\":\"";
    j += (m.status == MinerStatus::Online ? "online" :
          m.status == MinerStatus::Offline ? "offline" : "degraded");
    j += "\",\"battery_percent\":";
    j += std::to_string(m.battery_percent);
    j += ",\"cpu_usage_percent\":";
    j += std::to_string(m.cpu_usage_percent);
    j += ",\"thermal_state\":";
    j += stringify(m.thermal_state);
    j += ",\"network_type\":";
    j += stringify(m.network_type);
    j += "}";
    return j;
}

[[nodiscard]] inline auto to_json(const std::vector<std::string>& ids) -> std::string {
    std::string j = "[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) j += ",";
        j += stringify(ids[i]);
    }
    j += "]";
    return j;
}

} // namespace json

// Parse a simple JSON key-value (for heartbeat, etc.)
[[nodiscard]] inline auto parse_json_value(std::string_view json, std::string_view key)
    -> std::optional<std::string> {
    std::string search = "\"" + std::string(key) + "\":";
    size_t pos = json.find(search);
    if (pos == std::string_view::npos) return std::nullopt;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return std::nullopt;

    if (json[pos] == '"') {
        // String value
        ++pos; // skip opening quote
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos; // skip escape
            }
            val.push_back(static_cast<char>(json[pos++]));
        }
        return val;
    } else if (json[pos] >= '0' && json[pos] <= '9') {
        // Numeric value
        std::string val;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            val.push_back(static_cast<char>(json[pos++]));
        }
        return val;
    }
    return std::nullopt;
}

// Lightweight ThreadPool for concurrent connection handling
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 64) {
        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) return;
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// The orchestrator server
class OrchestratorServer {
public:
    OrchestratorServer(int port = 8080) : port_(port), pool_(64) {}

    auto run() -> bool {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to bind to port " << port_ << std::endl;
            close(server_fd);
            return false;
        }

        if (listen(server_fd, 1024) < 0) {
            std::cerr << "Failed to listen" << std::endl;
            close(server_fd);
            return false;
        }

        // Small pause to avoid a predictable early-connect failure to Postgres
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::cout << "Poot Orchestrator listening on port " << port_ << std::endl;

        // ── Optional Postgres persistence ──────────────────────────────────
        {
            const char* db_url = std::getenv("DATABASE_URL");
            if (db_url && *db_url) {
                auto conn_result = pg_store_.connect(db_url);
                if (!conn_result) {
                    std::cerr << "WARNING: Postgres connection failed: "
                              << conn_result.error()
                              << "\nWARNING: Running WITHOUT persistence.\n";
                } else {
                    // Best-effort: hydrate from DB if tables exist
                    if (auto miners = pg_store_.load_miners()) {
                        for (const auto& m : *miners) {
                            auto r = orchestrator_.register_miner(m);
                            if (!r) std::cerr << "Hydrate skip (re-register) " << m.id << "\n";
                        }
                        std::cout << "Postgres connected — hydrated " << miners->size()
                                  << " miners from DB.\n";
                    } else {
                        std::cout << "Postgres connected — table not yet created "
                                     "(run schema script).\n";
                    }
                }
            } else {
                std::cout << "DATABASE_URL not set — running without persistence.\n";
            }
        }

        while (running_) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_fd, &readfds);

            timeval timeout{1, 0}; // 1 second timeout for clean shutdown
            int sel = select(server_fd + 1, &readfds, nullptr, nullptr, &timeout);
            if (sel < 0) break;
            if (sel == 0) continue; // Timeout, check running_ again

            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) continue;

            timeval tv{5, 0};
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            pool_.enqueue([this, client_fd]() {
                handle_client(client_fd);
                close(client_fd);
            });
        }

        close(server_fd);
        return true;
    }

    void stop() { running_ = false; }

    Orchestrator& orchestrator() { return orchestrator_; }

private:
    void handle_client(int client_fd) {
        char buffer[4096]{};
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) return;

        HttpRequest req;
        if (!parse_request(std::string_view(buffer, bytes), req)) {
            send_response(client_fd, HttpResponse{400, "Bad Request", "{\"error\":\"Invalid request\"}"});
            return;
        }

        HttpResponse resp;
        if (req.method == "GET" && req.path == "/health") {
            resp.body = "{\"status\":\"healthy\"}";
        } else if (req.method == "GET" && req.path == "/miners") {
            auto ids = orchestrator_.list_miners();
            resp.body = json::to_json(ids);
        } else if (req.method == "GET" && req.path == "/miners/online") {
            auto ids = orchestrator_.list_online_miners();
            resp.body = json::to_json(ids);
        } else if (req.method == "GET" && req.path.find("/miner/") == 0) {
            std::string miner_id = req.path.substr(7);
            auto miner = orchestrator_.get_miner(miner_id);
            if (miner.has_value()) {
                resp.body = json::to_json(miner->get());
            } else {
                resp = HttpResponse{404, "Not Found", "{\"error\":\"Miner not found\"}"};
            }
        } else if (req.method == "POST" && req.path == "/miner/register") {
            Miner m;
            auto id = parse_json_value(req.body, "id");
            if (!id.has_value()) {
                resp = HttpResponse{400, "Bad Request", "{\"error\":\"Missing miner id\"}"};
            } else {
                m.id = *id;
                auto wallet = parse_json_value(req.body, "wallet_address");
                if (wallet.has_value()) m.wallet_address = *wallet;
                auto storage = parse_json_value(req.body, "storage_bytes_available");
                if (storage.has_value()) m.storage_bytes_available = std::stoull(*storage);

                auto result = orchestrator_.register_miner(m);
                if (result.has_value()) {
                    resp.body = "{\"id\":\"" + m.id + "\",\"status\":\"registered\"}";
                    if (pg_store_.is_connected()) {
                        if (auto pr = pg_store_.insert_miner(m); !pr) {
                            std::cerr << "Postgres insert_miner failed: " << pr.error() << "\n";
                        }
                    }
                } else {
                    resp = HttpResponse{400, "Bad Request", "{\"error\":\"" + result.error() + "\"}"};
                }
            }
        } else if (req.method == "POST" && req.path == "/heartbeat") {
            HeartbeatRequest hb;
            auto miner_id = parse_json_value(req.body, "miner_id");
            if (!miner_id.has_value()) {
                resp = HttpResponse{400, "Bad Request", "{\"error\":\"Missing miner_id\"}"};
            } else {
                hb.miner_id = *miner_id;
                auto val = parse_json_value(req.body, "storage_bytes_available");
                if (val.has_value()) hb.storage_bytes_available = std::stoull(*val);
                val = parse_json_value(req.body, "battery_percent");
                if (val.has_value()) hb.battery_percent = std::stoi(*val);
                val = parse_json_value(req.body, "thermal_state");
                if (val.has_value()) hb.thermal_state = *val;
                val = parse_json_value(req.body, "network_type");
                if (val.has_value()) hb.network_type = *val;

                auto result = orchestrator_.handle_heartbeat(hb);
                if (result.has_value()) {
                    resp.body = "{\"accepted\":true}";
                    if (pg_store_.is_connected()) {
                        if (auto upd_m = orchestrator_.get_miner(hb.miner_id)) {
                            if (auto pr = pg_store_.update_miner(upd_m->get()); !pr) {
                                std::cerr << "Postgres update_miner failed: " << pr.error() << "\n";
                            }
                        }
                    }
                } else {
                    resp = HttpResponse{404, "Not Found", "{\"error\":\"" + result.error() + "\"}"};
                }
            }
        } else if (req.method == "POST" && req.path == "/check-offline") {
            auto offline = orchestrator_.check_offline_miners();
            resp.body = json::to_json(offline);
        } else {
            resp = HttpResponse{404, "Not Found", "{\"error\":\"Endpoint not found\"}"};
        }

        send_response(client_fd, resp);
    }

    bool parse_request(std::string_view raw, HttpRequest& out) {
        size_t pos = raw.find(' ');
        if (pos == std::string_view::npos) return false;
        out.method = std::string(raw.substr(0, pos));

        raw.remove_prefix(pos + 1);
        pos = raw.find(' ');
        if (pos == std::string_view::npos) return false;
        out.path = std::string(raw.substr(0, pos));

        // Find body
        size_t body_pos = raw.find("\r\n\r\n");
        if (body_pos != std::string_view::npos) {
            out.body = std::string(raw.substr(body_pos + 4));
        }

        return true;
    }

    void send_response(int client_fd, const HttpResponse& resp) {
        std::string raw = resp.to_string();
        send(client_fd, raw.c_str(), raw.size(), 0);
    }

    int port_;
    std::atomic<bool> running_ = true;
    ThreadPool pool_;
    Orchestrator orchestrator_;
    PostgresStore pg_store_;
};

} // namespace Poot::Orchestrator

// Main entry point
int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    Poot::Orchestrator::OrchestratorServer server(port);
    server.run();
    return 0;
}
