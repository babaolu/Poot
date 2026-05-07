// reed_solomon.hpp — GF(2^8) arithmetic and Reed-Solomon erasure coding
// C++23 — adheres to CppCoreGuidelines
// Uses Vandermonde matrix encoding with Gaussian elimination decoding

#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <expected>
#include <span>
#include <algorithm>
#include <numeric>

namespace CloudMine::ShardEngine {

// clang-format off
// GF(2^8) with primitive polynomial 0x11d (x^8 + x^4 + x^3 + x^2 + 1)
namespace detail {
    // Runtime-computed tables (Meyers singleton pattern)
    inline auto& get_exp_table() {
        static std::array<uint8_t, 512> exp{};
        static bool initialized = false;
        if (!initialized) {
            uint8_t x = 1;
            for (int i = 0; i < 255; ++i) {
                exp[i] = x;
                exp[i + 255] = x;
                if (x & 0x80) {
                    x = (x << 1) ^ static_cast<uint8_t>(0x11d);
                } else {
                    x = x << 1;
                }
            }
            exp[255] = exp[0]; // x^255 = 1
            initialized = true;
        }
        return exp;
    }

    inline auto& get_log_table() {
        static std::array<uint8_t, 256> log{};
        static bool initialized = false;
        if (!initialized) {
            auto& exp = get_exp_table();
            for (int i = 0; i < 255; ++i) {
                log[exp[i]] = static_cast<uint8_t>(i);
            }
            log[0] = 0; // Convention
            initialized = true;
        }
        return log;
    }
}

class GF256 final {
public:
    [[nodiscard]] static auto mul(uint8_t a, uint8_t b) -> uint8_t {
        if (a == 0 || b == 0) return 0;
        auto& log = detail::get_log_table();
        auto& exp = detail::get_exp_table();
        return exp[log[a] + log[b]];
    }

    [[nodiscard]] static auto inv(uint8_t a) -> uint8_t {
        if (a == 0) throw std::invalid_argument("Cannot invert 0 in GF(2^8)");
        auto& log = detail::get_log_table();
        auto& exp = detail::get_exp_table();
        return exp[255 - log[a]];
    }

    [[nodiscard]] static auto pow(uint8_t a, int n) -> uint8_t {
        if (a == 0) return (n == 0) ? 1 : 0;
        auto& log = detail::get_log_table();
        auto& exp = detail::get_exp_table();
        return exp[(static_cast<int>(log[a]) * n) % 255];
    }

    [[nodiscard]] static auto gf_exp(int n) -> uint8_t {
        auto& exp = detail::get_exp_table();
        return exp[n % 255];
    }

    [[nodiscard]] static auto gf_log(uint8_t a) -> uint8_t {
        if (a == 0) return 0;
        auto& log = detail::get_log_table();
        return log[a];
    }
};

// clang-format on

// Reed-Solomon codec using Vandermonde matrix
class ReedSolomon {
public:
    using ByteMatrix = std::vector<std::vector<uint8_t>>;

    ReedSolomon() = default;
    ReedSolomon(const ReedSolomon&) = delete;
    auto operator=(const ReedSolomon&) -> ReedSolomon& = delete;
    ~ReedSolomon() = default;

    // Encode: given data shards, produce parity shards
    [[nodiscard]] auto encode(
        std::span<const std::vector<uint8_t>> data,
        size_t data_shards,
        size_t parity_shards) const
        -> std::expected<std::vector<std::vector<uint8_t>>, std::string>
    {
        if (data.size() != data_shards) {
            return std::unexpected("Data shard count mismatch: expected " +
                                   std::to_string(data_shards) + ", got " +
                                   std::to_string(data.size()));
        }
        if (data_shards == 0 || parity_shards == 0) {
            return std::unexpected("Shard counts must be positive");
        }

        // Verify all shards have same size
        size_t shard_size = data[0].size();
        for (size_t i = 1; i < data.size(); ++i) {
            if (data[i].size() != shard_size) {
                return std::unexpected("All data shards must have the same size");
            }
        }

        // Build Vandermonde matrix V of size (data_shards + parity_shards) x data_shards
        ByteMatrix V(data_shards + parity_shards, std::vector<uint8_t>(data_shards, 0));
        for (size_t i = 0; i < data_shards + parity_shards; ++i) {
            for (size_t j = 0; j < data_shards; ++j) {
                V[i][j] = GF256::gf_exp(static_cast<int>(i * j)); // i^j in GF(2^8)
            }
        }

        // Encode: for each parity shard i (i >= data_shards), compute:
        // parity[i-data_shards][byte] = sum over j: V[i][j] * data[j][byte]
        std::vector<std::vector<uint8_t>> parity(parity_shards);
        for (size_t p = 0; p < parity_shards; ++p) {
            size_t row = data_shards + p;
            parity[p].resize(shard_size, 0);
            for (size_t b = 0; b < shard_size; ++b) {
                uint8_t acc = 0;
                for (size_t j = 0; j < data_shards; ++j) {
                    uint8_t term = GF256::mul(V[row][j], data[j][b]);
                    acc ^= term; // Addition in GF(2^8) is XOR
                }
                parity[p][b] = acc;
            }
        }

        return parity;
    }

    // Decode: given any data_shards received shards, reconstruct the original data
    [[nodiscard]] auto decode(
        std::span<const std::vector<uint8_t>> received,
        std::span<const size_t> indices,
        size_t data_shards) const
        -> std::expected<std::vector<std::vector<uint8_t>>, std::string>
    {
        if (received.size() != indices.size()) {
            return std::unexpected("Received shards count != indices count");
        }
        if (received.size() < data_shards) {
            return std::unexpected("Need at least " + std::to_string(data_shards) +
                                   " shards, got " + std::to_string(received.size()));
        }

        size_t shard_size = received[0].size();
        for (size_t i = 1; i < received.size(); ++i) {
            if (received[i].size() != shard_size) {
                return std::unexpected("All received shards must have the same size");
            }
        }

        // Build matrix B: k x k
        // For each received shard at index indices[i]:
        //   If indices[i] < data_shards: identity row (1 at position indices[i])
        //   Else: Vandermonde row [gf_exp(indices[i]*0), ..., gf_exp(indices[i]*(k-1))]
        size_t k = std::min(data_shards, received.size());
        ByteMatrix B(k, std::vector<uint8_t>(k, 0));
        for (size_t i = 0; i < k; ++i) {
            if (indices[i] < data_shards) {
                // Data shard: identity row
                B[i][indices[i]] = 1;
            } else {
                // Parity shard: Vandermonde row
                for (size_t j = 0; j < k; ++j) {
                    B[i][j] = GF256::gf_exp(static_cast<int>(indices[i] * j));
                }
            }
        }

        // Invert matrix B using Gaussian elimination in GF(2^8)
        auto inv_B = gauss_jordan_inverse(B);
        if (!inv_B.has_value()) {
            return std::unexpected("Failed to invert matrix");
        }

        // Reconstruct: D[j][byte] = sum_i inv_B[j][i] * received[i][byte]
        std::vector<std::vector<uint8_t>> reconstructed(data_shards);
        for (size_t j = 0; j < data_shards; ++j) {
            reconstructed[j].resize(shard_size, 0);
            for (size_t b = 0; b < shard_size; ++b) {
                uint8_t acc = 0;
                for (size_t i = 0; i < k; ++i) {
                    uint8_t term = GF256::mul((*inv_B)[j][i], received[i][b]);
                    acc ^= term;
                }
                reconstructed[j][b] = acc;
            }
        }

        return reconstructed;
    }

private:
    // Gaussian-Jordan elimination in GF(2^8) to invert a k x k matrix
    [[nodiscard]] static auto gauss_jordan_inverse(const ByteMatrix& M)
        -> std::expected<ByteMatrix, std::string>
    {
        size_t k = M.size();
        if (k == 0) return std::unexpected("Cannot invert empty matrix");

        // Create augmented matrix [M | I]
        ByteMatrix aug(k, std::vector<uint8_t>(2 * k, 0));
        for (size_t i = 0; i < k; ++i) {
            for (size_t j = 0; j < k; ++j) {
                aug[i][j] = M[i][j];
            }
            // Set identity in right half
            for (size_t j = 0; j < k; ++j) {
                aug[i][k + j] = (i == j) ? 1 : 0;
            }
        }

        // Forward elimination
        for (size_t col = 0; col < k; ++col) {
            // Find pivot
            size_t pivot = k;
            for (size_t row = col; row < k; ++row) {
                if (aug[row][col] != 0) {
                    pivot = row;
                    break;
                }
            }
            if (pivot == k) {
                return std::unexpected("Matrix is singular, cannot invert");
            }

            // Swap rows
            if (pivot != col) {
                std::swap(aug[col], aug[pivot]);
            }

            // Scale pivot row
            uint8_t pivot_val = aug[col][col];
            uint8_t pivot_inv = GF256::inv(pivot_val);
            for (size_t j = 0; j < 2 * k; ++j) {
                aug[col][j] = GF256::mul(aug[col][j], pivot_inv);
            }

            // Eliminate other rows
            for (size_t row = 0; row < k; ++row) {
                if (row == col) continue;
                uint8_t factor = aug[row][col];
                if (factor == 0) continue;
                for (size_t j = 0; j < 2 * k; ++j) {
                    aug[row][j] ^= GF256::mul(factor, aug[col][j]);
                }
            }
        }

        // Extract inverse from right half
        ByteMatrix inv(k, std::vector<uint8_t>(k, 0));
        for (size_t i = 0; i < k; ++i) {
            for (size_t j = 0; j < k; ++j) {
                inv[i][j] = aug[i][k + j];
            }
        }

        return inv;
    }
};

} // namespace CloudMine::ShardEngine
