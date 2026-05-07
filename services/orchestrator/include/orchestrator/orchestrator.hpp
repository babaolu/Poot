// orchestrator.hpp — Core orchestrator logic for Poot
// C++23 — adheres to CppCoreGuidelines

#pragma once

#include "models.hpp"
#include <algorithm>
#include <chrono>
#include <expected>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace Poot::Orchestrator {

class Orchestrator {
public:
    using Clock = std::chrono::system_clock;
    using TimePoint = Clock::time_point;

    Orchestrator() : rng_(std::random_device{}()) {}

    // --- Miner registry ---

    [[nodiscard]] auto register_miner(const Miner& miner) -> std::expected<std::string, std::string> {
        if (miner.id.empty()) {
            return std::unexpected("Miner ID cannot be empty");
        }
        if (miners_.contains(miner.id)) {
            return std::unexpected("Miner already registered: " + miner.id);
        }
        Miner m = miner;
        m.last_heartbeat = Clock::now(); // Initialize heartbeat time
        miners_[m.id] = m;
        return m.id;
    }

    [[nodiscard]] auto get_miner(const std::string& id) const
        -> std::optional<std::reference_wrapper<const Miner>> {
        auto it = miners_.find(id);
        if (it == miners_.end()) return std::nullopt;
        return std::cref(it->second);
    }

    [[nodiscard]] auto list_miners() const -> std::vector<std::string> {
        std::vector<std::string> ids;
        ids.reserve(miners_.size());
        for (const auto& [id, _] : miners_) {
            ids.push_back(id);
        }
        return ids;
    }

    [[nodiscard]] auto list_online_miners() const -> std::vector<std::string> {
        std::vector<std::string> ids;
        for (const auto& [id, miner] : miners_) {
            if (miner.status == MinerStatus::Online) {
                ids.push_back(id);
            }
        }
        return ids;
    }

    // --- Heartbeat ---

    [[nodiscard]] auto handle_heartbeat(const HeartbeatRequest& req)
        -> std::expected<HeartbeatResponse, std::string> {
        auto it = miners_.find(req.miner_id);
        if (it == miners_.end()) {
            return std::unexpected("Unknown miner: " + req.miner_id);
        }

        auto& miner = it->second;
        miner.last_heartbeat = Clock::now();
        miner.storage_bytes_available = req.storage_bytes_available;
        miner.storage_bytes_used = req.storage_bytes_used;
        miner.is_charging = req.is_charging;
        miner.battery_percent = req.battery_percent;
        miner.cpu_usage_percent = req.cpu_usage_percent;
        miner.thermal_state = req.thermal_state;
        miner.network_type = req.network_type;

        // Update status based on resource state
        if (req.battery_percent < 20 && !req.is_charging) {
            miner.status = MinerStatus::Degraded;
        } else if (req.thermal_state == "critical" || req.cpu_usage_percent > 80.0f) {
            miner.status = MinerStatus::Degraded;
        } else {
            miner.status = MinerStatus::Online;
        }

        // Check for pending shard assignments
        HeartbeatResponse resp;
        resp.accepted = true;
        auto pending = pending_shards_.extract(req.miner_id);
        if (!pending.empty()) {
            resp.shard_tasks.assign(pending.mapped().begin(), pending.mapped().end());
        }

        return resp;
    }

    // --- Shard assignment ---

    [[nodiscard]] auto assign_shards(
        const std::string& customer_id,
        const std::string& deployment_id,
        const std::vector<std::string>& shard_ids,
        size_t replication_factor = 3)
        -> std::expected<std::vector<ShardAssignment>, std::string> {
        if (shard_ids.empty()) {
            return std::unexpected("No shards to assign");
        }

        auto online = list_online_miners();
        if (online.size() < replication_factor) {
            return std::unexpected("Not enough online miners. Need " +
                                   std::to_string(replication_factor) + ", have " +
                                   std::to_string(online.size()));
        }

        std::vector<ShardAssignment> assignments;
        assignments.reserve(shard_ids.size() * replication_factor);

        for (const auto& shard_id : shard_ids) {
            // Select miners for this shard (round-robin + capacity filter)
            std::vector<std::string> selected;
            select_miners(online, replication_factor, selected);

            for (const auto& miner_id : selected) {
                ShardAssignment a;
                a.shard_id = shard_id;
                a.miner_id = miner_id;
                a.customer_id = customer_id;
                a.replication_factor = replication_factor;
                a.assigned_at = Clock::now();
                a.verified = false;

                assignments.push_back(a);
                pending_shards_[miner_id].push_back(shard_id);

                // Update miner storage tracking
                auto it = miners_.find(miner_id);
                if (it != miners_.end()) {
                    // Assume ~1MB per shard for now
                    it->second.storage_bytes_used += 1024 * 1024;
                    it->second.storage_bytes_available -= 1024 * 1024;
                }
            }
        }

        // Store assignments
        for (const auto& a : assignments) {
            shard_assignments_[a.shard_id].push_back(a);
        }

        return assignments;
    }

    [[nodiscard]] auto get_assignments_for_shard(const std::string& shard_id) const
        -> std::vector<ShardAssignment> {
        auto it = shard_assignments_.find(shard_id);
        if (it == shard_assignments_.end()) return {};
        return it->second;
    }

    [[nodiscard]] auto get_assignments_for_miner(const std::string& miner_id) const
        -> std::vector<ShardAssignment> {
        std::vector<ShardAssignment> result;
        for (const auto& [shard_id, assignments] : shard_assignments_) {
            for (const auto& a : assignments) {
                if (a.miner_id == miner_id) {
                    result.push_back(a);
                }
            }
        }
        return result;
    }

    // --- Re-replication ---

    [[nodiscard]] auto check_offline_miners(std::chrono::seconds timeout = std::chrono::seconds(120))
        -> std::vector<std::string> {
        auto now = Clock::now();
        std::vector<std::string> offline;

        for (auto& [id, miner] : miners_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - miner.last_heartbeat).count();
            if (elapsed > timeout.count()) {
                miner.status = MinerStatus::Offline;
                offline.push_back(id);
            }
        }

        return offline;
    }

    [[nodiscard]] auto trigger_rereplication(const std::string& offline_miner_id)
        -> std::expected<std::vector<ShardAssignment>, std::string> {
        auto it = miners_.find(offline_miner_id);
        if (it == miners_.end()) {
            return std::unexpected("Unknown miner: " + offline_miner_id);
        }
        it->second.status = MinerStatus::Offline;

        // Find all shards stored by this miner
        std::vector<ShardAssignment> lost_assignments;
        for (auto& [shard_id, assignments] : shard_assignments_) {
            std::erase_if(assignments, [&](const ShardAssignment& a) {
                return a.miner_id == offline_miner_id;
            });
            // Check if we still have enough replicas
            if (assignments.empty()) {
                // All replicas lost — critical!
                ShardAssignment lost;
                lost.shard_id = shard_id;
                lost.miner_id = offline_miner_id;
                lost.customer_id = "(unknown)";
                lost.replication_factor = 0;
                lost.assigned_at = Clock::now();
                lost.verified = false;
                lost_assignments.push_back(lost);
            }
        }

        if (lost_assignments.empty()) {
            return std::vector<ShardAssignment>{}; // Nothing to re-replicate
        }

        // Re-assign lost shards to new miners
        std::vector<std::string> shard_ids;
        for (const auto& a : lost_assignments) {
            shard_ids.push_back(a.shard_id);
        }

        // For simplicity, use the first customer_id found
        std::string customer_id = lost_assignments[0].customer_id;
        return assign_shards(customer_id, "repl-" + offline_miner_id, shard_ids, 3);
    }

    // --- Stats ---

    [[nodiscard]] auto total_storage_available() const -> uint64_t {
        uint64_t total = 0;
        for (const auto& [_, m] : miners_) {
            if (m.status == MinerStatus::Online) {
                total += m.storage_bytes_available;
            }
        }
        return total;
    }

    [[nodiscard]] auto total_storage_used() const -> uint64_t {
        uint64_t total = 0;
        for (const auto& [_, m] : miners_) {
            total += m.storage_bytes_used;
        }
        return total;
    }

    [[nodiscard]] auto miner_count() const -> size_t {
        return miners_.size();
    }

    [[nodiscard]] auto online_miner_count() const -> size_t {
        return list_online_miners().size();
    }

private:
    // Select miners based on capacity and reputation (simplified round-robin)
    void select_miners(const std::vector<std::string>& online,
                       size_t count,
                       std::vector<std::string>& out) {
        if (online.empty() || count == 0) return;

        // Shuffle for basic distribution
        std::vector<std::string> candidates = online;
        std::shuffle(candidates.begin(), candidates.end(), rng_);

        // Filter by capacity (must have at least 1MB free)
        std::erase_if(candidates, [&](const std::string& id) {
            auto it = miners_.find(id);
            if (it == miners_.end()) return true;
            return it->second.storage_bytes_available < 1024 * 1024;
        });

        // Sort by reputation (higher is better)
        std::sort(candidates.begin(), candidates.end(), [&](const std::string& a, const std::string& b) {
            return miners_.at(a).reputation_score > miners_.at(b).reputation_score;
        });

        for (size_t i = 0; i < count && i < candidates.size(); ++i) {
            out.push_back(candidates[i]);
        }
    }

    std::map<std::string, Miner> miners_;
    std::map<std::string, std::vector<ShardAssignment>> shard_assignments_;
    std::map<std::string, std::vector<std::string>> pending_shards_; // miner_id -> shard_ids
    std::mt19937 rng_;
};

} // namespace Poot::Orchestrator
