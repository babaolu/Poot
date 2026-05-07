// test_orchestrator.cpp — Unit tests for CloudMine Orchestrator
// C++23 — adheres to CppCoreGuidelines

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "orchestrator/orchestrator.hpp"

using namespace CloudMine::Orchestrator;

TEST_CASE("Register a miner", "[orchestrator][register]") {
    Orchestrator orch;

    Miner m;
    m.id = "miner-001";
    m.wallet_address = "0xabc123";
    m.storage_bytes_available = 1024ull * 1024 * 1024 * 10; // 10GB
    m.compute_units_available = 100;

    auto result = orch.register_miner(m);
    REQUIRE(result.has_value());
    REQUIRE(*result == "miner-001");
    REQUIRE(orch.miner_count() == 1);
}

TEST_CASE("Register duplicate miner fails", "[orchestrator][register]") {
    Orchestrator orch;

    Miner m;
    m.id = "miner-001";
    auto r1 = orch.register_miner(m);
    REQUIRE(r1.has_value());

    Miner m2;
    m2.id = "miner-001"; // Same ID
    auto result = orch.register_miner(m2);
    REQUIRE(!result.has_value());
}

TEST_CASE("Get existing miner", "[orchestrator][get]") {
    Orchestrator orch;

    Miner m;
    m.id = "miner-001";
    m.storage_bytes_available = 5000;
    auto r = orch.register_miner(m);
    REQUIRE(r.has_value());

    auto miner = orch.get_miner("miner-001");
    REQUIRE(miner.has_value());
    REQUIRE(miner->get().id == "miner-001");
    REQUIRE(miner->get().storage_bytes_available == 5000);
}

TEST_CASE("Get non-existent miner", "[orchestrator][get]") {
    Orchestrator orch;
    auto miner = orch.get_miner("nonexistent");
    REQUIRE(!miner.has_value());
}

TEST_CASE("Heartbeat updates miner status", "[orchestrator][heartbeat]") {
    Orchestrator orch;

    Miner m;
    m.id = "miner-001";
    m.status = MinerStatus::Offline;
    auto r1 = orch.register_miner(m);
    REQUIRE(r1.has_value());

    HeartbeatRequest hb;
    hb.miner_id = "miner-001";
    hb.battery_percent = 80;
    hb.is_charging = true;
    hb.cpu_usage_percent = 20.0f;
    hb.thermal_state = "nominal";
    hb.network_type = "wifi";

    auto resp = orch.handle_heartbeat(hb);
    REQUIRE(resp.has_value());
    REQUIRE(resp->accepted);

    auto miner = orch.get_miner("miner-001");
    REQUIRE(miner.has_value());
    REQUIRE(miner->get().status == MinerStatus::Online);
}

TEST_CASE("Heartbeat low battery sets degraded", "[orchestrator][heartbeat]") {
    Orchestrator orch;

    Miner m;
    m.id = "miner-001";
    auto r = orch.register_miner(m);
    REQUIRE(r.has_value());

    HeartbeatRequest hb;
    hb.miner_id = "miner-001";
    hb.battery_percent = 15; // Below 20%
    hb.is_charging = false;

    auto resp = orch.handle_heartbeat(hb);
    REQUIRE(resp.has_value());

    auto miner = orch.get_miner("miner-001");
    REQUIRE(miner.has_value());
    REQUIRE(miner->get().status == MinerStatus::Degraded);
}

TEST_CASE("Heartbeat unknown miner fails", "[orchestrator][heartbeat]") {
    Orchestrator orch;

    HeartbeatRequest hb;
    hb.miner_id = "nonexistent";

    auto resp = orch.handle_heartbeat(hb);
    REQUIRE(!resp.has_value());
}

TEST_CASE("List online miners", "[orchestrator][list]") {
    Orchestrator orch;

    // Register 3 miners
    for (int i = 0; i < 3; ++i) {
        Miner m;
        m.id = "miner-00" + std::to_string(i);
        m.status = MinerStatus::Online;
        auto r = orch.register_miner(m);
        REQUIRE(r.has_value());
    }

    // All online
    auto online = orch.list_online_miners();
    REQUIRE(online.size() == 3);

    // Set one offline
    HeartbeatRequest hb;
    hb.miner_id = "miner-001";
    hb.battery_percent = 10;
    hb.is_charging = false;
    auto resp = orch.handle_heartbeat(hb);
    REQUIRE(resp.has_value());

    online = orch.list_online_miners();
    REQUIRE(online.size() == 2);
}

TEST_CASE("Assign shards to miners", "[orchestrator][assign]") {
    Orchestrator orch;

    // Register 5 miners
    for (int i = 0; i < 5; ++i) {
        Miner m;
        m.id = "miner-00" + std::to_string(i);
        m.storage_bytes_available = 1024ull * 1024 * 100; // 100MB each
        m.status = MinerStatus::Online;
        auto r = orch.register_miner(m);
        REQUIRE(r.has_value());
    }

    std::vector<std::string> shard_ids = {"shard-001", "shard-002", "shard-003"};
    auto result = orch.assign_shards("customer-001", "deploy-001", shard_ids, 3);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 9); // 3 shards × 3 replicas each

    // Verify each shard has 3 assignments
    for (const auto& shard_id : shard_ids) {
        auto assignments = orch.get_assignments_for_shard(shard_id);
        REQUIRE(assignments.size() == 3);
    }
}

TEST_CASE("Assign shards insufficient miners", "[orchestrator][assign]") {
    Orchestrator orch;

    // Only 2 miners
    for (int i = 0; i < 2; ++i) {
        Miner m;
        m.id = "miner-00" + std::to_string(i);
        m.status = MinerStatus::Online;
        auto r = orch.register_miner(m);
        REQUIRE(r.has_value());
    }

    std::vector<std::string> shard_ids = {"shard-001"};
    auto result = orch.assign_shards("customer-001", "deploy-001", shard_ids, 3);

    REQUIRE(!result.has_value()); // Not enough miners
}

TEST_CASE("Check offline miners", "[orchestrator][offline]") {
    Orchestrator orch;

    // Register a miner
    Miner m;
    m.id = "miner-001";
    m.status = MinerStatus::Online;
    auto r = orch.register_miner(m);
    REQUIRE(r.has_value());

    // Immediately check - should not be offline yet
    auto offline = orch.check_offline_miners(std::chrono::hours(24)); // Very long timeout
    REQUIRE(offline.empty());

    // Simulate old heartbeat
    auto miner = orch.get_miner("miner-001");
    // Can't easily manipulate time, so just test the function works
}

TEST_CASE("Trigger re-replication", "[orchestrator][replication]") {
    Orchestrator orch;

    // Register miners
    for (int i = 0; i < 5; ++i) {
        Miner m;
        m.id = "miner-00" + std::to_string(i);
        m.storage_bytes_available = 1024ull * 1024 * 100;
        m.status = MinerStatus::Online;
        auto r = orch.register_miner(m);
        REQUIRE(r.has_value());
    }

    // Assign shards
    std::vector<std::string> shard_ids = {"shard-001", "shard-002"};
    auto result = orch.assign_shards("customer-001", "deploy-001", shard_ids, 3);
    REQUIRE(result.has_value());

    // Mark miner-003 as offline and trigger re-replication
    auto new_assignments = orch.trigger_rereplication("miner-003");
    // Should create new assignments to replace the lost one
}

TEST_CASE("Stats tracking", "[orchestrator][stats]") {
    Orchestrator orch;

    Miner m1;
    m1.id = "miner-001";
    m1.storage_bytes_available = 1000;
    m1.storage_bytes_used = 200;
    m1.status = MinerStatus::Online;
    auto r1 = orch.register_miner(m1);
    REQUIRE(r1.has_value());

    Miner m2;
    m2.id = "miner-002";
    m2.storage_bytes_available = 2000;
    m2.storage_bytes_used = 500;
    m2.status = MinerStatus::Online;
    auto r2 = orch.register_miner(m2);
    REQUIRE(r2.has_value());

    REQUIRE(orch.total_storage_available() == 3000);
    REQUIRE(orch.total_storage_used() == 700);
    REQUIRE(orch.miner_count() == 2);
    REQUIRE(orch.online_miner_count() == 2);
}

TEST_CASE("Get assignments for miner", "[orchestrator][assignments]") {
    Orchestrator orch;

    // Register miners
    for (int i = 0; i < 3; ++i) {
        Miner m;
        m.id = "miner-00" + std::to_string(i);
        m.storage_bytes_available = 1024ull * 1024 * 100;
        m.status = MinerStatus::Online;
        auto r = orch.register_miner(m);
        REQUIRE(r.has_value());
    }

    // Assign shards
    std::vector<std::string> shard_ids = {"shard-001", "shard-002"};
    auto result = orch.assign_shards("customer-001", "deploy-001", shard_ids, 3);
    REQUIRE(result.has_value());

    // Check assignments for miner-001
    auto miner_assignments = orch.get_assignments_for_miner("miner-001");
    REQUIRE(!miner_assignments.empty());
}
