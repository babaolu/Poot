// test_merkle.cpp — Unit tests for Merkle tree
// C++23 — adheres to CppCoreGuidelines

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <string>

#include "shard_engine/merkle.hpp"

using namespace Poot::ShardEngine::Merkle;

TEST_CASE("Merkle tree with 1 shard", "[merkle]") {
    std::vector<std::vector<uint8_t>> shards = {
        {1, 2, 3, 4, 5}
    };

    MerkleTree tree(shards);
    auto root = tree.root();
    REQUIRE(root.size() == 32); // SHA-256 hash
}

TEST_CASE("Merkle tree with 9 shards", "[merkle]") {
    std::vector<std::vector<uint8_t>> shards(9);
    for (size_t i = 0; i < 9; ++i) {
        shards[i].resize(100, 0);
        for (size_t j = 0; j < 100; ++j) {
            shards[i][j] = static_cast<uint8_t>(i * 100 + j);
        }
    }

    MerkleTree tree(shards);
    auto root = tree.root();
    REQUIRE(root.size() == 32);

    // Each leaf should be different
    for (size_t i = 0; i < 9; ++i) {
        REQUIRE(tree.leaf(i).size() == 32);
    }
}

TEST_CASE("Merkle proof verifies correctly", "[merkle]") {
    std::vector<std::vector<uint8_t>> shards(9);
    for (size_t i = 0; i < 9; ++i) {
        shards[i].resize(50, static_cast<uint8_t>(i + 1));
    }

    MerkleTree tree(shards);
    auto root = tree.root();

    // Get proof for shard 0
    auto proof = tree.get_proof(0);
    REQUIRE(!proof.empty());

    // Verify proof
    REQUIRE(MerkleTree::verify_proof(tree.leaf(0), proof, root));
}

TEST_CASE("Merkle proof for different shards", "[merkle]") {
    std::vector<std::vector<uint8_t>> shards(9);
    for (size_t i = 0; i < 9; ++i) {
        shards[i].resize(50, static_cast<uint8_t>(i * 7 + 3));
    }

    MerkleTree tree(shards);
    auto root = tree.root();

    // Verify proof for shard 4
    auto proof4 = tree.get_proof(4);
    REQUIRE(MerkleTree::verify_proof(tree.leaf(4), proof4, root));

    // Verify proof for shard 8
    auto proof8 = tree.get_proof(8);
    REQUIRE(MerkleTree::verify_proof(tree.leaf(8), proof8, root));
}

TEST_CASE("Tampered Merkle proof fails verification", "[merkle]") {
    std::vector<std::vector<uint8_t>> shards(9);
    for (size_t i = 0; i < 9; ++i) {
        shards[i].resize(50, static_cast<uint8_t>(i + 1));
    }

    MerkleTree tree(shards);
    auto root = tree.root();

    auto proof = tree.get_proof(3);
    auto leaf = tree.leaf(3);

    // Tamper with the leaf hash
    auto bad_leaf = leaf;
    bad_leaf[0] ^= 0xFF;

    REQUIRE(!MerkleTree::verify_proof(bad_leaf, proof, root));
}

TEST_CASE("Merkle tree is deterministic", "[merkle]") {
    std::vector<std::vector<uint8_t>> shards(5);
    for (size_t i = 0; i < 5; ++i) {
        shards[i] = {static_cast<uint8_t>(i), static_cast<uint8_t>(i + 1), static_cast<uint8_t>(i + 2)};
    }

    MerkleTree tree1(shards);
    MerkleTree tree2(shards);

    REQUIRE(tree1.root() == tree2.root());
}
