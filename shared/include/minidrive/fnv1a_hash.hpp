#pragma once

#ifndef FNV1A_HASH_HPP
#define FNV1A_HASH_HPP

#include <cstdint>
#include <string>
#include <fstream>
#include <stdexcept>

// FNV-1a 64-bit hash implementation
constexpr uint64_t fnv1a_hash(const char* data, size_t length) {
    constexpr uint64_t fnv_offset_basis = 0xcbf29ce484222325;
    constexpr uint64_t fnv_prime = 0x100000001b3;

    uint64_t hash = fnv_offset_basis;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(data[i]));
        hash *= fnv_prime;
    }
    return hash;
}

// Hashes the contents of a file
uint64_t hash_file(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + file_path);
    }

    constexpr size_t buffer_size = 4096;
    unsigned char buffer[buffer_size];
    uint64_t hash = 0xcbf29ce484222325; // FNV offset basis
    constexpr uint64_t fnv_prime = 0x100000001b3;

    while (file.read(reinterpret_cast<char*>(buffer), buffer_size) || file.gcount() > 0) {
        size_t bytes_read = file.gcount();
        for (size_t i = 0; i < bytes_read; ++i) {
            hash ^= static_cast<uint64_t>(buffer[i]);
            hash *= fnv_prime;
        }
    }

    return hash;
}

struct FileInfo {
    uint64_t hash; // Hash of the file content
    std::string type; // Type of the file (e.g., "file" or "directory")

    FileInfo() = default;

    FileInfo(uint64_t file_hash, const std::string& file_type)
        : hash(file_hash), type(file_type) {}
};

#endif // FNV1A_HASH_HPP