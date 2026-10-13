#include "satsolv/parser.hpp"
#include "satsolv/solver.hpp"

#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

template<class T> T integer_option(const std::string& value, const std::string& option) {
    T number{};
    const auto [end, error] = std::from_chars(value.data(), value.data()+value.size(), number);
    if (error != std::errc{} || end != value.data()+value.size()) throw std::invalid_argument("invalid integer for " + option);
    return number;
}
double time_option(const std::string& value) {
    try {
        std::size_t consumed = 0; const double number = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(number) || number < 0) throw std::invalid_argument("invalid time limit");
        return number;
    } catch (const std::out_of_range&) { throw std::invalid_argument("time limit out of range"); }
}

void usage(std::ostream& os) {
    os << "SatSolv 0.2.0\n"
          "Usage: satsolv <instance.cnf> [options]\n"
          "Options:\n"
          "  --proof <path>       emit DRAT proof (required to certify UNSAT)\n"
          "  --seed <u64>         deterministic tie/phase seed (default: 1)\n"
          "  --reduce-interval <n> learned-clause reduction interval (default: 2000)\n"
          "  --time-limit <sec>   wall-clock limit; 0 means unlimited\n"
          "  --no-simplify        ablate bounded preprocessing\n"
          "  --no-binary-minimize ablate binary implication minimization\n"
          "  --no-clause-decay    ablate recency-weighted clause activity\n"
          "  --telemetry          count propagation and heap events\n"
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
        << "  \"watch_visits\": " << s.watch_visits << ",\n"
        << "  \"blocker_hits\": " << s.blocker_hits << ",\n"
        << "  \"descriptor_scans\": " << s.descriptor_scans << ",\n"
        << "  \"descriptor_compactions\": " << s.descriptor_compactions << ",\n"
        << "  \"heap_updates\": " << s.heap_updates << ",\n"
        << "  \"peak_heap_entries\": " << s.peak_heap_entries << ",\n"
        << "  \"binary_edges_scanned\": " << s.binary_edges_scanned << ",\n"
        << "  \"binary_literals_removed\": " << s.binary_literals_removed << ",\n"
        << "  \"simplified_literals\": " << s.simplified_literals << ",\n"
        << "  \"eliminated_variables\": " << s.eliminated_variables << ",\n"
        << "  \"subsumed_clauses\": " << s.subsumed_clauses << ",\n"
        << "  \"probed_units\": " << s.probed_units << ",\n"
        << "  \"vivified_clauses\": " << s.vivified_clauses << ",\n"
        << "  \"live_clauses\": " << s.live_clauses << ",\n"
        << "  \"clause_descriptors\": " << s.clause_descriptors << "\n"
        << "}\n";
    out.flush();
    if (!out) throw std::runtime_error("stats JSON output failed");
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
            if (arg == "--version") { std::cout << "SatSolv 0.2.0\n"; return 0; }
            if (arg == "--proof" || arg == "--seed" || arg == "--time-limit" || arg == "--stats-json" || arg == "--reduce-interval") {
                if (++i >= argc) throw std::invalid_argument("missing value for " + arg);
                std::string value = argv[i];
                if (arg == "--proof") proof = std::filesystem::path(value);
                else if (arg == "--seed") config.seed = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--reduce-interval") config.reduce_interval = integer_option<int>(value, arg);
                else if (arg == "--time-limit") config.time_limit_seconds = time_option(value);
                else stats_json = std::filesystem::path(value);
                continue;
            }
            if (arg == "--no-simplify") { config.simplify = false; continue; }
            if (arg == "--no-binary-minimize") { config.binary_minimize = false; continue; }
            if (arg == "--no-clause-decay") { config.clause_decay = 1.0; continue; }
            if (arg == "--telemetry") { config.telemetry = true; continue; }
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

