// models.hpp — Data models for Poot Orchestrator
// C++23 — adheres to CppCoreGuidelines

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace Poot::Orchestrator {

// Miner status
enum class MinerStatus {
    Online,
    Offline,
    Degraded  // High load, low battery, etc.
};

// Geographic region (simplified)
struct GeoLocation {
    std::string country;
    std::string region;
    double latitude = 0.0;
    double longitude = 0.0;
};

// Miner node representation
struct Miner {
    std::string id;                          // Unique miner ID (UUID or derived from wallet)
    std::string wallet_address;                // For token payouts
    GeoLocation location;

    // Capacity
    uint64_t storage_bytes_available = 0;
    uint64_t storage_bytes_used = 0;
    uint32_t compute_units_available = 0;     // Abstract compute capacity
    uint32_t compute_units_used = 0;

    // Status
    MinerStatus status = MinerStatus::Offline;
    std::chrono::system_clock::time_point last_heartbeat;
    uint32_t uptime_percentage = 0;           // Rolling window
    uint32_t reputation_score = 100;           // 0-100, decreases on failures

    // Resource state (reported by miner)
    bool is_charging = false;
    int battery_percent = 0;
    float cpu_usage_percent = 0.0f;
    std::string thermal_state;                   // "nominal", "fair", "serious", "critical"
    std::string network_type;                   // "wifi", "cellular", "none"
};

// Shard assignment to a miner
struct ShardAssignment {
    std::string shard_id;         // References a shard in the system
    std::string miner_id;         // Which miner stores it
    std::string customer_id;       // Which customer owns it
    size_t replication_factor = 3; // How many copies should exist
    std::chrono::system_clock::time_point assigned_at;
    bool verified = false;          // Passed latest proof challenge
};

// Heartbeat request from miner
struct HeartbeatRequest {
    std::string miner_id;
    uint64_t storage_bytes_available = 0;
    uint64_t storage_bytes_used = 0;
    bool is_charging = false;
    int battery_percent = 0;
    float cpu_usage_percent = 0.0f;
    std::string thermal_state;
    std::string network_type;
};

// Heartbeat response to miner
struct HeartbeatResponse {
    bool accepted = true;
    std::string message;
    std::vector<std::string> shard_tasks;  // New shards to store
};

// Customer instance deployment request
struct DeploymentRequest {
    std::string customer_id;
    std::string instance_type;      // "static-site", "nodejs", "docker", "object-storage"
    uint64_t storage_bytes_needed = 0;
    uint32_t compute_units_needed = 0;
    std::string deployment_manifest; // JSON or SDL-format manifest
};

// Deployment result
struct DeploymentResult {
    std::string deployment_id;
    std::vector<ShardAssignment> shard_assignments;
    std::string edge_url;           // Where the instance is accessible
    bool success = false;
    std::string error_message;
};

} // namespace Poot::Orchestrator
