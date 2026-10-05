#pragma once
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace vii::prepare {
// Ownership comes only from this shipped registry, never from writable install state.
inline std::set<std::string> ParseProxyRegistry(const std::string& text) {
    std::set<std::string> result;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        if (line.size() <= 67 || line.substr(64, 3) != " | " ||
            line.find_first_not_of(" \t", 67) == std::string::npos)
            throw std::runtime_error("Invalid known-proxy registry entry");
        const auto hash = line.substr(0, 64);
        if (!std::all_of(hash.begin(), hash.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })) throw std::runtime_error("Invalid known-proxy SHA-256");
        if (!result.insert(hash).second)
            throw std::runtime_error("Duplicate known-proxy SHA-256");
    }
    if (result.empty()) throw std::runtime_error("Known-proxy registry is empty");
    return result;
}
}
