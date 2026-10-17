#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}\n--- needle ---\n{old}")
    p.write_text(text.replace(old, new, 1))


# src/solver_v2.cpp
replace_once(
    "src/solver_v2.cpp",
    '''    if (config_.target_phases && config_.target_window == 0)\n        throw std::invalid_argument("target phase window must be positive");\n    if (!(config_.var_decay > 0.0 && config_.var_decay < 1.0)) {\n''',
    '''    if (config_.target_phases && config_.target_window == 0)\n        throw std::invalid_argument("target phase window must be positive");\n    if (config_.dual_search && config_.dual_search_interval == 0)\n        throw std::invalid_argument("dual-search interval must be positive");\n    if (!(config_.var_decay > 0.0 && config_.var_decay < 1.0)) {\n''')

replace_once(
    "src/solver_v2.cpp",
    '''    reason_bump_epoch_ = 0;\n    search_active_ = config_.search_warmup == 0 && config_.search_warmup_seconds == 0.0;\n    order_heap_.reset(activity_, heap_rank_);\n    var_inc_ = 1.0;\n''',
    '''    reason_bump_epoch_ = 0;\n    search_active_ = config_.search_warmup == 0 && config_.search_warmup_seconds == 0.0;\n    search_mode_ = config_.dual_search ? SearchMode::Focused : SearchMode::Stable;\n    next_mode_switch_ = config_.dual_search ? config_.dual_search_interval : std::numeric_limits<std::uint64_t>::max();\n    order_heap_.reset(activity_, heap_rank_);\n    if (config_.dual_search) vmtf_queue_.reset(activity_, heap_rank_);\n    else vmtf_queue_.clear();\n    var_inc_ = 1.0;\n''')

replace_once(
    "src/solver_v2.cpp",
    '''        reasons_[static_cast<std::size_t>(v)] = -1;\n        if (decision_enabled_[static_cast<std::size_t>(v)] != 0) order_heap_.update(v);\n    }\n''',
    '''        reasons_[static_cast<std::size_t>(v)] = -1;\n        if (decision_enabled_[static_cast<std::size_t>(v)] != 0) {\n            order_heap_.update(v);\n            if (config_.dual_search) vmtf_queue_.note_unassigned(v);\n        }\n    }\n''')

replace_once(
    "src/solver_v2.cpp",
    '''int Solver::pick_branch_lit() {\n    while (!order_heap_.empty()) {\n        const int v = order_heap_.pop();\n        if (assigns_[static_cast<std::size_t>(v)] != -1 || decision_enabled_[static_cast<std::size_t>(v)] == 0) continue;\n        if (config_.target_phases && search_active_ && target_phase_[static_cast<std::size_t>(v)] >= 0) {\n            ++stats_.target_decisions;\n            return target_phase_[static_cast<std::size_t>(v)] != 0 ? v : -v;\n        }\n        return saved_phase_[static_cast<std::size_t>(v)] != 0 ? v : -v;\n    }\n    return 0;\n}\n''',
    '''int Solver::pick_branch_lit() {\n    int v = 0;\n    if (config_.dual_search && search_mode_ == SearchMode::Focused) {\n        std::uint64_t inspected = 0;\n        v = vmtf_queue_.pick(assigns_, decision_enabled_, inspected);\n        stats_.vmtf_search_steps += inspected;\n    } else {\n        while (!order_heap_.empty()) {\n            const int candidate = order_heap_.pop();\n            if (assigns_[static_cast<std::size_t>(candidate)] != -1 ||\n                decision_enabled_[static_cast<std::size_t>(candidate)] == 0) continue;\n            v = candidate;\n            break;\n        }\n    }\n    if (v == 0) return 0;\n    if (config_.dual_search) {\n        if (search_mode_ == SearchMode::Focused) ++stats_.focused_decisions;\n        else ++stats_.stable_decisions;\n    }\n    if (config_.target_phases && search_active_ && target_phase_[static_cast<std::size_t>(v)] >= 0) {\n        ++stats_.target_decisions;\n        return target_phase_[static_cast<std::size_t>(v)] != 0 ? v : -v;\n    }\n    return saved_phase_[static_cast<std::size_t>(v)] != 0 ? v : -v;\n}\n''')

replace_once(
    "src/solver_v2.cpp",
    '''void Solver::maybe_activate_search() {\n    if (search_active_ || !(config_.rank_lbd || config_.dynamic_lbd || config_.target_phases || config_.reason_bump || config_.recent_use)) return;\n    const bool conflict_budget = config_.search_warmup > 0 && stats_.conflicts >= config_.search_warmup;\n    const bool time_budget = config_.search_warmup_seconds > 0.0 &&\n        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count() >= config_.search_warmup_seconds;\n    if (conflict_budget || time_budget) activate_search();\n}\n\nint Solver::compute_antecedent_lbd''',
    '''void Solver::maybe_activate_search() {\n    if (search_active_ || !(config_.rank_lbd || config_.dynamic_lbd || config_.target_phases || config_.reason_bump || config_.recent_use)) return;\n    const bool conflict_budget = config_.search_warmup > 0 && stats_.conflicts >= config_.search_warmup;\n    const bool time_budget = config_.search_warmup_seconds > 0.0 &&\n        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count() >= config_.search_warmup_seconds;\n    if (conflict_budget || time_budget) activate_search();\n}\n\nvoid Solver::maybe_switch_search_mode() {\n    if (!config_.dual_search || stats_.conflicts < next_mode_switch_) return;\n    search_mode_ = search_mode_ == SearchMode::Focused ? SearchMode::Stable : SearchMode::Focused;\n    ++stats_.search_mode_switches;\n    const auto limit = std::numeric_limits<std::uint64_t>::max();\n    next_mode_switch_ = config_.dual_search_interval > limit - stats_.conflicts\n        ? limit\n        : stats_.conflicts + config_.dual_search_interval;\n}\n\nint Solver::compute_antecedent_lbd''')

replace_once(
    "src/solver_v2.cpp",
    '''void Solver::bump_variable(int variable) {\n    activity_[static_cast<std::size_t>(variable)] += var_inc_;\n    if (activity_[static_cast<std::size_t>(variable)] > 1e100) rescale_activities();\n    if (decision_enabled_[static_cast<std::size_t>(variable)] != 0) order_heap_.update(variable);\n    if (config_.telemetry) {\n''',
    '''void Solver::bump_variable(int variable) {\n    activity_[static_cast<std::size_t>(variable)] += var_inc_;\n    if (activity_[static_cast<std::size_t>(variable)] > 1e100) rescale_activities();\n    if (decision_enabled_[static_cast<std::size_t>(variable)] != 0) {\n        order_heap_.update(variable);\n        if (config_.dual_search) {\n            vmtf_queue_.bump(variable); ++stats_.vmtf_bumps;\n        }\n    }\n    if (config_.telemetry) {\n''')

replace_once(
    "src/solver_v2.cpp",
    '''    for (int v = 1; v <= nvars_; ++v) {\n        if (decision_enabled_[static_cast<std::size_t>(v)] != 0) order_heap_.update(v);\n    }\n\n    SolveOutput output;\n''',
    '''    for (int v = 1; v <= nvars_; ++v) {\n        if (decision_enabled_[static_cast<std::size_t>(v)] != 0) order_heap_.update(v);\n    }\n    if (config_.dual_search) vmtf_queue_.reset(activity_, heap_rank_);\n\n    SolveOutput output;\n''')

replace_once(
    "src/solver_v2.cpp",
    '''                if (conflict >= 0) {\n                    ++stats_.conflicts;\n                    ++conflicts_since_restart_;\n                    if (decision_level() == 0) {\n''',
    '''                if (conflict >= 0) {\n                    ++stats_.conflicts;\n                    ++conflicts_since_restart_;\n                    if (config_.dual_search) {\n                        if (search_mode_ == SearchMode::Focused) ++stats_.focused_conflicts;\n                        else ++stats_.stable_conflicts;\n                    }\n                    if (decision_level() == 0) {\n''')

replace_once(
    "src/solver_v2.cpp",
    '''                        if (config_.watched_vivify && stats_.conflicts >= next_vivify_) {\n                            next_vivify_ *= 2;\n                            conflict = propagate();\n                            if (conflict == -1) conflict = vivify_database(config_.vivify_budget / 16);\n                            if (conflict == -2) { output.result = SolveResult::Unknown; break; }\n                            if (conflict >= 0) { log_empty(); output.result = SolveResult::Unsat; break; }\n                        }\n                    }\n                    continue;\n''',
    '''                        if (config_.watched_vivify && stats_.conflicts >= next_vivify_) {\n                            next_vivify_ *= 2;\n                            conflict = propagate();\n                            if (conflict == -1) conflict = vivify_database(config_.vivify_budget / 16);\n                            if (conflict == -2) { output.result = SolveResult::Unknown; break; }\n                            if (conflict >= 0) { log_empty(); output.result = SolveResult::Unsat; break; }\n                        }\n                    }\n                    maybe_switch_search_mode();\n                    continue;\n''')

# src/main.cpp
replace_once(
    "src/main.cpp",
    '''          "  --experimental-reason-bump bounded reason-side variable activity\\n"\n          "  --experimental-recent-use one-round conflict-use clause protection\\n"\n          "  --experimental-short-ssr bounded structure-preserving resolution\\n"\n''',
    '''          "  --experimental-reason-bump bounded reason-side variable activity\\n"\n          "  --experimental-recent-use one-round conflict-use clause protection\\n"\n          "  --experimental-dual-search alternate focused VMTF / stable EVSIDS\\n"\n          "  --dual-search-interval <n> conflicts per search mode (default: 1000)\\n"\n          "  --experimental-short-ssr bounded structure-preserving resolution\\n"\n''')

replace_once(
    "src/main.cpp",
    '''        << "  \\\"propagations\\\": " << s.propagations << ",\\n"\n        << "  \\\"restarts\\\": " << s.restarts << ",\\n"\n        << "  \\\"learnt_clauses\\\": " << s.learnt_clauses << ",\\n"\n''',
    '''        << "  \\\"propagations\\\": " << s.propagations << ",\\n"\n        << "  \\\"restarts\\\": " << s.restarts << ",\\n"\n        << "  \\\"search_mode_switches\\\": " << s.search_mode_switches << ",\\n"\n        << "  \\\"focused_conflicts\\\": " << s.focused_conflicts << ",\\n"\n        << "  \\\"stable_conflicts\\\": " << s.stable_conflicts << ",\\n"\n        << "  \\\"focused_decisions\\\": " << s.focused_decisions << ",\\n"\n        << "  \\\"stable_decisions\\\": " << s.stable_decisions << ",\\n"\n        << "  \\\"vmtf_bumps\\\": " << s.vmtf_bumps << ",\\n"\n        << "  \\\"vmtf_search_steps\\\": " << s.vmtf_search_steps << ",\\n"\n        << "  \\\"learnt_clauses\\\": " << s.learnt_clauses << ",\\n"\n''')

replace_once(
    "src/main.cpp",
    ''' || arg == "--search-warmup" || arg == "--search-warmup-seconds" || arg == "--strengthen-budget"''',
    ''' || arg == "--search-warmup" || arg == "--search-warmup-seconds" || arg == "--dual-search-interval" || arg == "--strengthen-budget"''')

replace_once(
    "src/main.cpp",
    '''                else if (arg == "--search-warmup-seconds") config.search_warmup_seconds = time_option(value);\n                else if (arg == "--time-limit") config.time_limit_seconds = time_option(value);\n''',
    '''                else if (arg == "--search-warmup-seconds") config.search_warmup_seconds = time_option(value);\n                else if (arg == "--dual-search-interval") config.dual_search_interval = integer_option<std::uint64_t>(value, arg);\n                else if (arg == "--time-limit") config.time_limit_seconds = time_option(value);\n''')

replace_once(
    "src/main.cpp",
    '''            if (arg == "--experimental-recent-use") { config.recent_use = true; continue; }\n            if (arg == "--experimental-short-ssr") { config.short_ssr = true; continue; }\n''',
    '''            if (arg == "--experimental-recent-use") { config.recent_use = true; continue; }\n            if (arg == "--experimental-dual-search") { config.dual_search = true; continue; }\n            if (arg == "--experimental-short-ssr") { config.short_ssr = true; continue; }\n''')

replace_once(
    "src/main.cpp",
    '''                      << "c propagations " << s.propagations << "\\n"\n                      << "c restarts " << s.restarts << "\\n"\n                      << "c learnt_clauses " << s.learnt_clauses << "\\n";\n''',
    '''                      << "c propagations " << s.propagations << "\\n"\n                      << "c restarts " << s.restarts << "\\n"\n                      << "c search_mode_switches " << s.search_mode_switches << "\\n"\n                      << "c focused_conflicts " << s.focused_conflicts << "\\n"\n                      << "c stable_conflicts " << s.stable_conflicts << "\\n"\n                      << "c learnt_clauses " << s.learnt_clauses << "\\n";\n''')

# tests/test_search.cpp
replace_once(
    "tests/test_search.cpp",
    '''    static void root_probing() {\n''',
    '''    static void dual_search_modes() {\n        SolverConfig config; config.dual_search = true; config.dual_search_interval = 2;\n        Solver solver(config); solver.reset(4);\n        assert(solver.search_mode_ == Solver::SearchMode::Focused);\n        assert(solver.next_mode_switch_ == 2);\n\n        solver.activity_[4] = 100.0;\n        solver.order_heap_.update(4);\n        solver.vmtf_queue_.reset(solver.activity_, solver.heap_rank_);\n        assert(solver.pick_branch_lit() == 4);\n        solver.bump_variable(3);\n        assert(solver.vmtf_queue_.tail() == 3);\n        assert(solver.pick_branch_lit() == 3);\n\n        solver.stats_.conflicts = 1;\n        solver.maybe_switch_search_mode();\n        assert(solver.search_mode_ == Solver::SearchMode::Focused);\n        solver.stats_.conflicts = 2;\n        solver.maybe_switch_search_mode();\n        assert(solver.search_mode_ == Solver::SearchMode::Stable);\n        assert(solver.stats_.search_mode_switches == 1 && solver.next_mode_switch_ == 4);\n\n        solver.activity_[2] = 1000.0;\n        solver.order_heap_.update(2);\n        assert(solver.pick_branch_lit() == 2);\n        solver.stats_.conflicts = 4;\n        solver.maybe_switch_search_mode();\n        assert(solver.search_mode_ == Solver::SearchMode::Focused);\n        assert(solver.stats_.search_mode_switches == 2);\n\n        SolverConfig end_to_end; end_to_end.dual_search = true;\n        end_to_end.dual_search_interval = 1; end_to_end.congruence = false;\n        Solver search_solver(end_to_end);\n        const auto unsat = parse_dimacs_text("p cnf 2 4\\n1 2 0\\n1 -2 0\\n-1 2 0\\n-1 -2 0\\n");\n        const auto result = search_solver.solve(unsat);\n        assert(result.result == SolveResult::Unsat);\n        assert(result.stats.focused_conflicts + result.stats.stable_conflicts == result.stats.conflicts);\n        assert(result.stats.focused_decisions + result.stats.stable_decisions == result.stats.decisions);\n        assert(result.stats.vmtf_search_steps > 0);\n    }\n\n    static void root_probing() {\n''')

replace_once(
    "tests/test_search.cpp",
    '''    satsolv::SolverSearchTestAccess::delayed_activation();\n    satsolv::SolverSearchTestAccess::root_probing();\n''',
    '''    satsolv::SolverSearchTestAccess::delayed_activation();\n    satsolv::SolverSearchTestAccess::dual_search_modes();\n    satsolv::SolverSearchTestAccess::root_probing();\n''')

replace_once(
    "tests/test_search.cpp",
    '''    config = {}; config.rank_lbd = config.target_phases = config.reason_bump = config.recent_use = true;\n''',
    '''    config = {}; config.dual_search = true; config.dual_search_interval = 0; rejected = false;\n    try { satsolv::Solver solver(config); } catch (const std::invalid_argument&) { rejected = true; }\n    assert(rejected);\n    config = {}; config.rank_lbd = config.target_phases = config.reason_bump = config.recent_use = true;\n''')

replace_once(
    "tests/test_search.cpp",
    '''    std::cout << "PASS: birth/reason score conventions, fixed tiers, recent-use expiration, reason locks, target backtracking/reset, bounded bumps and epoch wrap\\n";\n''',
    '''    std::cout << "PASS: search policy invariants including focused VMTF / stable EVSIDS dual search\\n";\n''')

print("dual-search v1 source transformation applied")
