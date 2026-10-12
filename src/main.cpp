#include "satsolv/parser.hpp"
#include "satsolv/solver.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage(std::ostream& os) {
    os << "SatSolv 0.1.0\n"
          "Usage: satsolv <instance.cnf> [options]\n"
          "Options:\n"
          "  --proof <path>       emit DRAT proof (required to certify UNSAT)\n"
          "  --seed <u64>         deterministic seed (default: 1)\n"
          "  --time-limit <sec>   wall-clock limit; 0 means unlimited\n"
          "  --stats              print human-readable statistics\n"
          "  --stats-json <path>  write stable JSON statistics\n"
          "  --version            print version\n"
          "  --help               print help\n"
          "Exit codes: 10 SAT, 20 UNSAT, 0 UNKNOWN, 2 input/usage error, 3 internal error.\n";
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

void write_stats_json(const std::filesystem::path& path,
                      const satsolv::SolveOutput& result,
                      double parse_seconds,
                      std::uint64_t seed) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot open stats JSON: " + path.string());
    const auto& s = result.stats;
    out << std::setprecision(17)
        << "{\n"
        << "  \"result\": \"" << json_escape(satsolv::result_name(result.result)) << "\",\n"
        << "  \"seed\": " << seed << ",\n"
        << "  \"parse_seconds\": " << parse_seconds << ",\n"
        << "  \"solve_seconds\": " << s.solve_seconds << ",\n"
        << "  \"conflicts\": " << s.conflicts << ",\n"
        << "  \"decisions\": " << s.decisions << ",\n"
        << "  \"propagations\": " << s.propagations << ",\n"
        << "  \"restarts\": " << s.restarts << ",\n"
        << "  \"learnt_clauses\": " << s.learnt_clauses << ",\n"
        << "  \"deleted_learnt_clauses\": " << s.deleted_learnt_clauses << ",\n"
        << "  \"max_trail\": " << s.max_trail << ",\n"
        << "  \"proof_bytes\": " << s.proof_bytes << ",\n"
        << "  \"core_count\": " << s.core_count << ",\n"
        << "  \"tier2_count\": " << s.tier2_count << ",\n"
        << "  \"local_count\": " << s.local_count << ",\n"
        << "  \"peak_live_learnt\": " << s.peak_live_learnt << ",\n"
        << "  \"peak_tier2\": " << s.peak_tier2 << ",\n"
        << "  \"promotions\": " << s.promotions << ",\n"
        << "  \"demotions\": " << s.demotions << ",\n"
        << "  \"deleted_core\": " << s.deleted_core << ",\n"
        << "  \"deleted_tier2\": " << s.deleted_tier2 << ",\n"
        << "  \"deleted_local\": " << s.deleted_local << ",\n"
        << "  \"tier2_reused_before_deletion\": " << s.tier2_reused_before_deletion << ",\n"
        << "  \"deletion_age_sum\": " << s.deletion_age_sum << ",\n"
        << "  \"reductions\": " << s.reductions << ",\n"
        << "  \"peak_arena_literals\": " << s.peak_arena_literals << ",\n"
        << "  \"peak_live_watchers\": " << s.peak_live_watchers << ",\n"
        << "  \"live_watchers\": " << s.live_watchers << ",\n"
        << "  \"database_bytes\": " << s.database_bytes << ",\n"
        << "  \"average_deletion_age\": " << (s.deleted_learnt_clauses ? static_cast<double>(s.deletion_age_sum) / static_cast<double>(s.deleted_learnt_clauses) : 0.0) << "\n"
        << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc <= 1) {
            usage(std::cerr);
            return 2;
        }
        std::filesystem::path input;
        std::optional<std::filesystem::path> proof;
        std::optional<std::filesystem::path> stats_json;
        satsolv::SolverConfig config;
        bool show_stats = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help") { usage(std::cout); return 0; }
            if (arg == "--version") { std::cout << "SatSolv 0.1.0\n"; return 0; }
            if (arg == "--proof" || arg == "--seed" || arg == "--time-limit" || arg == "--stats-json") {
                if (++i >= argc) throw std::invalid_argument("missing value for " + arg);
                std::string value = argv[i];
                if (arg == "--proof") proof = std::filesystem::path(value);
                else if (arg == "--seed") config.seed = std::stoull(value);
                else if (arg == "--time-limit") config.time_limit_seconds = std::stod(value);
                else stats_json = std::filesystem::path(value);
                continue;
            }
            if (arg == "--stats") { show_stats = true; continue; }
            if (!arg.empty() && arg[0] == '-') throw std::invalid_argument("unknown option: " + arg);
            if (!input.empty()) throw std::invalid_argument("multiple input files supplied");
            input = arg;
        }
        if (input.empty()) throw std::invalid_argument("missing CNF input path");

        const auto parse_start = std::chrono::steady_clock::now();
        const satsolv::Formula formula = satsolv::parse_dimacs(input);
        const double parse_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - parse_start).count();

        satsolv::Solver solver(config);
        const satsolv::SolveOutput result = solver.solve(formula, proof);
        if (result.result == satsolv::SolveResult::Sat) {
            std::cout << "s SATISFIABLE\n";
            std::cout << "v";
            std::size_t column = 1;
            for (int lit : result.model) {
                const std::string token = " " + std::to_string(lit);
                if (column + token.size() > 78) {
                    std::cout << " 0\nv";
                    column = 1;
                }
                std::cout << token;
                column += token.size();
            }
            std::cout << " 0\n";
        } else if (result.result == satsolv::SolveResult::Unsat) {
            std::cout << "s UNSATISFIABLE\n";
        } else {
            std::cout << "s UNKNOWN\n";
        }

        if (show_stats) {
            const auto& s = result.stats;
            std::cerr << std::fixed << std::setprecision(6)
                      << "c parse_seconds " << parse_seconds << "\n"
                      << "c solve_seconds " << s.solve_seconds << "\n"
                      << "c conflicts " << s.conflicts << "\n"
                      << "c decisions " << s.decisions << "\n"
                      << "c propagations " << s.propagations << "\n"
                      << "c restarts " << s.restarts << "\n"
                      << "c learnt_clauses " << s.learnt_clauses << "\n"
                      << "c deleted_learnt_clauses " << s.deleted_learnt_clauses << "\n"
                      << "c max_trail " << s.max_trail << "\n"
                      << "c proof_bytes " << s.proof_bytes << "\n";
        }
        if (stats_json) write_stats_json(*stats_json, result, parse_seconds, config.seed);

        if (result.result == satsolv::SolveResult::Sat) return 10;
        if (result.result == satsolv::SolveResult::Unsat) return 20;
        return 0;
    } catch (const satsolv::ParseError& e) {
        std::cerr << "c PARSE_ERROR: " << e.what() << "\n";
        return 2;
    } catch (const std::invalid_argument& e) {
        std::cerr << "c USAGE_ERROR: " << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "c INTERNAL_ERROR: " << e.what() << "\n";
        return 3;
    }
}

