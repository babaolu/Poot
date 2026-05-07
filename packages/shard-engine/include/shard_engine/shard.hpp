// shard.hpp — Public API for CloudMine Shard Engine
// C++23 — adheres to CppCoreGuidelines
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "reed_solomon.hpp"
#include "crypto.hpp"
#include "merkle.hpp"

namespace CloudMine::ShardEngine {

// Represents one encrypted shard ready for storage on a miner
struct Shard {
    std::vector<uint8_t> data;       // Encrypted shard bytes
    size_t index;                      // 0..N-1 (original position)
    std::vector<uint8_t> iv;          // AES-256-GCM IV (12 bytes)
    std::vector<uint8_t> tag;         // AES-256-GCM auth tag (16 bytes)
    std::vector<uint8_t> merkle_proof; // Serialized Merkle proof (for Phase 2)
};

// Manifest describing the sharding operation
// Passed to reconstruct_and_verify along with the shards
struct Manifest {
    std::string content_hash_b64;          // SHA-256 of original data (Base64)
    size_t original_size = 0;
    size_t shard_size = 0;
    size_t data_shards = 6;
    size_t parity_shards = 3;
    std::vector<std::string> shard_hashes_b64; // SHA-256 of each shard (for integrity, Base64)
    std::string encryption_iv_b64;         // Base64 IV used for encryption

    // Serialize to JSON
    [[nodiscard]] auto to_json() const -> std::string;

    // Deserialize from JSON
    [[nodiscard]] static auto from_json(const std::string& json) -> std::expected<Manifest, std::string>;
};

// Split data into shards and encrypt each shard
// Returns: (vector of Shards, Manifest)
// Default: 6 data + 3 parity = 9 total shards; any 6 can reconstruct
[[nodiscard]] auto split_and_encrypt(
    std::span<const uint8_t> data,
    size_t data_shards = 6,
    size_t parity_shards = 3)
    -> std::expected<std::pair<std::vector<Shard>, Manifest>, std::string>;

// Given any `data_shards` shards, reconstruct and verify the original data
// The shards can be any subset of the originals (as long as count >= data_shards)
[[nodiscard]] auto reconstruct_and_verify(
    std::span<const Shard> shards,
    const Manifest& manifest)
    -> std::expected<std::vector<uint8_t>, std::string>;

// --- Implementation ---

namespace detail {

// Base64 encode/decode helpers (header-only, no external deps)
[[nodiscard]] inline auto base64_encode(std::span<const uint8_t> data) -> std::string {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t triple = (static_cast<uint32_t>(data[i]) << 16);
        triple |= (i + 1 < data.size()) ? (static_cast<uint32_t>(data[i + 1]) << 8) : 0;
        triple |= (i + 2 < data.size()) ? static_cast<uint32_t>(data[i + 2]) : 0;
        out.push_back(table[(triple >> 18) & 0x3F]);
        out.push_back(table[(triple >> 12) & 0x3F]);
        out.push_back((i + 1 < data.size()) ? table[(triple >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < data.size()) ? table[triple & 0x3F] : '=');
    }
    return out;
}

[[nodiscard]] inline auto base64_decode(std::string_view s)
    -> std::expected<std::vector<uint8_t>, std::string>
{
    static constexpr int8_t decode_table[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1
    };
    std::vector<uint8_t> out;
    out.reserve((s.size() * 3) / 4);
    for (size_t i = 0; i < s.size(); ) {
        int8_t a = (i < s.size()) ? decode_table[static_cast<uint8_t>(s[i++])] : -1;
        int8_t b = (i < s.size()) ? decode_table[static_cast<uint8_t>(s[i++])] : -1;
        int8_t c = (i < s.size()) ? decode_table[static_cast<uint8_t>(s[i++])] : -1;
        int8_t d = (i < s.size()) ? decode_table[static_cast<uint8_t>(s[i++])] : -1;
        if (a < 0 || b < 0) return std::unexpected("Invalid Base64 character");
        out.push_back(static_cast<uint8_t>((a << 2) | ((b >> 4) & 0x3)));
        if (c >= 0) {
            out.push_back(static_cast<uint8_t>(((b << 4) & 0xF0) | ((c >> 2) & 0xF)));
            if (d >= 0) {
                out.push_back(static_cast<uint8_t>(((c << 6) & 0xC0) | (d & 0x3F)));
            }
        }
    }
    return out;
}

// Simple JSON string escaping
[[nodiscard]] inline auto json_escape(std::string_view s) -> std::string {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    return out;
}

// Simple JSON parser for our specific Manifest format
[[nodiscard]] inline auto json_parse_string(std::string_view& s)
    -> std::expected<std::string, std::string>
{
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) s.remove_prefix(1);
    if (s.empty() || s[0] != '"') return std::unexpected("Expected string");
    s.remove_prefix(1);
    std::string out;
    while (!s.empty() && s[0] != '"') {
        if (s[0] == '\\' && s.size() > 1) {
            s.remove_prefix(1);
            if (s[0] == '"') out.push_back('"');
            else if (s[0] == '\\') out.push_back('\\');
            else if (s[0] == 'n') out.push_back('\n');
            else if (s[0] == 'r') out.push_back('\r');
            else if (s[0] == 't') out.push_back('\t');
            s.remove_prefix(1);
        } else {
            out.push_back(s[0]);
            s.remove_prefix(1);
        }
    }
    if (!s.empty()) s.remove_prefix(1); // skip closing "
    return out;
}

[[nodiscard]] inline auto json_parse_number(std::string_view& s)
    -> std::expected<size_t, std::string>
{
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) s.remove_prefix(1);
    size_t val = 0;
    while (!s.empty() && s[0] >= '0' && s[0] <= '9') {
        val = val * 10 + (s[0] - '0');
        s.remove_prefix(1);
    }
    return val;
}

[[nodiscard]] inline auto json_parse_array(std::string_view& s)
    -> std::expected<std::vector<std::string>, std::string>
{
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) s.remove_prefix(1);
    if (s.empty() || s[0] != '[') return std::unexpected("Expected array");
    s.remove_prefix(1);
    std::vector<std::string> out;
    while (!s.empty()) {
        while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) s.remove_prefix(1);
        if (s[0] == ']') { s.remove_prefix(1); break; }
        if (s[0] == ',') { s.remove_prefix(1); continue; }
        auto str = json_parse_string(s);
        if (!str.has_value()) return std::unexpected(str.error());
        out.push_back(std::move(*str));
    }
    return out;
}

} // namespace detail

// --- Manifest JSON serialization ---

inline auto Manifest::to_json() const -> std::string {
    using detail::base64_encode;
    using detail::json_escape;
    std::string j = "{";
    j += "\"content_hash_b64\":\"" + json_escape(content_hash_b64) + "\",";
    j += "\"original_size\":" + std::to_string(original_size) + ",";
    j += "\"shard_size\":" + std::to_string(shard_size) + ",";
    j += "\"data_shards\":" + std::to_string(data_shards) + ",";
    j += "\"parity_shards\":" + std::to_string(parity_shards) + ",";
    j += "\"encryption_iv_b64\":\"" + json_escape(encryption_iv_b64) + "\",";
    j += "\"shard_hashes_b64\":[";
    for (size_t i = 0; i < shard_hashes_b64.size(); ++i) {
        if (i > 0) j += ",";
        j += "\"" + json_escape(shard_hashes_b64[i]) + "\"";
    }
    j += "]}";
    return j;
}

inline auto Manifest::from_json(const std::string& json) -> std::expected<Manifest, std::string> {
    using detail::json_parse_string;
    using detail::json_parse_number;
    using detail::json_parse_array;
    Manifest m;
    std::string_view s = json;

    // Skip whitespace
    auto skip_ws = [&]() {
        while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) {
            s.remove_prefix(1);
        }
    };

    skip_ws();
    if (s.empty() || s[0] != '{') return std::unexpected("Expected object");
    s.remove_prefix(1); // skip {

    while (true) {
        skip_ws();
        if (s.empty()) return std::unexpected("Unexpected end");
        if (s[0] == '}') { s.remove_prefix(1); break; }

        // Parse key
        if (s[0] != '"') return std::unexpected("Expected string key");
        auto key = json_parse_string(s);
        if (!key.has_value()) return std::unexpected("Failed to parse key");

        skip_ws();
        if (s.empty() || s[0] != ':') return std::unexpected("Expected colon");
        s.remove_prefix(1); // skip :

        // Parse value based on key
        if (*key == "content_hash_b64") {
            skip_ws();
            auto val = json_parse_string(s);
            if (!val.has_value()) return std::unexpected("Failed to parse content_hash_b64");
            m.content_hash_b64 = *val;
        } else if (*key == "original_size") {
            skip_ws();
            auto val = json_parse_number(s);
            if (!val.has_value()) return std::unexpected("Failed to parse original_size");
            m.original_size = *val;
        } else if (*key == "shard_size") {
            skip_ws();
            auto val = json_parse_number(s);
            if (!val.has_value()) return std::unexpected("Failed to parse shard_size");
            m.shard_size = *val;
        } else if (*key == "data_shards") {
            skip_ws();
            auto val = json_parse_number(s);
            if (!val.has_value()) return std::unexpected("Failed to parse data_shards");
            m.data_shards = *val;
        } else if (*key == "parity_shards") {
            skip_ws();
            auto val = json_parse_number(s);
            if (!val.has_value()) return std::unexpected("Failed to parse parity_shards");
            m.parity_shards = *val;
        } else if (*key == "encryption_iv_b64") {
            skip_ws();
            auto val = json_parse_string(s);
            if (!val.has_value()) return std::unexpected("Failed to parse encryption_iv_b64");
            m.encryption_iv_b64 = *val;
        } else if (*key == "shard_hashes_b64") {
            skip_ws();
            auto val = json_parse_array(s);
            if (!val.has_value()) return std::unexpected("Failed to parse shard_hashes_b64");
            m.shard_hashes_b64 = *val;
        } else {
            // Skip unknown value
            skip_ws();
            while (!s.empty() && s[0] != ',' && s[0] != '}') s.remove_prefix(1);
        }

        skip_ws();
        if (s.empty()) return std::unexpected("Unexpected end after value");
        if (s[0] == ',') { s.remove_prefix(1); continue; }
        if (s[0] == '}') { s.remove_prefix(1); break; }
        return std::unexpected("Expected comma or closing brace");
    }

    return m;
}

// --- Core functions ---

inline auto split_and_encrypt(
    std::span<const uint8_t> data,
    size_t data_shards,
    size_t parity_shards)
    -> std::expected<std::pair<std::vector<Shard>, Manifest>, std::string>
{
    if (data.empty()) return std::unexpected("Cannot shard empty data");
    if (data_shards == 0 || parity_shards == 0) {
        return std::unexpected("Shard counts must be positive");
    }

    // Step 1: Compute content hash (SHA-256 of original data)
    std::vector<uint8_t> content_hash = Crypto::sha256(data);
    std::string content_hash_b64 = detail::base64_encode(content_hash);

    // Step 2: Derive AES-256 key from content hash
    std::array<uint8_t, 32> key = Crypto::derive_key_from_hash(content_hash);

    // Step 3: Split data into data_shards chunks
    size_t total_shards = data_shards + parity_shards;
    size_t shard_size = (data.size() + data_shards - 1) / data_shards; // ceil

    // Pad data to multiple of data_shards if needed
    std::vector<uint8_t> padded_data(data.begin(), data.end());
    padded_data.resize(shard_size * data_shards, 0);

    // Create data shards
    std::vector<std::vector<uint8_t>> data_shard_bytes(data_shards);
    for (size_t i = 0; i < data_shards; ++i) {
        auto begin = padded_data.begin() + static_cast<ssize_t>(i * shard_size);
        auto end = (i + 1 < data_shards) ? padded_data.begin() + static_cast<ssize_t>((i + 1) * shard_size)
                                          : padded_data.end();
        data_shard_bytes[i].assign(begin, end);
    }

    // Step 4: Reed-Solomon encode to get parity shards
    ReedSolomon rs;
    auto parity = rs.encode(data_shard_bytes, data_shards, parity_shards);
    if (!parity.has_value()) {
        return std::unexpected("Reed-Solomon encode failed: " + parity.error());
    }

    // All shards (data + parity) before encryption
    std::vector<std::vector<uint8_t>> all_shards_before_encryption;
    all_shards_before_encryption.reserve(total_shards);
    all_shards_before_encryption.insert(all_shards_before_encryption.end(),
        data_shard_bytes.begin(), data_shard_bytes.end());
    all_shards_before_encryption.insert(all_shards_before_encryption.end(),
        parity->begin(), parity->end());

    // Step 5: Build Merkle tree over all shard bytes (before encryption)
    Merkle::MerkleTree merkle(all_shards_before_encryption);

    // Step 6: Generate common IV and encrypt each shard
    std::vector<uint8_t> iv(12, 0);
    Crypto::random_bytes(iv);
    std::string iv_b64 = detail::base64_encode(iv);

    std::vector<Shard> shards;
    shards.reserve(total_shards);
    std::vector<std::string> shard_hashes_b64;
    shard_hashes_b64.reserve(total_shards);

    for (size_t i = 0; i < total_shards; ++i) {
        // Hash shard before encryption (for manifest integrity)
        std::vector<uint8_t> shard_hash = Crypto::sha256(all_shards_before_encryption[i]);
        shard_hashes_b64.push_back(detail::base64_encode(shard_hash));

        // Encrypt
        auto encrypted = Crypto::aes256_gcm_encrypt(
            std::span<const uint8_t>(key.data(), key.size()),
            iv,
            all_shards_before_encryption[i]);
        if (!encrypted.has_value()) {
            return std::unexpected("AES encryption failed for shard " + std::to_string(i) + ": " + encrypted.error());
        }

        Shard shard;
        shard.data = std::move(encrypted->first);
        shard.index = i;
        shard.iv = iv; // Same IV for all (content-addressed)
        shard.tag = std::move(encrypted->second);
        shard.merkle_proof = {}; // Can be populated on demand from merkle tree
        shards.push_back(std::move(shard));
    }

    // Step 7: Build Manifest
    Manifest manifest;
    manifest.content_hash_b64 = content_hash_b64;
    manifest.original_size = data.size();
    manifest.shard_size = shard_size;
    manifest.data_shards = data_shards;
    manifest.parity_shards = parity_shards;
    manifest.shard_hashes_b64 = std::move(shard_hashes_b64);
    manifest.encryption_iv_b64 = iv_b64;

    return std::make_pair(std::move(shards), std::move(manifest));
}

inline auto reconstruct_and_verify(
    std::span<const Shard> shards,
    const Manifest& manifest)
    -> std::expected<std::vector<uint8_t>, std::string>
{
    if (shards.size() < manifest.data_shards) {
        return std::unexpected("Need at least " + std::to_string(manifest.data_shards) +
                               " shards, got " + std::to_string(shards.size()));
    }

    // Step 1: Decrypt each shard
    auto key = Crypto::derive_key_from_hash(
        *detail::base64_decode(manifest.content_hash_b64));

    auto iv = detail::base64_decode(manifest.encryption_iv_b64);
    if (!iv.has_value()) return std::unexpected("Invalid IV in manifest: " + iv.error());

    std::vector<std::vector<uint8_t>> decrypted_shards;
    std::vector<size_t> indices;
    for (const auto& shard : shards) {
        auto plaintext = Crypto::aes256_gcm_decrypt(
            std::span<const uint8_t>(key.data(), key.size()),
            *iv,
            shard.data,
            shard.tag);
        if (!plaintext.has_value()) {
            return std::unexpected("AES decryption failed for shard " +
                               std::to_string(shard.index) + ": " + plaintext.error());
        }
        decrypted_shards.push_back(std::move(*plaintext));
        indices.push_back(shard.index);
    }

    // Step 2: Reed-Solomon decode
    ReedSolomon rs;
    auto data_shard_bytes = rs.decode(decrypted_shards, indices, manifest.data_shards);
    if (!data_shard_bytes.has_value()) {
        return std::unexpected("Reed-Solomon decode failed: " + data_shard_bytes.error());
    }

    // Step 3: Reassemble original data
    std::vector<uint8_t> reconstructed;
    for (const auto& ds : *data_shard_bytes) {
        reconstructed.insert(reconstructed.end(), ds.begin(), ds.end());
    }
    // Trim to original size
    if (reconstructed.size() > manifest.original_size) {
        reconstructed.resize(manifest.original_size);
    }

    // Step 4: Verify content hash
    std::vector<uint8_t> verify_hash = Crypto::sha256(reconstructed);
    std::string verify_hash_b64 = detail::base64_encode(verify_hash);
    if (verify_hash_b64 != manifest.content_hash_b64) {
        return std::unexpected("Content hash mismatch — data may be corrupted or incomplete");
    }

    return reconstructed;
}

} // namespace CloudMine::ShardEngine
