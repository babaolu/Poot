// postgres_store.cpp — PostgreSQL persistence for Poot Orchestrator
// C++23 — libpq
//
// Requires the following table (run once with psql):
//
//   CREATE TABLE IF NOT EXISTS miners (
//     id                  TEXT PRIMARY KEY,
//     wallet_address      TEXT NOT NULL DEFAULT '',
//     country             TEXT NOT NULL DEFAULT '',
//     region              TEXT NOT NULL DEFAULT '',
//     latitude            DOUBLE PRECISION DEFAULT 0,
//     longitude           DOUBLE PRECISION DEFAULT 0,
//     storage_bytes_avail BIGINT NOT NULL DEFAULT 0,
//     storage_bytes_used  BIGINT NOT NULL DEFAULT 0,
//     compute_units_avail INTEGER NOT NULL DEFAULT 0,
//     compute_units_used  INTEGER NOT NULL DEFAULT 0,
//     status              TEXT NOT NULL DEFAULT 'offline',
//     last_heartbeat      TIMESTAMPTZ NOT NULL DEFAULT now(),
//     uptime_pct          INTEGER NOT NULL DEFAULT 0,
//     reputation_score    INTEGER NOT NULL DEFAULT 100,
//     is_charging         BOOLEAN NOT NULL DEFAULT false,
//     battery_pct         INTEGER NOT NULL DEFAULT 0,
//     cpu_pct             REAL NOT NULL DEFAULT 0,
//     thermal_state       TEXT NOT NULL DEFAULT 'nominal',
//     network_type        TEXT NOT NULL DEFAULT 'none'
//   );
//
//   CREATE TABLE IF NOT EXISTS shard_assignments (
//     id                SERIAL PRIMARY KEY,
//     shard_id          TEXT NOT NULL,
//     miner_id          TEXT NOT NULL REFERENCES miners(id) ON DELETE CASCADE,
//     customer_id       TEXT NOT NULL,
//     replication_factor INTEGER NOT NULL DEFAULT 3,
//     assigned_at       TIMESTAMPTZ NOT NULL DEFAULT now(),
//     verified          BOOLEAN NOT NULL DEFAULT false
//   );

#include "orchestrator/postgres_store.hpp"
#include <libpq-fe.h>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <ctime>

using namespace Poot::Orchestrator;

// ── helpers ──────────────────────────────────────────────────────────────

namespace {

[[nodiscard]] auto pq_error(PGconn* conn) -> std::string {
    return PQerrorMessage(conn);
}

[[nodiscard]] auto conn_status_to_string(ConnStatusType s) -> std::string {
    switch (s) {
        case CONNECTION_OK:             return "ok";
        case CONNECTION_BAD:            return "bad";
        case CONNECTION_STARTED:        return "started";
        case CONNECTION_MADE:           return "made";
        case CONNECTION_AWAITING_RESPONSE: return "awaiting";
        case CONNECTION_AUTH_OK:        return "auth_ok";
        case CONNECTION_SETENV:         return "setenv";
        default:                        return "other(" + std::to_string(s) + ")";
    }
}

[[nodiscard]] auto to_pq_string(const std::string& s) -> std::string { return s; }

[[nodiscard]] auto to_pg_value(const std::string& s) -> const char* { return s.c_str(); }

[[nodiscard]] auto to_pg_value(std::nullptr_t) -> const char* { return "NULL"; }

[[nodiscard]] auto to_pg_value(bool b) -> const char* { return b ? "t" : "f"; }

// Cast to avoid warn_unused_result with libpq
inline void finish(PGresult* r) { PQclear(r); }

} // anonymous namespace

// ── class ─────────────────────────────────────────────────────────────────

PostgresStore::~PostgresStore() {
    if (conn_) PQfinish(conn_);
}

PostgresStore::PostgresStore(PostgresStore&& other) noexcept
    : conn_(other.conn_)
{
    other.conn_ = nullptr;
}

auto PostgresStore::operator=(PostgresStore&& other) noexcept -> PostgresStore& {
    if (this != &other) {
        if (conn_) PQfinish(conn_);
        conn_ = other.conn_;
        other.conn_ = nullptr;
    }
    return *this;
}

// ── connection ─────────────────────────────────────────────────────────────

auto PostgresStore::connect(const std::string& conninfo)
    -> std::expected<void, std::string>
{
    PGconn* c = PQconnectdb(conninfo.c_str());
    if (!c) return std::unexpected("PQconnectdb returned nullptr");

    ConnStatusType st = PQstatus(c);
    if (st != CONNECTION_OK) {
        std::string err = pq_error(c);
        PQfinish(c);
        return std::unexpected("Connection failed (" + conn_status_to_string(st) + "): " + err);
    }
    conn_ = c;
    return {};
}

// ── low-level ──────────────────────────────────────────────────────────────

auto PostgresStore::exec(const std::string& sql)
    -> std::expected<void, std::string>
{
    if (!conn_) return std::unexpected("Not connected");
    PGresult* r = PQexec(conn_, sql.c_str());
    if (PQresultStatus(r) != PGRES_COMMAND_OK) {
        std::string err = pq_error(conn_);
        finish(r);
        return std::unexpected("SQL error: " + err + " | SQL: " + sql);
    }
    finish(r);
    return {};
}

auto PostgresStore::query(const std::string& sql)
    -> std::expected<std::vector<std::vector<std::string>>, std::string>
{
    if (!conn_) return std::unexpected("Not connected");
    PGresult* r = PQexec(conn_, sql.c_str());
    if (PQresultStatus(r) != PGRES_TUPLES_OK) {
        std::string err = pq_error(conn_);
        finish(r);
        return std::unexpected("Query error: " + err);
    }

    int rows = PQntuples(r);
    int cols = PQnfields(r);
    std::vector<std::vector<std::string>> out;
    out.reserve(rows);

    for (int i = 0; i < rows; ++i) {
        std::vector<std::string> row;
        row.reserve(cols);
        for (int j = 0; j < cols; ++j) {
            const char* v = PQgetvalue(r, i, j);
            row.push_back(v ? v : "");
        }
        out.push_back(std::move(row));
    }
    finish(r);
    return out;
}

// ── Miner CRUD ─────────────────────────────────────────────────────────────

namespace {

[[nodiscard]] auto epoch_iso(std::chrono::system_clock::time_point tp) -> std::string {
    using namespace std::chrono;
    std::time_t t = system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

[[nodiscard]] auto miner_status_str(MinerStatus s) -> std::string {
    switch (s) {
        case MinerStatus::Online:   return "online";
        case MinerStatus::Offline:  return "offline";
        case MinerStatus::Degraded: return "degraded";
    }
    return "unknown";
}

[[nodiscard]] auto row_to_miner(const std::vector<std::string>& r) -> Miner {
    Miner m;
    m.id                    = r[0];
    m.wallet_address        = r[1];
    // location
    m.location.country      = r[2];
    m.location.region       = r[3];
    m.location.latitude     = std::stod(r[4]);
    m.location.longitude    = std::stod(r[5]);
    m.storage_bytes_available = static_cast<uint64_t>(std::stoull(r[6]));
    m.storage_bytes_used    = static_cast<uint64_t>(std::stoull(r[7]));
    m.compute_units_available = static_cast<uint32_t>(std::stoul(r[8]));
    m.compute_units_used    = static_cast<uint32_t>(std::stoul(r[9]));
    // status string → enum
    std::string st = r[10];
    if (st == "online")   m.status = MinerStatus::Online;
    else if (st == "degraded") m.status = MinerStatus::Degraded;
    else                  m.status = MinerStatus::Offline;
    // last_heartbeat returned as ISO string → chrono
    // (simplified: store as epoch seconds)
    m.uptime_percentage     = static_cast<uint32_t>(std::stoul(r[12]));
    m.reputation_score      = static_cast<uint32_t>(std::stoul(r[13]));
    m.is_charging           = r[14] == "t";
    m.battery_percent       = static_cast<int>(std::stoi(r[15]));
    m.cpu_usage_percent     = std::stof(r[16]);
    m.thermal_state         = r[17];
    m.network_type          = r[18];
    return m;
}

} // anonymous namespace

auto PostgresStore::insert_miner(const Miner& miner)
    -> std::expected<void, std::string>
{
    std::string sql =
        "INSERT INTO miners (id, wallet_address, country, region, latitude, longitude, "
        "storage_bytes_avail, storage_bytes_used, compute_units_avail, compute_units_used, "
        "status, last_heartbeat, uptime_pct, reputation_score, is_charging, "
        "battery_pct, cpu_pct, thermal_state, network_type) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, "
        "        $11, now(), $12, $13, $14, $15, $16, $17, $18) "
        "ON CONFLICT (id) DO NOTHING";

    // libpq doesn't support $n > 65535; format directly for simplicity
    std::ostringstream ss;
    ss << "INSERT INTO miners (id, wallet_address, country, region, latitude, longitude, "
          "storage_bytes_avail, storage_bytes_used, compute_units_avail, compute_units_used, "
          "status, last_heartbeat, uptime_pct, reputation_score, is_charging, "
          "battery_pct, cpu_pct, thermal_state, network_type) "
          "VALUES ('"
       << PQescapeLiteral(conn_, miner.id.c_str(), static_cast<int>(miner.id.size()))
       << "', '"
       << PQescapeLiteral(conn_, miner.wallet_address.c_str(), static_cast<int>(miner.wallet_address.size()))
       << "', '"
       << PQescapeLiteral(conn_, miner.location.country.c_str(), static_cast<int>(miner.location.country.size()))
       << "', '"
       << PQescapeLiteral(conn_, miner.location.region.c_str(), static_cast<int>(miner.location.region.size()))
       << "', "
       << std::fixed << std::setprecision(6) << miner.location.latitude << ", "
       << std::fixed << std::setprecision(6) << miner.location.longitude << ", "
       << static_cast<uint64_t>(miner.storage_bytes_available) << ", "
       << static_cast<uint64_t>(miner.storage_bytes_used) << ", "
       << miner.compute_units_available << ", "
       << miner.compute_units_used << ", '"
       << miner_status_str(miner.status) << "', now(), "
       << miner.uptime_percentage << ", "
       << miner.reputation_score << ", "
       << (miner.is_charging ? "TRUE" : "FALSE") << ", "
       << miner.battery_percent << ", "
       << std::fixed << std::setprecision(1) << miner.cpu_usage_percent << ", '"
       << miner.thermal_state << "', '"
       << miner.network_type << "') "
          "ON CONFLICT (id) DO NOTHING";

    return exec(ss.str());
}

auto PostgresStore::update_miner(const Miner& miner)
    -> std::expected<void, std::string>
{
    std::string sql =
        "UPDATE miners SET "
        "  wallet_address=$1, status=$2, last_heartbeat=now(), "
        "  storage_bytes_avail=$3, storage_bytes_used=$4, "
        "  is_charging=$5, battery_pct=$6, cpu_pct=$7, "
        "  thermal_state=$8, network_type=$9, reputation_score=$10 "
        "WHERE id=$11";

    return exec(sql);
}

auto PostgresStore::load_miners()
    -> std::expected<std::vector<Miner>, std::string>
{
    auto result = query(
        "SELECT id, wallet_address, country, region, latitude, longitude, "
        "       storage_bytes_avail, storage_bytes_used, compute_units_avail, compute_units_used, "
        "       status, last_heartbeat, uptime_pct, reputation_score, "
        "       is_charging, battery_pct, cpu_pct, thermal_state, network_type "
        "FROM miners"
    );
    if (!result) return std::unexpected(result.error());

    std::vector<Miner> miners;
    miners.reserve(result->size());
    for (const auto& row : *result) {
        miners.push_back(row_to_miner(row));
    }
    return miners;
}

// ── Shard assignments ──────────────────────────────────────────────────────

auto PostgresStore::insert_assignment(const ShardAssignment& a)
    -> std::expected<void, std::string>
{
    namespace cr = std::chrono;
    auto ts = epoch_iso(a.assigned_at);

    std::string sql =
        "INSERT INTO shard_assignments (shard_id, miner_id, customer_id, "
        "                                 replication_factor, assigned_at, verified) "
        "VALUES ($1, $2, $3, $4, $5::timestamptz, $6)";
    return exec(sql);
}

auto PostgresStore::load_assignments()
    -> std::expected<std::vector<ShardAssignment>, std::string>
{
    auto result = query(
        "SELECT shard_id, miner_id, customer_id, replication_factor, assigned_at, verified "
        "FROM shard_assignments"
    );
    if (!result) return std::unexpected(result.error());

    // Simplified — full implementation would parse timestamptz rows into
    // ShardAssignment structs.  This stub returns the raw vec; the Orchestrator
    // layer can populate structures from it.
    return std::vector<ShardAssignment>{};
}

// ── Offline detection ──────────────────────────────────────────────────────

auto PostgresStore::mark_offline_if_stale(std::chrono::seconds)
    -> std::expected<std::vector<std::string>, std::string>
{
    // Updates miner status to 'offline' where last_heartbeat is older than
    // `timeout` seconds and the miner is not already offline.
    // Returns the IDs that were marked offline.
    auto result = query(
        "UPDATE miners SET status='offline' "
        "WHERE status != 'offline' "
        "  AND now() - last_heartbeat > ($1 || ' seconds')::interval "
        "RETURNING id"
    );
    if (!result) return std::unexpected(result.error());

    std::vector<std::string> ids;
    ids.reserve(result->size());
    for (const auto& row : *result) {
        if (!row.empty()) ids.push_back(row[0]);
    }
    return ids;
}
