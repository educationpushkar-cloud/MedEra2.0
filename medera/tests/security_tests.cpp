#include "medera/security.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "PASS  " << name << '\n';
    } else {
        std::cerr << "FAIL  " << name << '\n';
        ++failures;
    }
}

bool isLowerHex(const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}

template <typename Function>
bool throwsInvalidArgument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}
} // namespace

int main() {
    const auto token = medera::randomHex(32);
    expect(token.size() == 64 && isLowerHex(token),
           "Secure random token has the requested length and lowercase hex encoding");
    expect(throwsInvalidArgument([] { medera::randomHex(0); }),
           "Random generator rejects an empty token request");
    expect(throwsInvalidArgument([] { medera::randomHex(1025); }),
           "Random generator rejects an excessive token request");

    const std::string password = "correct-horse-battery";
    const std::string salt = "00112233445566778899aabbccddeeff";
    const auto hash = medera::makePasswordHash(password, salt);
    expect(hash.size() == 64 && isLowerHex(hash),
           "PBKDF2 password hash is a 256-bit lowercase hex value");
    expect(hash == medera::makePasswordHash(password, salt),
           "The same password and salt produce the same password hash");
    expect(hash != medera::makePasswordHash(password, "ffeeddccbbaa99887766554433221100"),
           "Changing the salt changes the password hash");
    expect(hash != medera::makePasswordHash("different-password", salt),
           "Changing the password changes the password hash");
    expect(throwsInvalidArgument([&] { medera::makePasswordHash(password, "1234"); }),
           "Password hashing rejects a salt with the wrong length");
    expect(throwsInvalidArgument([&] { medera::makePasswordHash(password, "00112233445566778899aabbccddeefg"); }),
           "Password hashing rejects non-hexadecimal salt data");

    expect(medera::constantTimeEqual(hash, hash),
           "Constant-time comparison accepts identical hashes");
    expect(!medera::constantTimeEqual(hash, std::string(64, '0')),
           "Constant-time comparison rejects different same-length values");
    expect(!medera::constantTimeEqual(hash, "short"),
           "Constant-time comparison rejects values with different lengths");

    if (failures != 0) {
        std::cerr << failures << " security test(s) failed.\n";
        return 1;
    }
    std::cout << "All security tests passed.\n";
    return 0;
}
