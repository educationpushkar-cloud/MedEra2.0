#pragma once

#include <cstddef>
#include <string>

namespace medera {

std::string randomHex(std::size_t bytes = 16);
std::string makePasswordHash(const std::string& password, const std::string& saltHex);
bool constantTimeEqual(const std::string& a, const std::string& b);

} // namespace medera
