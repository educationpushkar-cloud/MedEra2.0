#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "medera/security.hpp"

namespace medera {

std::string randomHex(std::size_t bytes) {
    std::vector<unsigned char> data(bytes);
    if (BCryptGenRandom(nullptr, data.data(), static_cast<ULONG>(data.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw std::runtime_error("Secure random number generation failed.");
    }
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(bytes * 2);
    for (unsigned char byte : data) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

std::string makePasswordHash(const std::string& password, const std::string& saltHex) {
    std::vector<unsigned char> salt;
    for (std::size_t i = 0; i + 1 < saltHex.size(); i += 2) {
        salt.push_back(static_cast<unsigned char>(std::stoul(saltHex.substr(i, 2), nullptr, 16)));
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) {
        throw std::runtime_error("Password hashing is unavailable.");
    }
    std::array<unsigned char, 32> output{};
    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        algorithm, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
        salt.data(), static_cast<ULONG>(salt.size()), 180000, output.data(), static_cast<ULONG>(output.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status != 0) throw std::runtime_error("Password hashing failed.");
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(output.size() * 2);
    for (unsigned char byte : output) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

bool constantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}

} // namespace medera
