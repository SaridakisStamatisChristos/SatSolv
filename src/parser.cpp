#include "satsolv/parser.hpp"

#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>

namespace satsolv {
namespace {

int parse_int_token(std::string_view token, std::size_t line_no) {
    if (token.empty()) throw ParseError("empty integer token on line " + std::to_string(line_no));
    long long value = 0;
    const char* begin = token.data();
    const char* end = token.data() + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        throw ParseError("invalid integer token '" + std::string(token) + "' on line " + std::to_string(line_no));
    }
    if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
        value > static_cast<long long>(std::numeric_limits<int>::max())) {
        throw ParseError("integer out of range on line " + std::to_string(line_no));
    }
    return static_cast<int>(value);
}

Formula parse_stream(std::istream& in) {
    Formula out;
    bool header_seen = false;
    int declared_clauses = -1;
    std::vector<int> current;
    std::string line;
    std::size_t line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        std::size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        if (line[first] == 'c') continue;
        if (line[first] == 'p') {
            if (header_seen) throw ParseError("duplicate DIMACS header on line " + std::to_string(line_no));
            std::istringstream hs(line.substr(first));
            std::string p, kind, extra;
            long long vars = -1, clauses = -1;
            if (!(hs >> p >> kind >> vars >> clauses) || p != "p" || kind != "cnf" || (hs >> extra)) {
                throw ParseError("malformed DIMACS header on line " + std::to_string(line_no));
            }
            if (vars < 0 || vars > std::numeric_limits<int>::max() || clauses < 0 || clauses > std::numeric_limits<int>::max()) {
                throw ParseError("DIMACS header counts out of range on line " + std::to_string(line_no));
            }
            out.variables = static_cast<int>(vars);
            declared_clauses = static_cast<int>(clauses);
            header_seen = true;
            continue;
        }
        if (!header_seen) throw ParseError("clause data before DIMACS header on line " + std::to_string(line_no));

        std::istringstream ls(line.substr(first));
        std::string token;
        while (ls >> token) {
            int lit = parse_int_token(token, line_no);
            if (lit == 0) {
                out.clauses.push_back(current);
                current.clear();
                if (static_cast<int>(out.clauses.size()) > declared_clauses) {
                    throw ParseError("more clauses than declared in header");
                }
                continue;
            }
            if (lit == std::numeric_limits<int>::min()) throw ParseError("literal magnitude overflow");
            int v = lit < 0 ? -lit : lit;
            if (v == 0 || v > out.variables) {
                throw ParseError("literal variable outside declared bounds on line " + std::to_string(line_no));
            }
            current.push_back(lit);
        }
    }

    if (!header_seen) throw ParseError("missing DIMACS header");
    if (!current.empty()) throw ParseError("unterminated final clause (missing 0)");
    if (static_cast<int>(out.clauses.size()) != declared_clauses) {
        throw ParseError("clause count mismatch: declared " + std::to_string(declared_clauses) +
                         ", parsed " + std::to_string(out.clauses.size()));
    }
    return out;
}

} // namespace

Formula parse_dimacs(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw ParseError("cannot open CNF file: " + path.string());
    return parse_stream(in);
}

Formula parse_dimacs_text(const std::string& text) {
    std::istringstream in(text);
    return parse_stream(in);
}

} // namespace satsolv
