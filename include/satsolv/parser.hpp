#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace satsolv {

struct Formula {
    int variables = 0;
    std::vector<std::vector<int>> clauses;
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

Formula parse_dimacs(const std::filesystem::path& path);
Formula parse_dimacs_text(const std::string& text);

} // namespace satsolv
