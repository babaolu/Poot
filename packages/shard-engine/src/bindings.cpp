// bindings.cpp — Emscripten JS bindings for Poot Shard Engine
// C++23 — adheres to CppCoreGuidelines

#include <emscripten/bind.h>
#include <vector>
#include <cstdint>
#include <cstddef>

#include "shard_engine/shard.hpp"

using namespace emscripten;
using namespace Poot::ShardEngine;

// Wrapper: split_and_encrypt returns a JS object
// The caller provides data as Uint8Array, gets back {shards, manifest_json}
EMSCRIPTEN_BINDINGS(shard_engine) {

    // Shard struct binding
    value_object<Shard>("Shard")
        .field("index", &Shard::index)
        // Note: data, iv, tag, merkle_proof are accessed via helper functions below
        ;

    // Function: split_and_encrypt
    function("split_and_encrypt", optional_override([](const val& data_val,
                                                   uint32_t data_shards,
                                                   uint32_t parity_shards) -> val {
        // Convert JS Uint8Array to std::vector<uint8_t>
        std::vector<uint8_t> data;
        uint32_t len = data_val["length"].as<uint32_t>();
        data.reserve(len);
        for (uint32_t i = 0; i < len; ++i) {
            data.push_back(data_val[i].as<uint8_t>());
        }

        auto result = split_and_encrypt(
            std::span<const uint8_t>(data.data(), data.size()),
            static_cast<size_t>(data_shards),
            static_cast<size_t>(parity_shards));

        if (!result.has_value()) {
            return val::undefined();
        }

        auto& [shards, manifest] = *result;

        // Build return object
        val obj = val::object();
        val shards_arr = val::array();
        for (size_t i = 0; i < shards.size(); ++i) {
            val shard_obj = val::object();
            shard_obj.set("index", static_cast<uint32_t>(shards[i].index));

            // data as Uint8Array
            val data_arr = val::array();
            for (size_t j = 0; j < shards[i].data.size(); ++j) {
                data_arr.set(j, shards[i].data[j]);
            }
            shard_obj.set("data", data_arr);

            // iv
            val iv_arr = val::array();
            for (size_t j = 0; j < shards[i].iv.size(); ++j) {
                iv_arr.set(j, shards[i].iv[j]);
            }
            shard_obj.set("iv", iv_arr);

            // tag
            val tag_arr = val::array();
            for (size_t j = 0; j < shards[i].tag.size(); ++j) {
                tag_arr.set(j, shards[i].tag[j]);
            }
            shard_obj.set("tag", tag_arr);

            shards_arr.set(i, shard_obj);
        }
        obj.set("shards", shards_arr);
        obj.set("manifest_json", manifest.to_json());
        return obj;
    }));

    // Function: reconstruct_and_verify
    function("reconstruct_and_verify", optional_override([](const val& shards_val,
                                                        const std::string& manifest_json) -> val {
        // Parse shards from JS array
        size_t count = shards_val["length"].as<size_t>();
        std::vector<Shard> shards(count);
        for (size_t i = 0; i < count; ++i) {
            val shard_val = shards_val[i];
            shards[i].index = shard_val["index"].as<size_t>();

            val data_val = shard_val["data"];
            uint32_t data_len = data_val["length"].as<uint32_t>();
            shards[i].data.resize(data_len);
            for (uint32_t j = 0; j < data_len; ++j) {
                shards[i].data[j] = data_val[j].as<uint8_t>();
            }

            val iv_val = shard_val["iv"];
            uint32_t iv_len = iv_val["length"].as<uint32_t>();
            shards[i].iv.resize(iv_len);
            for (uint32_t j = 0; j < iv_len; ++j) {
                shards[i].iv[j] = iv_val[j].as<uint8_t>();
            }

            val tag_val = shard_val["tag"];
            uint32_t tag_len = tag_val["length"].as<uint32_t>();
            shards[i].tag.resize(tag_len);
            for (uint32_t j = 0; j < tag_len; ++j) {
                shards[i].tag[j] = tag_val[j].as<uint8_t>();
            }
        }

        auto manifest_result = Manifest::from_json(manifest_json);
        if (!manifest_result.has_value()) {
            return val::undefined();
        }

        auto result = reconstruct_and_verify(shards, *manifest_result);
        if (!result.has_value()) {
            return val::undefined();
        }

        // Return as Uint8Array
        val out = val::array();
        for (size_t i = 0; i < result->size(); ++i) {
            out.set(i, (*result)[i]);
        }
        return out;
    }));
}
