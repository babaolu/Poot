// crypto.hpp — AES-256-GCM and SHA-256 wrappers using OpenSSL EVP API
// C++23 — adheres to CppCoreGuidelines
// For WASM: compile with Emscripten -s USE_OPENSSL=1

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <vector>
#include <span>

// OpenSSL headers
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

namespace Poot::ShardEngine::Crypto {

// SHA-256: returns 32-byte hash
[[nodiscard]] inline auto sha256(std::span<const uint8_t> data)
    -> std::vector<uint8_t>
{
    std::vector<uint8_t> hash(32, 0);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestUpdate failed");
    }

    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx, hash.data(), &len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    EVP_MD_CTX_free(ctx);
    return hash;
}

// SHA-256 convenience: from string
[[nodiscard]] inline auto sha256(std::string_view data)
    -> std::vector<uint8_t>
{
    return sha256(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(data.data()), data.size()));
}

// AES-256-GCM encryption
// Key: 32 bytes (256 bits)
// IV: 12 bytes recommended for GCM
// AAD: additional authenticated data (can be empty)
// Returns: (ciphertext, auth_tag_16_bytes)
[[nodiscard]] inline auto aes256_gcm_encrypt(
    std::span<const uint8_t> key,
    std::span<const uint8_t> iv,
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> aad = {})
    -> std::expected<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>, std::string>
{
    if (key.size() != 32) {
        return std::unexpected("AES-256 requires 32-byte key, got " + std::to_string(key.size()));
    }
    if (iv.size() != 12) {
        return std::unexpected("AES-GCM recommends 12-byte IV, got " + std::to_string(iv.size()));
    }

    std::vector<uint8_t> ciphertext(plaintext.size(), 0);
    std::vector<uint8_t> tag(16, 0);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::unexpected("EVP_CIPHER_CTX_new failed");

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_EncryptInit_ex failed");
    }

    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_CTRL_GCM_SET_IVLEN failed");
    }

    // Set key and IV
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_EncryptInit_ex (key/IV) failed");
    }

    // Provide AAD if any
    int len = 0;
    if (!aad.empty()) {
        if (EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::unexpected("EVP_EncryptUpdate (AAD) failed");
        }
    }

    // Encrypt plaintext
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_EncryptUpdate failed");
    }

    // Finalize
    if (EVP_EncryptFinal_ex(ctx, nullptr, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_EncryptFinal_ex failed");
    }

    // Get GCM tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_CTRL_GCM_GET_TAG failed");
    }

    EVP_CIPHER_CTX_free(ctx);
    return std::make_pair(std::move(ciphertext), std::move(tag));
}

// AES-256-GCM decryption
// Returns plaintext or error
[[nodiscard]] inline auto aes256_gcm_decrypt(
    std::span<const uint8_t> key,
    std::span<const uint8_t> iv,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t> tag,
    std::span<const uint8_t> aad = {})
    -> std::expected<std::vector<uint8_t>, std::string>
{
    if (key.size() != 32) {
        return std::unexpected("AES-256 requires 32-byte key, got " + std::to_string(key.size()));
    }
    if (iv.size() != 12) {
        return std::unexpected("AES-GCM recommends 12-byte IV, got " + std::to_string(iv.size()));
    }
    if (tag.size() != 16) {
        return std::unexpected("GCM tag must be 16 bytes, got " + std::to_string(tag.size()));
    }

    std::vector<uint8_t> plaintext(ciphertext.size(), 0);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::unexpected("EVP_CIPHER_CTX_new failed");

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_DecryptInit_ex failed");
    }

    // Set IV length
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_CTRL_GCM_SET_IVLEN failed");
    }

    // Set key and IV
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_DecryptInit_ex (key/IV) failed");
    }

    // Provide AAD if any
    int len = 0;
    if (!aad.empty()) {
        if (EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::unexpected("EVP_DecryptUpdate (AAD) failed");
        }
    }

    // Decrypt ciphertext
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_DecryptUpdate failed");
    }

    // Set expected tag before final
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("EVP_CTRL_GCM_SET_TAG failed");
    }

    // Finalize — this verifies the tag
    if (EVP_DecryptFinal_ex(ctx, nullptr, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return std::unexpected("GCM authentication failed — invalid tag or corrupted ciphertext");
    }

    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

// Derive AES-256 key from SHA-256 hash
// Takes first 32 bytes of the hash (or the full hash if 32 bytes)
[[nodiscard]] inline auto derive_key_from_hash(const std::vector<uint8_t>& hash)
    -> std::array<uint8_t, 32>
{
    std::array<uint8_t, 32> key{};
    size_t copy_len = std::min(static_cast<size_t>(32), hash.size());
    std::copy_n(hash.begin(), copy_len, key.begin());
    return key;
}

// Generate random bytes (for IV)
// In production, use OpenSSL RAND_bytes()
inline void random_bytes(std::span<uint8_t> buffer) {
    if (RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
}

} // namespace Poot::ShardEngine::Crypto
