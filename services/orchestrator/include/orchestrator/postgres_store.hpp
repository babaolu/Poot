// postgres_store.hpp — PostgreSQL persistence layer for Poot Orchestrator
// C++23 — libpq (PostgreSQL C client library)
// Schema managed externally; this module handles connection + CRUD.

#pragma once

#include <string>
#include <vector>
#include <expected>
#include <chrono>

#include <libpq-fe.h>

#include "models.hpp"

namespace Poot::Orchestrator {

// Thin RAII wrapper around a libpq PGconn*.
class PostgresStore {
public:
    PostgresStore() = default;
    ~PostgresStore();

    PostgresStore(const PostgresStore&) = delete;
    auto operator=(const PostgresStore&) -> PostgresStore& = delete;
    PostgresStore(PostgresStore&& other) noexcept;
    auto operator=(PostgresStore&& other) noexcept -> PostgresStore&;

    // Connect: "host=cloudmine user=cloudmine password=cloudmine dbname=cloudmine"
    [[nodiscard]] auto connect(const std::string& conninfo)
        -> std::expected<void, std::string>;

    [[nodiscard]] auto is_connected() const noexcept -> bool { return conn_ != nullptr; }

    // --- Miner registry ---

    [[nodiscard]] auto insert_miner(const Miner& miner) -> std::expected<void, std::string>;
    [[nodiscard]] auto update_miner(const Miner& miner) -> std::expected<void, std::string>;
    [[nodiscard]] auto load_miners() -> std::expected<std::vector<Miner>, std::string>;

    // --- Shard assignments ---

    [[nodiscard]] auto insert_assignment(const ShardAssignment& a)
        -> std::expected<void, std::string>;
    [[nodiscard]] auto load_assignments()
        -> std::expected<std::vector<ShardAssignment>, std::string>;

    // --- Offline detection helper ---

    [[nodiscard]] auto mark_offline_if_stale(std::chrono::seconds timeout)
        -> std::expected<std::vector<std::string>, std::string>;

private:
    // PostgreSQL connection handle (extern "C" from libpq)
    PGconn* conn_ = nullptr;

    [[nodiscard]] auto exec(const std::string& sql)
        -> std::expected<void, std::string>;
    [[nodiscard]] auto query(const std::string& sql)
        -> std::expected<std::vector<std::vector<std::string>>, std::string>;
};

} // namespace Poot::Orchestrator
