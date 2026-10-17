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
    os << "SatSolv 0.3.0\n"
          "Usage: satsolv <instance.cnf> [options]\n"
          "Options:\n"
          "  --proof <path>       emit DRAT proof (required to certify UNSAT)\n"
          "  --seed <u64>         deterministic tie/phase seed (default: 1)\n"
          "  --reduce-interval <n> learned-clause reduction interval (default: 2000)\n"
          "  --time-limit <sec>   wall-clock limit; 0 means unlimited\n"
          "  --no-congruence      ablate SCC/AND-gate congruence\n"
          "  --experimental-watched-vivify enable bounded watched vivification\n"
          "  --experimental-eliminate enable clause-non-growing bounded resolution\n"
          "  --experimental-dynamic-lbd update learnt quality on reuse\n"
          "  --experimental-fixed-lbd-tier preserve birth tiers with legacy LBD\n"
          "  --experimental-rank-lbd rank by antecedent levels; fixed birth tiers\n"
          "  --experimental-target-phases guide decisions by promising trails\n"
          "  --experimental-reason-bump bounded reason-side variable activity\n"
          "  --experimental-recent-use one-round conflict-use clause protection\n"
          "  --experimental-dual-search alternate focused VMTF / stable EVSIDS\n"
          "  --dual-search-interval <n> conflicts per search mode (default: 1000)\n"
          "  --experimental-short-ssr bounded structure-preserving resolution\n"
          "  --experimental-implication-strengthen bounded binary graph strengthening\n"
          "  --strengthen-budget <n> maximum structural strengthening work\n"
          "  --strengthen-keep-activity preserve pre-strengthening occurrence priorities\n"
          "  --experimental-root-probe learn bounded watched-propagation implications\n"
          "  --probe-start <n> first probing conflict budget (default: 1000000)\n"
          "  --probe-budget <n> maximum charged probe work per round\n"
          "  --target-window <n>  initial target reset interval (default: 10000)\n"
          "  --reason-bump-budget <n> maximum inspected reason literals per conflict\n"
          "  --search-warmup <n> defer experimental search policies by conflicts\n"
          "  --search-warmup-seconds <sec> alternative elapsed activation budget\n"
          "  --eliminate-budget <n> resolution work limit\n"
          "  --vivify-budget <n>  initial watched-vivification work limit\n"
          "  --experimental-simplify enable previous bounded simplifier\n"
          "  --experimental-binary-minimize enable previous minimizer\n"
          "  --experimental-clause-decay enable previous recency policy\n"
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
        << "  \"search_mode_switches\": " << s.search_mode_switches << ",\n"
        << "  \"focused_conflicts\": " << s.focused_conflicts << ",\n"
        << "  \"stable_conflicts\": " << s.stable_conflicts << ",\n"
        << "  \"focused_decisions\": " << s.focused_decisions << ",\n"
        << "  \"stable_decisions\": " << s.stable_decisions << ",\n"
        << "  \"vmtf_bumps\": " << s.vmtf_bumps << ",\n"
        << "  \"vmtf_search_steps\": " << s.vmtf_search_steps << ",\n"
        << "  \"learnt_clauses\": " << s.learnt_clauses << ",\n"
        << "  \"deleted_learnt_clauses\": " << s.deleted_learnt_clauses << ",\n"
        << "  \"max_trail\": " << s.max_trail << ",\n"
        << "  \"proof_bytes\": " << s.proof_bytes << ",\n"
        << "  \"congruence_equivalences\": " << s.congruence_equivalences << ",\n"
        << "  \"congruence_and_gates\": " << s.congruence_and_gates << ",\n"
        << "  \"congruence_work\": " << s.congruence_work << ",\n"
        << "  \"congruence_rounds\": " << s.congruence_rounds << ",\n"
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
        << "  \"elimination_work\": " << s.elimination_work << ",\n"
        << "  \"elimination_attempts\": " << s.elimination_attempts << ",\n"
        << "  \"tautological_resolvents\": " << s.tautological_resolvents << ",\n"
        << "  \"lbd_improvements\": " << s.lbd_improvements << ",\n"
        << "  \"lbd_tier_crossings\": " << s.lbd_tier_crossings << ",\n"
        << "  \"reductions\": " << s.reductions << ",\n"
        << "  \"peak_protected_clauses\": " << s.peak_protected_clauses << ",\n"
        << "  \"peak_protected_literals\": " << s.peak_protected_literals << ",\n"
        << "  \"peak_birth_protected_clauses\": " << s.peak_birth_protected_clauses << ",\n"
        << "  \"recent_use_kept\": " << s.recent_use_kept << ",\n"
        << "  \"target_captures\": " << s.target_captures << ",\n"
        << "  \"target_phase_writes\": " << s.target_phase_writes << ",\n"
        << "  \"target_decisions\": " << s.target_decisions << ",\n"
        << "  \"target_resets\": " << s.target_resets << ",\n"
        << "  \"reason_bump_scans\": " << s.reason_bump_scans << ",\n"
        << "  \"reason_bumps\": " << s.reason_bumps << ",\n"
        << "  \"search_activations\": " << s.search_activations << ",\n"
        << "  \"strengthen_work\": " << s.strengthen_work << ",\n"
        << "  \"ssr_removed\": " << s.ssr_removed << ",\n"
        << "  \"implication_removed\": " << s.implication_removed << ",\n"
        << "  \"binary_failed_units\": " << s.binary_failed_units << ",\n"
        << "  \"probe_work\": " << s.probe_work << ",\n"
        << "  \"probe_attempts\": " << s.probe_attempts << ",\n"
        << "  \"probe_rounds\": " << s.probe_rounds << ",\n"
        << "  \"probe_units\": " << s.probe_units << ",\n"
        << "  \"probe_binaries\": " << s.probe_binaries << ",\n"
        << "  \"subsumed_clauses\": " << s.subsumed_clauses << ",\n"
        << "  \"probed_units\": " << s.probed_units << ",\n"
        << "  \"vivified_clauses\": " << s.vivified_clauses << ",\n"
        << "  \"vivify_attempts\": " << s.vivify_attempts << ",\n"
        << "  \"vivify_work\": " << s.vivify_work << ",\n"
        << "  \"vivify_removed\": " << s.vivify_removed << ",\n"
        << "  \"live_clauses\": " << s.live_clauses << ",\n"
        << "  \"clause_descriptors\": " << s.clause_descriptors << ",\n";
    const auto histogram = [&out](const char* name, const auto& bins, bool last) {
        out << "  \"" << name << "\": [";
        for (std::size_t i = 0; i < bins.size(); ++i) { if (i != 0) out << ", "; out << bins[i]; }
        out << (last ? "]\n" : "],\n");
    };
    histogram("birth_lbd_histogram", s.birth_lbd_histogram, false);
    histogram("reuse_score_histogram", s.reuse_score_histogram, true);
    out << "}\n";
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
            if (arg == "--version") { std::cout << "SatSolv 0.3.0\n"; return 0; }
            if (arg == "--proof" || arg == "--seed" || arg == "--time-limit" || arg == "--stats-json" || arg == "--reduce-interval" || arg == "--vivify-budget" || arg == "--eliminate-budget" || arg == "--target-window" || arg == "--reason-bump-budget" || arg == "--search-warmup" || arg == "--search-warmup-seconds" || arg == "--dual-search-interval" || arg == "--strengthen-budget" || arg == "--probe-start" || arg == "--probe-budget") {
                if (++i >= argc) throw std::invalid_argument("missing value for " + arg);
                std::string value = argv[i];
                if (arg == "--proof") proof = std::filesystem::path(value);
                else if (arg == "--seed") config.seed = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--reduce-interval") config.reduce_interval = integer_option<int>(value, arg);
                else if (arg == "--vivify-budget") config.vivify_budget = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--eliminate-budget") config.eliminate_budget = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--strengthen-budget") config.strengthen_budget = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--probe-start") config.probe_start = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--probe-budget") config.probe_budget = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--target-window") config.target_window = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--reason-bump-budget") config.reason_bump_budget = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--search-warmup") config.search_warmup = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--search-warmup-seconds") config.search_warmup_seconds = time_option(value);
                else if (arg == "--dual-search-interval") config.dual_search_interval = integer_option<std::uint64_t>(value, arg);
                else if (arg == "--time-limit") config.time_limit_seconds = time_option(value);
                else stats_json = std::filesystem::path(value);
                continue;
            }
            if (arg == "--no-congruence") { config.congruence = false; continue; }
            if (arg == "--experimental-watched-vivify") { config.watched_vivify = true; continue; }
            if (arg == "--experimental-eliminate") { config.bounded_eliminate = true; continue; }
            if (arg == "--experimental-dynamic-lbd") { config.dynamic_lbd = true; continue; }
            if (arg == "--experimental-fixed-lbd-tier") { config.fixed_lbd_tier = true; continue; }
            if (arg == "--experimental-rank-lbd") { config.rank_lbd = true; continue; }
            if (arg == "--experimental-target-phases") { config.target_phases = true; continue; }
            if (arg == "--experimental-reason-bump") { config.reason_bump = true; continue; }
            if (arg == "--experimental-recent-use") { config.recent_use = true; continue; }
            if (arg == "--experimental-dual-search") { config.dual_search = true; continue; }
            if (arg == "--experimental-short-ssr") { config.short_ssr = true; continue; }
            if (arg == "--experimental-implication-strengthen") { config.implication_strengthen = true; continue; }
            if (arg == "--strengthen-keep-activity") { config.strengthen_keep_activity = true; continue; }
            if (arg == "--experimental-root-probe") { config.root_probe = true; continue; }
            if (arg == "--experimental-simplify") { config.simplify = true; continue; }
            if (arg == "--experimental-binary-minimize") { config.binary_minimize = true; continue; }
            if (arg == "--experimental-clause-decay") { config.clause_decay = 0.999; continue; }
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
                      << "c search_mode_switches " << s.search_mode_switches << "\n"
                      << "c focused_conflicts " << s.focused_conflicts << "\n"
                      << "c stable_conflicts " << s.stable_conflicts << "\n"
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

