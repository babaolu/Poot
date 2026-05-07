// merkle.hpp — Merkle tree for shard integrity and proof-of-storage
// C++23 — adheres to CppCoreGuidelines
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <span>

#include "crypto.hpp"

namespace Poot::ShardEngine::Merkle {

// Direction of a hash in the proof: LEFT or RIGHT child
enum class Direction : uint8_t { LEFT, RIGHT };

// A Merkle proof step: (hash of sibling, direction)
using ProofStep = std::pair<std::vector<uint8_t>, Direction>;
using Proof = std::vector<ProofStep>;

// Compute a leaf hash: SHA-256("leaf" || index (4 bytes BE) || data)
[[nodiscard]] inline auto leaf_hash(std::span<const uint8_t> data, uint32_t index)
    -> std::vector<uint8_t>
{
    constexpr std::string_view LEAF_PREFIX = "leaf";
    std::vector<uint8_t> buffer;
    buffer.reserve(LEAF_PREFIX.size() + sizeof(uint32_t) + data.size());

    // Append "leaf" prefix
    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(LEAF_PREFIX.data()),
                 reinterpret_cast<const uint8_t*>(LEAF_PREFIX.data()) + LEAF_PREFIX.size());

    // Append index as 4 bytes big-endian
    uint8_t idx_bytes[4] = {
        static_cast<uint8_t>((index >> 24) & 0xFF),
        static_cast<uint8_t>((index >> 16) & 0xFF),
        static_cast<uint8_t>((index >> 8) & 0xFF),
        static_cast<uint8_t>(index & 0xFF)
    };
    buffer.insert(buffer.end(), idx_bytes, idx_bytes + 4);

    // Append data
    buffer.insert(buffer.end(), data.begin(), data.end());

    return Crypto::sha256(buffer);
}

// Compute an internal node hash: SHA-256("node" || left || right)
[[nodiscard]] inline auto node_hash(const std::vector<uint8_t>& left, const std::vector<uint8_t>& right)
    -> std::vector<uint8_t>
{
    constexpr std::string_view NODE_PREFIX = "node";
    std::vector<uint8_t> buffer;
    buffer.reserve(NODE_PREFIX.size() + left.size() + right.size());

    buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(NODE_PREFIX.data()),
                 reinterpret_cast<const uint8_t*>(NODE_PREFIX.data()) + NODE_PREFIX.size());
    buffer.insert(buffer.end(), left.begin(), left.end());
    buffer.insert(buffer.end(), right.begin(), right.end());

    return Crypto::sha256(buffer);
}

// Merkle tree class
class MerkleTree {
public:
    explicit MerkleTree(std::span<const std::vector<uint8_t>> shards)
        : leaves_(shards.size()), root_hash_(32, 0)
    {
        if (shards.empty()) {
            throw std::invalid_argument("Cannot build Merkle tree with 0 shards");
        }

        // Compute leaf hashes
        for (size_t i = 0; i < shards.size(); ++i) {
            leaves_[i] = leaf_hash(shards[i], static_cast<uint32_t>(i));
        }

        // Build tree bottom-up
        std::vector<std::vector<uint8_t>> current = leaves_;
        while (current.size() > 1) {
            std::vector<std::vector<uint8_t>> next;
            for (size_t i = 0; i < current.size(); i += 2) {
                if (i + 1 < current.size()) {
                    next.push_back(node_hash(current[i], current[i + 1]));
                } else {
                    // Odd number: duplicate last
                    next.push_back(node_hash(current[i], current[i]));
                }
            }
            current = std::move(next);
        }
        root_hash_ = std::move(current[0]);
    }

    [[nodiscard]] auto root() const -> const std::vector<uint8_t>& { return root_hash_; }

    [[nodiscard]] auto leaf(size_t index) const -> const std::vector<uint8_t>& {
        return leaves_.at(index);
    }

    // Get Merkle proof for a given leaf index
    // Returns list of (sibling_hash, direction) from leaf to root
    [[nodiscard]] auto get_proof(size_t leaf_index) const -> Proof
    {
        if (leaf_index >= leaves_.size()) {
            throw std::out_of_range("Leaf index out of range");
        }

        std::vector<std::vector<uint8_t>> current = leaves_;
        Proof proof;
        size_t idx = leaf_index;

        while (current.size() > 1) {
            size_t sibling = (idx % 2 == 0) ? idx + 1 : idx - 1;
            // If idx is the last and the count is odd, sibling = idx (self)
            if (idx == current.size() - 1 && current.size() % 2 == 1) {
                sibling = idx;
            }

            Direction dir = (idx % 2 == 0) ? Direction::RIGHT : Direction::LEFT;
            proof.emplace_back(current[sibling], dir);

            // Compute next level
            std::vector<std::vector<uint8_t>> next;
            for (size_t i = 0; i < current.size(); i += 2) {
                if (i + 1 < current.size()) {
                    next.push_back(node_hash(current[i], current[i + 1]));
                } else {
                    next.push_back(node_hash(current[i], current[i]));
                }
            }

            idx /= 2;
            current = std::move(next);
        }

        return proof;
    }

    // Verify a Merkle proof
    [[nodiscard]] static auto verify_proof(
        const std::vector<uint8_t>& leaf_hash,
        const Proof& proof,
        const std::vector<uint8_t>& root)
        -> bool
    {
        std::vector<uint8_t> current = leaf_hash;
        for (const auto& [sibling, dir] : proof) {
            if (dir == Direction::LEFT) {
                current = node_hash(sibling, current);
            } else {
                current = node_hash(current, sibling);
            }
        }
        return current == root;
    }

private:
    std::vector<std::vector<uint8_t>> leaves_;
    std::vector<uint8_t> root_hash_;
};

} // namespace Poot::ShardEngine::Merkle
