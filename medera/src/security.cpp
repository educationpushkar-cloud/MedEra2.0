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
    if (bytes == 0 || bytes > 1024) throw std::invalid_argument("Random byte count is out of range.");
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
    if (saltHex.size() != 32) throw std::invalid_argument("Password salt must contain 16 bytes.");
    const auto hexNibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    std::array<unsigned char, 16> salt{};
    for (std::size_t i = 0; i < salt.size(); ++i) {
        const int high = hexNibble(saltHex[i * 2]);
        const int low = hexNibble(saltHex[i * 2 + 1]);
        if (high < 0 || low < 0) throw std::invalid_argument("Password salt contains invalid hexadecimal data.");
        salt[i] = static_cast<unsigned char>((high << 4) | low);
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
