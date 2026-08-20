// shard.hpp — Public API for Poot Shard Engine
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

namespace Poot::ShardEngine {

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
    std::vector<std::string> shard_ivs_b64;    // Base64 IV used for each shard (12 bytes each)
    std::string wrapped_key_b64;               // Envelope-encrypted DEK (AES-256-GCM ciphertext, Base64)
    std::string key_iv_b64;                    // IV for wrapped DEK (12 bytes, Base64)
    std::string key_tag_b64;                   // Tag for wrapped DEK (16 bytes, Base64)

    // Serialize to JSON
    [[nodiscard]] auto to_json() const -> std::string;

    // Deserialize from JSON
    [[nodiscard]] static auto from_json(const std::string& json) -> std::expected<Manifest, std::string>;
};

// Split data into shards and encrypt each shard with a unique random IV and envelope-encrypted DEK
// Returns: (vector of Shards, Manifest)
// Default: 6 data + 3 parity = 9 total shards; any 6 can reconstruct
[[nodiscard]] auto split_and_encrypt(
    std::span<const uint8_t> data,
    size_t data_shards = 6,
    size_t parity_shards = 3,
    std::span<const uint8_t> customer_kek = {})
    -> std::expected<std::pair<std::vector<Shard>, Manifest>, std::string>;

// Given any `data_shards` shards, reconstruct and verify the original data
// The shards can be any subset of the originals (as long as count >= data_shards)
[[nodiscard]] auto reconstruct_and_verify(
    std::span<const Shard> shards,
    const Manifest& manifest,
    std::span<const uint8_t> customer_kek = {})
    -> std::expected<std::vector<uint8_t>, std::string>;

// --- Implementation ---

namespace detail {

// Default Key-Encryption-Key for envelope encryption when no custom customer_kek is supplied
inline auto default_kek() -> std::array<uint8_t, 32> {
    static constexpr std::string_view kSeed = "CloudMine_Envelope_Master_KEK_v1_Secret";
    auto hash = Crypto::sha256(kSeed);
    std::array<uint8_t, 32> key{};
    std::copy_n(hash.begin(), 32, key.begin());
    return key;
}

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
    j += "\"wrapped_key_b64\":\"" + json_escape(wrapped_key_b64) + "\",";
    j += "\"key_iv_b64\":\"" + json_escape(key_iv_b64) + "\",";
    j += "\"key_tag_b64\":\"" + json_escape(key_tag_b64) + "\",";
    j += "\"shard_hashes_b64\":[";
    for (size_t i = 0; i < shard_hashes_b64.size(); ++i) {
        if (i > 0) j += ",";
        j += "\"" + json_escape(shard_hashes_b64[i]) + "\"";
    }
    j += "],";
    j += "\"shard_ivs_b64\":[";
    for (size_t i = 0; i < shard_ivs_b64.size(); ++i) {
        if (i > 0) j += ",";
        j += "\"" + json_escape(shard_ivs_b64[i]) + "\"";
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
        } else if (*key == "wrapped_key_b64") {
            skip_ws();
            auto val = json_parse_string(s);
            if (!val.has_value()) return std::unexpected("Failed to parse wrapped_key_b64");
            m.wrapped_key_b64 = *val;
        } else if (*key == "key_iv_b64") {
            skip_ws();
            auto val = json_parse_string(s);
            if (!val.has_value()) return std::unexpected("Failed to parse key_iv_b64");
            m.key_iv_b64 = *val;
        } else if (*key == "key_tag_b64") {
            skip_ws();
            auto val = json_parse_string(s);
            if (!val.has_value()) return std::unexpected("Failed to parse key_tag_b64");
            m.key_tag_b64 = *val;
        } else if (*key == "shard_hashes_b64") {
            skip_ws();
            auto val = json_parse_array(s);
            if (!val.has_value()) return std::unexpected("Failed to parse shard_hashes_b64");
            m.shard_hashes_b64 = *val;
        } else if (*key == "shard_ivs_b64") {
            skip_ws();
            auto val = json_parse_array(s);
            if (!val.has_value()) return std::unexpected("Failed to parse shard_ivs_b64");
            m.shard_ivs_b64 = *val;
        } else if (*key == "encryption_iv_b64") {
            // Legacy / fallback support
            skip_ws();
            auto val = json_parse_string(s);
            if (!val.has_value()) return std::unexpected("Failed to parse encryption_iv_b64");
            if (m.shard_ivs_b64.empty()) {
                m.shard_ivs_b64.push_back(*val);
            }
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
    size_t parity_shards,
    std::span<const uint8_t> customer_kek)
    -> std::expected<std::pair<std::vector<Shard>, Manifest>, std::string>
{
    if (data.empty()) return std::unexpected("Cannot shard empty data");
    if (data_shards == 0 || parity_shards == 0) {
        return std::unexpected("Shard counts must be positive");
    }
    if (!customer_kek.empty() && customer_kek.size() != 32) {
        return std::unexpected("Customer KEK must be 32 bytes");
    }

    // Step 1: Compute content hash (SHA-256 of original data)
    std::vector<uint8_t> content_hash = Crypto::sha256(data);
    std::string content_hash_b64 = detail::base64_encode(content_hash);

    // Step 2: Generate random 256-bit DEK (Data Encryption Key) - NOT derived from content hash
    std::array<uint8_t, 32> dek{};
    Crypto::random_bytes(dek);

    // Step 3: Envelope encrypt DEK with customer KEK (or default master KEK)
    auto kek = customer_kek.empty() ? detail::default_kek() : [&]() {
        std::array<uint8_t, 32> k{};
        std::copy_n(customer_kek.begin(), 32, k.begin());
        return k;
    }();

    std::vector<uint8_t> key_iv(12, 0);
    Crypto::random_bytes(key_iv);

    auto wrapped = Crypto::aes256_gcm_encrypt(kek, key_iv, dek);
    if (!wrapped.has_value()) {
        return std::unexpected("DEK envelope encryption failed: " + wrapped.error());
    }

    // Step 4: Split data into data_shards chunks
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

    // Step 5: Reed-Solomon encode to get parity shards
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

    // Step 6: Build Merkle tree over all shard bytes (before encryption)
    Merkle::MerkleTree merkle(all_shards_before_encryption);

    // Step 7: Encrypt each shard with unique random 12-byte IV and the random DEK
    std::vector<Shard> shards;
    shards.reserve(total_shards);
    std::vector<std::string> shard_hashes_b64;
    shard_hashes_b64.reserve(total_shards);
    std::vector<std::string> shard_ivs_b64;
    shard_ivs_b64.reserve(total_shards);

    for (size_t i = 0; i < total_shards; ++i) {
        // Hash shard before encryption (for manifest integrity)
        std::vector<uint8_t> shard_hash = Crypto::sha256(all_shards_before_encryption[i]);
        shard_hashes_b64.push_back(detail::base64_encode(shard_hash));

        // Generate fresh, unique IV for this shard
        std::vector<uint8_t> shard_iv(12, 0);
        Crypto::random_bytes(shard_iv);
        shard_ivs_b64.push_back(detail::base64_encode(shard_iv));

        // Encrypt
        auto encrypted = Crypto::aes256_gcm_encrypt(
            dek,
            shard_iv,
            all_shards_before_encryption[i]);
        if (!encrypted.has_value()) {
            return std::unexpected("AES encryption failed for shard " + std::to_string(i) + ": " + encrypted.error());
        }

        Shard shard;
        shard.data = std::move(encrypted->first);
        shard.index = i;
        shard.iv = std::move(shard_iv);
        shard.tag = std::move(encrypted->second);
        shard.merkle_proof = {}; // Can be populated on demand from merkle tree
        shards.push_back(std::move(shard));
    }

    // Step 8: Build Manifest
    Manifest manifest;
    manifest.content_hash_b64 = content_hash_b64;
    manifest.original_size = data.size();
    manifest.shard_size = shard_size;
    manifest.data_shards = data_shards;
    manifest.parity_shards = parity_shards;
    manifest.shard_hashes_b64 = std::move(shard_hashes_b64);
    manifest.shard_ivs_b64 = std::move(shard_ivs_b64);
    manifest.wrapped_key_b64 = detail::base64_encode(wrapped->first);
    manifest.key_iv_b64 = detail::base64_encode(key_iv);
    manifest.key_tag_b64 = detail::base64_encode(wrapped->second);

    return std::make_pair(std::move(shards), std::move(manifest));
}

inline auto reconstruct_and_verify(
    std::span<const Shard> shards,
    const Manifest& manifest,
    std::span<const uint8_t> customer_kek)
    -> std::expected<std::vector<uint8_t>, std::string>
{
    if (shards.size() < manifest.data_shards) {
        return std::unexpected("Need at least " + std::to_string(manifest.data_shards) +
                               " shards, got " + std::to_string(shards.size()));
    }
    if (!customer_kek.empty() && customer_kek.size() != 32) {
        return std::unexpected("Customer KEK must be 32 bytes");
    }

    // Step 1: Unwrap DEK using KEK
    auto kek = customer_kek.empty() ? detail::default_kek() : [&]() {
        std::array<uint8_t, 32> k{};
        std::copy_n(customer_kek.begin(), 32, k.begin());
        return k;
    }();

    auto wrapped_key = detail::base64_decode(manifest.wrapped_key_b64);
    if (!wrapped_key.has_value()) return std::unexpected("Invalid wrapped_key_b64: " + wrapped_key.error());

    auto key_iv = detail::base64_decode(manifest.key_iv_b64);
    if (!key_iv.has_value()) return std::unexpected("Invalid key_iv_b64: " + key_iv.error());

    auto key_tag = detail::base64_decode(manifest.key_tag_b64);
    if (!key_tag.has_value()) return std::unexpected("Invalid key_tag_b64: " + key_tag.error());

    auto unwrapped_dek = Crypto::aes256_gcm_decrypt(
        kek, *key_iv, *wrapped_key, *key_tag);
    if (!unwrapped_dek.has_value()) {
        return std::unexpected("Failed to unwrap DEK (invalid customer key or corrupted manifest): " + unwrapped_dek.error());
    }
    if (unwrapped_dek->size() != 32) {
        return std::unexpected("Unwrapped DEK has invalid length: " + std::to_string(unwrapped_dek->size()));
    }

    // Step 2: Decrypt each shard using unwrapped DEK and per-shard IV
    std::vector<std::vector<uint8_t>> decrypted_shards;
    std::vector<size_t> indices;
    for (const auto& shard : shards) {
        std::vector<uint8_t> shard_iv;
        if (!shard.iv.empty()) {
            shard_iv = shard.iv;
        } else if (shard.index < manifest.shard_ivs_b64.size()) {
            auto decoded_iv = detail::base64_decode(manifest.shard_ivs_b64[shard.index]);
            if (!decoded_iv.has_value()) {
                return std::unexpected("Invalid shard IV for index " + std::to_string(shard.index) + ": " + decoded_iv.error());
            }
            shard_iv = std::move(*decoded_iv);
        } else {
            return std::unexpected("Missing IV for shard index " + std::to_string(shard.index));
        }

        auto plaintext = Crypto::aes256_gcm_decrypt(
            *unwrapped_dek,
            shard_iv,
            shard.data,
            shard.tag);
        if (!plaintext.has_value()) {
            return std::unexpected("AES decryption failed for shard " +
                               std::to_string(shard.index) + ": " + plaintext.error());
        }
        decrypted_shards.push_back(std::move(*plaintext));
        indices.push_back(shard.index);
    }

    // Step 3: Reed-Solomon decode
    ReedSolomon rs;
    auto data_shard_bytes = rs.decode(decrypted_shards, indices, manifest.data_shards);
    if (!data_shard_bytes.has_value()) {
        return std::unexpected("Reed-Solomon decode failed: " + data_shard_bytes.error());
    }

    // Step 4: Reassemble original data
    std::vector<uint8_t> reconstructed;
    for (const auto& ds : *data_shard_bytes) {
        reconstructed.insert(reconstructed.end(), ds.begin(), ds.end());
    }
    // Trim to original size
    if (reconstructed.size() > manifest.original_size) {
        reconstructed.resize(manifest.original_size);
    }

    // Step 5: Verify content hash
    std::vector<uint8_t> verify_hash = Crypto::sha256(reconstructed);
    std::string verify_hash_b64 = detail::base64_encode(verify_hash);
    if (verify_hash_b64 != manifest.content_hash_b64) {
        return std::unexpected("Content hash mismatch — data may be corrupted or incomplete");
    }

    return reconstructed;
}

} // namespace Poot::ShardEngine
