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
    REQUIRE(manifest.shard_ivs_b64.size() == 9);
    REQUIRE(!manifest.content_hash_b64.empty());
    REQUIRE(!manifest.wrapped_key_b64.empty());
    REQUIRE(!manifest.key_iv_b64.empty());
    REQUIRE(!manifest.key_tag_b64.empty());
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
    REQUIRE(parsed->shard_ivs_b64.size() == manifest.shard_ivs_b64.size());
    REQUIRE(parsed->wrapped_key_b64 == manifest.wrapped_key_b64);
    REQUIRE(parsed->key_iv_b64 == manifest.key_iv_b64);
    REQUIRE(parsed->key_tag_b64 == manifest.key_tag_b64);
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

// ── Security Property Tests (FR-TST-3, FR-SHD-3, FR-SHD-4, SEC-1..4) ────────

TEST_CASE("Security: Encrypting identical plaintext twice produces different ciphertext and IVs", "[shard][security][FR-TST-3a]") {
    std::vector<uint8_t> data(2000, 0x5A);

    auto r1 = split_and_encrypt(data, 6, 3);
    auto r2 = split_and_encrypt(data, 6, 3);
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());

    auto& [shards1, manifest1] = *r1;
    auto& [shards2, manifest2] = *r2;

    // Content hashes must match (content addressable)
    REQUIRE(manifest1.content_hash_b64 == manifest2.content_hash_b64);

    // Wrapped DEKs must be different (random DEK per encryption)
    REQUIRE(manifest1.wrapped_key_b64 != manifest2.wrapped_key_b64);
    REQUIRE(manifest1.key_iv_b64 != manifest2.key_iv_b64);

    // Every shard ciphertext and IV must be different
    for (size_t i = 0; i < shards1.size(); ++i) {
        REQUIRE(shards1[i].iv != shards2[i].iv);
        REQUIRE(shards1[i].data != shards2[i].data);
    }
}

TEST_CASE("Security: No two shards of the same object ever share an IV", "[shard][security][FR-TST-3b][FR-SHD-3]") {
    std::vector<uint8_t> data(5000, 0x3C);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;
    REQUIRE(shards.size() == 9);

    // Verify all 9 shard IVs are distinct
    std::vector<std::vector<uint8_t>> seen_ivs;
    for (size_t i = 0; i < shards.size(); ++i) {
        REQUIRE(shards[i].iv.size() == 12);
        for (const auto& prev_iv : seen_ivs) {
            REQUIRE(shards[i].iv != prev_iv);
        }
        seen_ivs.push_back(shards[i].iv);
    }

    // Verify manifest has 9 distinct base64 IVs
    std::vector<std::string> seen_b64_ivs;
    for (const auto& iv_b64 : manifest.shard_ivs_b64) {
        for (const auto& prev : seen_b64_ivs) {
            REQUIRE(iv_b64 != prev);
        }
        seen_b64_ivs.push_back(iv_b64);
    }
}

TEST_CASE("Security: Tampered shard fails authentication and reconstruction fails", "[shard][security][FR-TST-3c][SEC-4]") {
    std::vector<uint8_t> data(1000, 0x7E);

    auto result = split_and_encrypt(data, 6, 3);
    REQUIRE(result.has_value());

    auto [shards, manifest] = *result;

    // Tamper with shard 0 ciphertext
    shards[0].data[0] ^= 0xFF;

    // Reconstructing using 6 shards including the tampered one must fail
    std::vector<Shard> subset(shards.begin(), shards.begin() + 6);
    auto reconstructed = reconstruct_and_verify(subset, manifest);
    REQUIRE(!reconstructed.has_value());
}

TEST_CASE("Security: Envelope encryption with custom customer KEK", "[shard][security][FR-SHD-4][SEC-3]") {
    std::vector<uint8_t> data(1500, 0x2B);
    std::vector<uint8_t> customer_kek(32, 0);
    for (size_t i = 0; i < 32; ++i) customer_kek[i] = static_cast<uint8_t>(i * 13 + 7);

    // Split & encrypt with customer KEK
    auto result = split_and_encrypt(data, 6, 3, customer_kek);
    REQUIRE(result.has_value());

    auto& [shards, manifest] = *result;

    // Reconstruct with correct customer KEK succeeds
    std::vector<Shard> subset(shards.begin(), shards.begin() + 6);
    auto recovered = reconstruct_and_verify(subset, manifest, customer_kek);
    REQUIRE(recovered.has_value());
    REQUIRE(*recovered == data);

    // Reconstruct with wrong customer KEK fails
    std::vector<uint8_t> wrong_kek = customer_kek;
    wrong_kek[0] ^= 0x01;
    auto fail_reconstruct = reconstruct_and_verify(subset, manifest, wrong_kek);
    REQUIRE(!fail_reconstruct.has_value());
}
