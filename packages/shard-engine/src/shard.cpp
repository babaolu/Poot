// shard.cpp — C-style API for Poot Shard Engine
// C++23 — adheres to CppCoreGuidelines
// Provides C-linkage entry points for WASM and native interop

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "shard_engine/shard.hpp"

using namespace Poot::ShardEngine;

// Opaque handle types
struct ShardHandle {
    std::vector<Shard> shards;
    Manifest manifest;
    std::vector<uint8_t> reconstructed;
};

struct SplitResult {
    ShardHandle* handle = nullptr;
    char* manifest_json = nullptr;
};

extern "C" {

// Split data into shards and encrypt
// Returns: opaque handle, or nullptr on error
// Caller must call shard_free() when done
void* shard_split(const uint8_t* data, size_t len, size_t* out_shard_count) {
    if (!data || len == 0 || !out_shard_count) return nullptr;
    try {
        std::span<const uint8_t> span(data, len);
        auto result = split_and_encrypt(span);
        if (!result.has_value()) return nullptr;

        auto* handle = new ShardHandle();
        handle->shards = std::move(result->first);
        handle->manifest = std::move(result->second);
        *out_shard_count = handle->shards.size();
        return static_cast<void*>(handle);
    } catch (...) {
        return nullptr;
    }
}

// Get shard data by index (0..count-1)
// Returns: pointer to shard bytes, sets out_len
const uint8_t* shard_get_data(void* handle, size_t index, size_t* out_len) {
    if (!handle || !out_len) return nullptr;
    auto* h = static_cast<ShardHandle*>(handle);
    if (index >= h->shards.size()) return nullptr;
    *out_len = h->shards[index].data.size();
    return h->shards[index].data.data();
}

// Get shard IV by index
const uint8_t* shard_get_iv(void* handle, size_t index, size_t* out_len) {
    if (!handle || !out_len) return nullptr;
    auto* h = static_cast<ShardHandle*>(handle);
    if (index >= h->shards.size()) return nullptr;
    *out_len = h->shards[index].iv.size();
    return h->shards[index].iv.data();
}

// Get shard auth tag by index
const uint8_t* shard_get_tag(void* handle, size_t index, size_t* out_len) {
    if (!handle || !out_len) return nullptr;
    auto* h = static_cast<ShardHandle*>(handle);
    if (index >= h->shards.size()) return nullptr;
    *out_len = h->shards[index].tag.size();
    return h->shards[index].tag.data();
}

// Get shard index by original position
size_t shard_get_index(void* handle, size_t shard_pos) {
    if (!handle) return SIZE_MAX;
    auto* h = static_cast<ShardHandle*>(handle);
    if (shard_pos >= h->shards.size()) return SIZE_MAX;
    return h->shards[shard_pos].index;
}

// Get manifest as JSON string
// Caller must free() the returned string
const char* shard_get_manifest_json(void* handle) {
    if (!handle) return nullptr;
    try {
        auto* h = static_cast<ShardHandle*>(handle);
        std::string json = h->manifest.to_json();
        char* buf = static_cast<char*>(malloc(json.size() + 1));
        if (!buf) return nullptr;
        std::memcpy(buf, json.c_str(), json.size() + 1);
        return buf;
    } catch (...) {
        return nullptr;
    }
}

// Reconstruct data from shards with optional explicit auth tags
// manifest_json: the manifest JSON from shard_get_manifest_json()
// shard_data_ptrs: array of pointers to shard data bytes
// shard_lens: array of shard data lengths
// shard_tag_ptrs: optional array of pointers to 16-byte shard auth tags (can be nullptr)
// shard_indices: original indices of each shard (0..total-1)
// shard_count: number of shards provided
// out_len: set to reconstructed data length on success
// Returns: pointer to reconstructed data, or nullptr on error
// Caller must free() the returned pointer
const uint8_t* shard_reconstruct_with_tags(
    const char* manifest_json,
    const uint8_t** shard_data_ptrs,
    const size_t* shard_lens,
    const uint8_t** shard_tag_ptrs,
    const size_t* shard_indices,
    size_t shard_count,
    size_t* out_len) {
    if (!manifest_json || !shard_data_ptrs || !shard_lens || !shard_indices || shard_count == 0) {
        return nullptr;
    }
    try {
        // Parse manifest
        auto manifest_result = Manifest::from_json(manifest_json);
        if (!manifest_result.has_value()) return nullptr;
        Manifest manifest = std::move(*manifest_result);

        // Build shard vector
        std::vector<Shard> shards(shard_count);
        for (size_t i = 0; i < shard_count; ++i) {
            shards[i].index = shard_indices[i];

            if (shard_tag_ptrs && shard_tag_ptrs[i]) {
                shards[i].data.assign(shard_data_ptrs[i], shard_data_ptrs[i] + shard_lens[i]);
                shards[i].tag.assign(shard_tag_ptrs[i], shard_tag_ptrs[i] + 16);
            } else if (shard_lens[i] > 16 && shard_lens[i] > manifest.shard_size) {
                // If tag is appended to shard data (ciphertext + 16-byte tag)
                size_t cipher_len = shard_lens[i] - 16;
                shards[i].data.assign(shard_data_ptrs[i], shard_data_ptrs[i] + cipher_len);
                shards[i].tag.assign(shard_data_ptrs[i] + cipher_len, shard_data_ptrs[i] + shard_lens[i]);
            } else {
                shards[i].data.assign(shard_data_ptrs[i], shard_data_ptrs[i] + shard_lens[i]);
            }
        }

        auto result = reconstruct_and_verify(shards, manifest);
        if (!result.has_value()) return nullptr;

        uint8_t* buf = static_cast<uint8_t*>(malloc(result->size()));
        if (!buf) return nullptr;
        std::memcpy(buf, result->data(), result->size());
        if (out_len) *out_len = result->size();
        return buf;
    } catch (...) {
        return nullptr;
    }
}

// Reconstruct data from shards (legacy C ABI signature)
const uint8_t* shard_reconstruct(
    const char* manifest_json,
    const uint8_t** shard_data_ptrs,
    const size_t* shard_lens,
    const size_t* shard_indices,
    size_t shard_count,
    size_t* out_len) {
    return shard_reconstruct_with_tags(
        manifest_json,
        shard_data_ptrs,
        shard_lens,
        nullptr,
        shard_indices,
        shard_count,
        out_len);
}

// Free handle and all associated memory
void shard_free(void* handle) {
    if (!handle) return;
    delete static_cast<ShardHandle*>(handle);
}

// Free a string returned by shard_get_manifest_json()
void shard_free_string(const char* s) {
    free(const_cast<char*>(s));
}

// Free data returned by shard_reconstruct() or shard_reconstruct_with_tags()
void shard_free_data(const uint8_t* p) {
    free(const_cast<uint8_t*>(p));
}

} // extern "C"
