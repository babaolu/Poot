// test_reed_solomon.cpp — Unit tests for GF(2^8) and Reed-Solomon codec
// C++23 — adheres to CppCoreGuidelines

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

#include "shard_engine/reed_solomon.hpp"

using namespace Poot::ShardEngine;

TEST_CASE("GF256 multiply is commutative", "[gf256]") {
    for (int a = 1; a < 256; ++a) {
        for (int b = 1; b < 256; ++b) {
            REQUIRE(GF256::mul(static_cast<uint8_t>(a), static_cast<uint8_t>(b)) ==
                    GF256::mul(static_cast<uint8_t>(b), static_cast<uint8_t>(a)));
        }
    }
}

TEST_CASE("GF256 multiply by 1 is identity", "[gf256]") {
    for (int a = 0; a < 256; ++a) {
        uint8_t val = static_cast<uint8_t>(a);
        REQUIRE(GF256::mul(val, 1) == val);
        REQUIRE(GF256::mul(1, val) == val);
    }
}

TEST_CASE("GF256 invert: a * inv(a) == 1", "[gf256]") {
    for (int a = 1; a < 256; ++a) {
        uint8_t val = static_cast<uint8_t>(a);
        uint8_t inv = GF256::inv(val);
        REQUIRE(GF256::mul(val, inv) == 1);
    }
}

TEST_CASE("GF256 pow produces expected values", "[gf256]") {
    // x^0 = 1
    REQUIRE(GF256::pow(2, 0) == 1);
    // x^1 = 2 (primitive element)
    REQUIRE(GF256::pow(2, 1) == 2);
    // x^2 = 4
    REQUIRE(GF256::pow(2, 2) == 4);
    // x^7 = 2^7 = 128 (00000010 in GF = 128)
    // Actually x^7 = x^7 = 128 (since x = 2 = 00000010, x^7 = 10000000 = 128)
    REQUIRE(GF256::pow(2, 7) == 128);

    // x^255 = 1 (order of primitive element)
    REQUIRE(GF256::pow(2, 255) == 1);
}

TEST_CASE("Reed-Solomon encode produces deterministic parity", "[reed-solomon]") {
    ReedSolomon rs;

    // Create 6 data shards of 100 bytes each
    std::vector<std::vector<uint8_t>> data(6, std::vector<uint8_t>(100, 0));
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 100; ++j) {
            data[i][j] = static_cast<uint8_t>(i * 100 + j);
        }
    }

    auto parity_or = rs.encode(data, 6, 3);
    REQUIRE(parity_or.has_value());
    auto& parity = *parity_or;
    REQUIRE(parity.size() == 3);
    REQUIRE(parity[0].size() == 100);
    REQUIRE(parity[1].size() == 100);
    REQUIRE(parity[2].size() == 100);

    // Encode again — should be deterministic
    auto parity2_or = rs.encode(data, 6, 3);
    REQUIRE(parity2_or.has_value());
    for (size_t i = 0; i < 3; ++i) {
        REQUIRE(parity[i] == (*parity2_or)[i]);
    }
}

TEST_CASE("Reed-Solomon decode with exact data_shards", "[reed-solomon]") {
    ReedSolomon rs;

    // Create 6 data shards
    std::vector<std::vector<uint8_t>> data(6, std::vector<uint8_t>(50, 0));
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 50; ++j) {
            data[i][j] = static_cast<uint8_t>((i * 50 + j) * 7 + 13);
        }
    }

    // Encode to get 9 shards (6 data + 3 parity)
    auto parity_or = rs.encode(data, 6, 3);
    REQUIRE(parity_or.has_value());

    // Combine: all 9 shards
    std::vector<std::vector<uint8_t>> all = data;
    all.insert(all.end(), parity_or->begin(), parity_or->end());

    // Decode using first 6 shards (indices 0..5 — the data shards themselves)
    std::vector<size_t> indices = {0, 1, 2, 3, 4, 5};
    auto subset = std::vector<std::vector<uint8_t>>(all.begin(), all.begin() + 6);
    auto decoded_or = rs.decode(subset, indices, 6);
    REQUIRE(decoded_or.has_value());

    // Check decoded matches original data
    REQUIRE(decoded_or->size() == 6);
    for (size_t i = 0; i < 6; ++i) {
        REQUIRE((*decoded_or)[i] == data[i]);
    }
}

TEST_CASE("Reed-Solomon decode with mixed shard indices", "[reed-solomon]") {
    ReedSolomon rs;

    std::vector<std::vector<uint8_t>> data(6, std::vector<uint8_t>(50, 0));
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 50; ++j) {
            data[i][j] = static_cast<uint8_t>((i * 50 + j) * 3 + 7);
        }
    }

    auto parity_or = rs.encode(data, 6, 3);
    REQUIRE(parity_or.has_value());

    // All 9 shards
    std::vector<std::vector<uint8_t>> all = data;
    all.insert(all.end(), parity_or->begin(), parity_or->end());

    // Decode using shards at indices 0, 2, 4, 5, 6, 8 (mixed data + parity)
    std::vector<std::vector<uint8_t>> received;
    std::vector<size_t> indices;
    for (size_t idx : {0, 2, 4, 5, 6, 8}) {
        received.push_back(all[idx]);
        indices.push_back(idx);
    }

    auto decoded_or = rs.decode(received, indices, 6);
    REQUIRE(decoded_or.has_value());
    REQUIRE(decoded_or->size() == 6);

    for (size_t i = 0; i < 6; ++i) {
        REQUIRE((*decoded_or)[i] == data[i]);
    }
}

TEST_CASE("Reed-Solomon decode with different subsets gives same result", "[reed-solomon]") {
    ReedSolomon rs;

    std::vector<std::vector<uint8_t>> data(6, std::vector<uint8_t>(50, 0));
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 50; ++j) {
            data[i][j] = static_cast<uint8_t>((i * 50 + j) * 11 + 3);
        }
    }

    auto parity_or = rs.encode(data, 6, 3);
    std::vector<std::vector<uint8_t>> all = data;
    all.insert(all.end(), parity_or->begin(), parity_or->end());

    // Subset A: indices 0, 1, 2, 3, 4, 5 (first 6)
    std::vector<size_t> idx_a = {0, 1, 2, 3, 4, 5};
    auto rec_a_or = rs.decode(
        std::vector<std::vector<uint8_t>>(all.begin(), all.begin() + 6), idx_a, 6);
    REQUIRE(rec_a_or.has_value());

    // Subset B: indices 0, 2, 4, 6, 7, 8 (mixed)
    std::vector<std::vector<uint8_t>> rec_b_data;
    std::vector<size_t> idx_b;
    for (size_t idx : {0, 2, 4, 6, 7, 8}) {
        rec_b_data.push_back(all[idx]);
        idx_b.push_back(idx);
    }
    auto rec_b_or = rs.decode(rec_b_data, idx_b, 6);
    REQUIRE(rec_b_or.has_value());

    // Both should give same result
    for (size_t i = 0; i < 6; ++i) {
        REQUIRE((*rec_a_or)[i] == (*rec_b_or)[i]);
    }
}

TEST_CASE("Reed-Solomon fails with too few shards", "[reed-solomon]") {
    ReedSolomon rs;

    std::vector<std::vector<uint8_t>> data(6, std::vector<uint8_t>(50, 0));
    for (size_t i = 0; i < 6; ++i) {
        for (size_t j = 0; j < 50; ++j) {
            data[i][j] = static_cast<uint8_t>(i * 50 + j);
        }
    }

    auto parity_or = rs.encode(data, 6, 3);
    std::vector<std::vector<uint8_t>> all = data;
    all.insert(all.end(), parity_or->begin(), parity_or->end());

    // Only 5 shards — should fail
    std::vector<std::vector<uint8_t>> five;
    std::vector<size_t> five_idx = {0, 1, 2, 3, 4};
    for (size_t idx : five_idx) five.push_back(all[idx]);

    auto result = rs.decode(five, five_idx, 6);
    REQUIRE(!result.has_value()); // Should fail
}
