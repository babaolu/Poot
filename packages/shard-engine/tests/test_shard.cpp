// test_shard.cpp — Integration tests for Shard Engine
// C++23 — adheres to CppCoreGuidelines

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>

#include "shard_engine/shard.hpp"

using namespace Poot::ShardEngine;

TEST_CASE("split_and_encrypt produces correct number of shards", "[shard][integration]") {
    std::vector<uint8_t> data(1000, 0);
    for (size_t i = 0; i < 1000; ++i) data[i] = static_cast<uint8_t>(i % 256);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;
    REQUIRE(shards.size() == 9); // 6 data + 3 parity
    REQUIRE(manifest.data_shards == 6);
    REQUIRE(manifest.parity_shards == 3);
    REQUIRE(manifest.original_size == 1000);
    REQUIRE(manifest.shard_size > 0);
    REQUIRE(manifest.shard_hashes_b64.size() == 9);
    REQUIRE(!manifest.content_hash_b64.empty());
    REQUIRE(!manifest.encryption_iv_b64.empty());
}

TEST_CASE("reconstruct_and_verify with first 6 shards", "[shard][integration]") {
    std::vector<uint8_t> data(1000, 0);
    for (size_t i = 0; i < 1000; ++i) data[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    // Use first 6 shards (indices 0..5)
    std::vector<Shard> subset(shards.begin(), shards.begin() + 6);

    auto reconstructed = reconstruct_and_verify(subset, manifest);
    REQUIRE(reconstructed.has_value());
    REQUIRE(*reconstructed == data);
}

TEST_CASE("reconstruct_and_verify with different 6-shard subset", "[shard][integration]") {
    std::vector<uint8_t> data(1000, 0);
    for (size_t i = 0; i < 1000; ++i) data[i] = static_cast<uint8_t>((i * 3 + 7) & 0xFF);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    // Use shards at indices 0, 2, 4, 5, 6, 8 (mixed data + parity)
    std::vector<Shard> subset;
    for (size_t idx : {0, 2, 4, 5, 6, 8}) {
        subset.push_back(shards[idx]);
    }

    auto reconstructed = reconstruct_and_verify(subset, manifest);
    REQUIRE(reconstructed.has_value());
    REQUIRE(*reconstructed == data);
}

TEST_CASE("reconstruct fails with only 5 shards", "[shard][integration]") {
    std::vector<uint8_t> data(1000, 0x42);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    // Only 5 shards — should fail
    std::vector<Shard> subset(shards.begin(), shards.begin() + 5);

    auto reconstructed = reconstruct_and_verify(subset, manifest);
    REQUIRE(!reconstructed.has_value());
}

TEST_CASE("Kill 3 random shards, reconstruct succeeds", "[shard][integration]") {
    std::vector<uint8_t> data(1000, 0);
    for (size_t i = 0; i < 1000; ++i) data[i] = static_cast<uint8_t>((i * 11 + 29) & 0xFF);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    // Simulate killing shards 1, 3, 7 — use the remaining 6
    std::vector<Shard> remaining;
    for (size_t i = 0; i < 9; ++i) {
        if (i == 1 || i == 3 || i == 7) continue;
        remaining.push_back(shards[i]);
    }
    REQUIRE(remaining.size() == 6);

    auto reconstructed = reconstruct_and_verify(remaining, manifest);
    REQUIRE(reconstructed.has_value());
    REQUIRE(*reconstructed == data);
}

TEST_CASE("10MB data split and reconstruct under 5 seconds", "[shard][performance]") {
    // Note: actual timing depends on hardware
    std::vector<uint8_t> data(10 * 1024 * 1024, 0); // 10MB
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }

    auto start = std::chrono::high_resolution_clock::now();

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    // Kill 3 random shards (e.g., 2, 4, 7)
    std::vector<Shard> remaining;
    for (size_t i = 0; i < 9; ++i) {
        if (i == 2 || i == 4 || i == 7) continue;
        remaining.push_back(shards[i]);
    }

    auto reconstructed = reconstruct_and_verify(remaining, manifest);
    REQUIRE(reconstructed.has_value());

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(*reconstructed == data);
    // Per Phase 1 definition: should be under 5 seconds for 10MB on local
    REQUIRE(duration.count() < 5000);
}

TEST_CASE("Manifest JSON serialization round-trip", "[shard][manifest]") {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    // Serialize to JSON
    std::string json = manifest.to_json();
    REQUIRE(!json.empty());

    // Deserialize
    auto parsed = Manifest::from_json(json);
    REQUIRE(parsed.has_value());

    // Verify key fields match
    REQUIRE(parsed->content_hash_b64 == manifest.content_hash_b64);
    REQUIRE(parsed->original_size == manifest.original_size);
    REQUIRE(parsed->shard_size == manifest.shard_size);
    REQUIRE(parsed->data_shards == manifest.data_shards);
    REQUIRE(parsed->parity_shards == manifest.parity_shards);
    REQUIRE(parsed->shard_hashes_b64.size() == manifest.shard_hashes_b64.size());
}

TEST_CASE("Shard IV and tag are correct sizes", "[shard][integration]") {
    std::vector<uint8_t> data(500, 0x99);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    for (const auto& shard : shards) {
        REQUIRE(shard.iv.size() == 12);  // 12-byte IV for GCM
        REQUIRE(shard.tag.size() == 16); // 16-byte auth tag
    }
}
