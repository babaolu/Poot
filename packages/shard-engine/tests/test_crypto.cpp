// test_crypto.cpp — Unit tests for AES-256-GCM and SHA-256
// C++23 — adheres to CppCoreGuidelines

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <array>

#include "shard_engine/crypto.hpp"

using namespace Poot::ShardEngine::Crypto;

TEST_CASE("SHA-256 empty string", "[crypto][sha256]") {
    std::vector<uint8_t> data = {};
    auto hash = sha256(data);
    // SHA-256 of empty string:
    // e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    REQUIRE(hash.size() == 32);
    REQUIRE(hash[0] == 0xe3);
    REQUIRE(hash[1] == 0xb0);
    REQUIRE(hash[2] == 0xc4);
    REQUIRE(hash[3] == 0x42);
    REQUIRE(hash[29] == 0x52);
    REQUIRE(hash[30] == 0xb8);
    REQUIRE(hash[31] == 0x55);
}

TEST_CASE("SHA-256 known test vector: 'abc'", "[crypto][sha256]") {
    std::string input = "abc";
    auto hash = sha256(std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(input.data()), input.size()));
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    REQUIRE(hash.size() == 32);
    REQUIRE(hash[0] == 0xba);
    REQUIRE(hash[1] == 0x78);
    REQUIRE(hash[2] == 0x16);
    REQUIRE(hash[3] == 0xbf);
}

TEST_CASE("SHA-256 is deterministic", "[crypto][sha256]") {
    std::vector<uint8_t> data(100);
    for (size_t i = 0; i < 100; ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);

    auto h1 = sha256(data);
    auto h2 = sha256(data);
    REQUIRE(h1 == h2);
}

TEST_CASE("SHA-256 different inputs give different hashes", "[crypto][sha256]") {
    std::vector<uint8_t> d1 = {1, 2, 3, 4};
    std::vector<uint8_t> d2 = {1, 2, 3, 5}; // Last byte different
    auto h1 = sha256(d1);
    auto h2 = sha256(d2);
    REQUIRE(h1 != h2);
}

TEST_CASE("AES-256-GCM encrypt then decrypt gives original", "[crypto][aes]") {
    std::array<uint8_t, 32> key{};
    for (size_t i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);

    std::vector<uint8_t> iv(12, 0);
    for (size_t i = 0; i < 12; ++i) iv[i] = static_cast<uint8_t>(i * 3 + 1);

    std::vector<uint8_t> plaintext(100, 0);
    for (size_t i = 0; i < 100; ++i) plaintext[i] = static_cast<uint8_t>(i * 17 + 42);

    // Encrypt
    auto encrypted = aes256_gcm_encrypt(key, iv, plaintext);
    REQUIRE(encrypted.has_value());
    REQUIRE(encrypted->first.size() == plaintext.size()); // Ciphertext same size
    REQUIRE(encrypted->second.size() == 16); // Tag is 16 bytes

    // Should be different from plaintext
    REQUIRE(encrypted->first != plaintext);

    // Decrypt
    auto decrypted = aes256_gcm_decrypt(key, iv, encrypted->first, encrypted->second);
    REQUIRE(decrypted.has_value());
    REQUIRE(*decrypted == plaintext);
}

TEST_CASE("AES-256-GCM wrong tag fails authentication", "[crypto][aes]") {
    std::array<uint8_t, 32> key{};
    for (size_t i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);

    std::vector<uint8_t> iv(12, 0x42);
    std::vector<uint8_t> plaintext = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto encrypted = aes256_gcm_encrypt(key, iv, plaintext);
    REQUIRE(encrypted.has_value());

    // Tamper with the tag
    auto bad_tag = encrypted->second;
    bad_tag[0] ^= 0xFF;

    auto decrypted = aes256_gcm_decrypt(key, iv, encrypted->first, bad_tag);
    REQUIRE(!decrypted.has_value()); // Should fail authentication
}

TEST_CASE("AES-256-GCM wrong key fails", "[crypto][aes]") {
    std::array<uint8_t, 32> key1{};
    std::array<uint8_t, 32> key2{};
    for (size_t i = 0; i < 32; ++i) {
        key1[i] = static_cast<uint8_t>(i);
        key2[i] = static_cast<uint8_t>(i + 50);
    }

    std::vector<uint8_t> iv(12, 0x99);
    std::vector<uint8_t> plaintext = {42, 43, 44, 45};

    auto encrypted = aes256_gcm_encrypt(key1, iv, plaintext);
    REQUIRE(encrypted.has_value());

    // Decrypt with wrong key
    auto decrypted = aes256_gcm_decrypt(key2, iv, encrypted->first, encrypted->second);
    REQUIRE(!decrypted.has_value());
}

TEST_CASE("AES-256-GCM with AAD", "[crypto][aes]") {
    std::array<uint8_t, 32> key{};
    std::vector<uint8_t> iv(12, 0x11);
    std::vector<uint8_t> plaintext = {1, 2, 3};
    std::vector<uint8_t> aad = {0xAA, 0xBB, 0xCC};

    auto encrypted = aes256_gcm_encrypt(key, iv, plaintext, aad);
    REQUIRE(encrypted.has_value());

    // Decrypt with same AAD
    auto decrypted = aes256_gcm_decrypt(key, iv, encrypted->first, encrypted->second, aad);
    REQUIRE(decrypted.has_value());
    REQUIRE(*decrypted == plaintext);

    // Decrypt with wrong AAD should fail
    std::vector<uint8_t> bad_aad = {0xAA, 0xBB, 0xDD};
    auto fail = aes256_gcm_decrypt(key, iv, encrypted->first, encrypted->second, bad_aad);
    REQUIRE(!fail.has_value());
}

TEST_CASE("derive_key_from_hash is deterministic", "[crypto][key-derivation]") {
    std::vector<uint8_t> hash1(32, 0);
    std::vector<uint8_t> hash2(32, 0);
    for (size_t i = 0; i < 32; ++i) {
        hash1[i] = static_cast<uint8_t>(i * 3);
        hash2[i] = static_cast<uint8_t>(i * 3);
    }

    auto k1 = derive_key_from_hash(hash1);
    auto k2 = derive_key_from_hash(hash2);
    REQUIRE(k1 == k2);
}

TEST_CASE("derive_key_from_hash uses first 32 bytes", "[crypto][key-derivation]") {
    std::vector<uint8_t> hash(64, 0); // Longer than 32
    for (size_t i = 0; i < 64; ++i) hash[i] = static_cast<uint8_t>(i);

    auto key = derive_key_from_hash(hash);
    for (size_t i = 0; i < 32; ++i) {
        REQUIRE(key[i] == static_cast<uint8_t>(i));
    }
}
