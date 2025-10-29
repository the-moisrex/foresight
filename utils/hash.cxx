// Created by moisrex on 10/29/25.

module;
#include <cstdint>
module foresight.utils.hash;

void foresight::fnv1a_init(std::uint32_t& hash) noexcept {
    hash = FNV1A_32_INIT;
}

void foresight::fnv1a_hash(std::uint32_t& hash, char32_t const uch) noexcept {
    hash ^= static_cast<std::uint32_t>(uch);
    hash *= FNV1A_32_PRIME;
}

void foresight::fnv1a_init(std::uint64_t& hash) noexcept {
    hash = FNV1A_64_INIT;
}

void foresight::fnv1a_hash(std::uint64_t& hash, char32_t const uch) noexcept {
    hash ^= static_cast<std::uint64_t>(uch);
    hash *= FNV1A_64_PRIME;
}
