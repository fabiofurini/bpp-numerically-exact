#include "bpp/column_generation.hpp"
#include "bpp/branch_and_price.hpp"
#include "bpp/instance_reader.hpp"
#include "bpp/solution_writer.hpp"
#include "bpp/solver.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Every option after INSTANCE and MODE is either the one legacy-compatible
// bare positive integer (MAX_ITERATIONS/MAX_COLUMNS/MAX_NODES, depending on
// MODE, kept in its historical position so existing scripts do not break)
// or one of the `--name value` flags below, in any order. This mirrors how
// the historical executable took a fixed positional parameter file but lets
// the new CLI expose the extra tunables added during the refactor
// (docs/STATUS.md) without breaking the positional form.
struct ParsedArgs {
  std::string instance_path;
  std::string mode;
  unsigned long long positional = 0;
  bool has_positional = false;
  std::string strategy = "best-bound";
  double sr3_gap_activation = -1.0;   // negative sentinel: "not set, keep default"
  long long sr3_max_cuts = -1;        // negative sentinel: "not set, keep default"
  long long sr3_max_rounds = -1;      // negative sentinel: "not set, keep default"
  bool diving = false;
  long long diving_down_budget = -1;      // negative sentinel: "not set, keep default"
  double diving_time_limit_seconds = -1.0;  // negative sentinel: "not set, keep default"
  bool stabilization = false;
  double stabilization_alpha = -1.0;  // negative sentinel: "not set, keep default"
  // Default to whichever backend this binary was actually built with, so a
  // single-backend build (either one, see README.md) works out of the box
  // without forcing every invocation to pass --solver explicitly. CPLEX
  // wins when both are built in, matching every historical ANI-comparison
  // parameter file's PARAM_SOLVER.
#if defined(BPP_HAS_CPLEX)
  std::string solver = "cplex";
#elif defined(BPP_HAS_GUROBI)
  std::string solver = "gurobi";
#else
  std::string solver = "cplex";  // irrelevant: neither backend is built in
#endif
};

[[noreturn]] void usage_error(const std::string& message) {
  std::cerr << "usage: bpp-solve INSTANCE [--root-cg|--populate|--legacy-root-cg"
               "|--branch-price|--no-populate] [MAX_ITERATIONS|MAX_COLUMNS|MAX_NODES]"
               " [--strategy best-bound|depth-first]"
               " [--sr3-gap-activation VALUE] [--sr3-max-cuts VALUE] [--sr3-max-rounds N]"
               " [--diving] [--diving-down-budget N] [--diving-time-limit SECONDS]"
               " [--stabilization] [--stabilization-alpha VALUE]"
               " [--solver cplex|gurobi]\n"
               "       bpp-solve --help  (full documentation)\n";
  if (!message.empty()) std::cerr << "error: " << message << '\n';
  std::exit(2);
}

// Full usage documentation for `bpp-solve --help`/`-h`. Kept in sync with
// README.md's CLI section by hand -- both describe the same flags, but this
// is the copy a user actually sees without a network connection or a clone
// of the repository, so it repeats the essentials rather than pointing
// elsewhere for them.
void print_help() {
  std::cout <<
R"(bpp-solve -- numerically exact branch-price-and-cut solver for the
classical one-dimensional Bin-Packing Problem (Baldacci, Coniglio, Cordeau,
Furini, "A Numerically Exact Algorithm for the Bin-Packing Problem",
INFORMS Journal on Computing, 2023).

USAGE
  bpp-solve INSTANCE
  bpp-solve INSTANCE --root-cg      [MAX_ITERATIONS] [OPTIONS]
  bpp-solve INSTANCE --no-populate  [MAX_ITERATIONS] [OPTIONS]
  bpp-solve INSTANCE --legacy-root-cg [MAX_ITERATIONS] [OPTIONS]
  bpp-solve INSTANCE --populate     [MAX_COLUMNS]    [OPTIONS]
  bpp-solve INSTANCE --branch-price [MAX_NODES]      [OPTIONS]
  bpp-solve --help | -h

INSTANCE
  Path to a text file: one line "<item_count> <capacity>", then one integer
  weight per item, one per line.

MODES
  All modes below require a CPLEX- and/or Gurobi-enabled build (cmake
  -DBPP_ENABLE_CPLEX=ON and/or -DBPP_ENABLE_GUROBI=ON, see README.md).
  Without either, INSTANCE alone runs a greedy fallback that is always
  available, even in the portable build.

  (no mode)        Greedy first-fit-decreasing heuristic. Not exact; useful
                    as a quick sanity check or when no backend is built in.
  --root-cg         Floating column-generation phase (CPLEX or Gurobi, see
                    --solver), then the mandatory rational SoPlex/GMP safe
                    certification phase. Automatic SR3/triplet separation is
                    enabled.
  --no-populate     Historical no-populate compatibility path: floating root
  --legacy-root-cg  phase only, no safe-phase certification, no automatic
                    SR3 separation (cut-free). The two names are aliases.
  --populate        Safe root path (like --root-cg) followed by the bounded
                    historical gap population: enumerates additional
                    integer-feasible columns up to MAX_COLUMNS.
  --branch-price    Full Ryan-Foster branch-and-price tree to a certified
                    integer optimum, or until MAX_NODES is reached.
                    Automatic SR3 separation is enabled at every node.

POSITIONAL LIMIT (optional; meaning depends on the mode)
  --root-cg / --no-populate / --legacy-root-cg : MAX_ITERATIONS
        Column-generation iteration cap (default: unbounded within the
        library's internal default).
  --populate      : MAX_COLUMNS   -- populate phase column cap.
  --branch-price  : MAX_NODES     -- branch-and-price node cap.

OPTIONS
  --strategy best-bound|depth-first   (--branch-price only; default: best-bound)
        Node exploration order. best-bound always expands the pending node
        with the smallest certified bound next. depth-first matches the
        historical traversal (always explores the Together branch before
        Different, with backtracking) and warm-starts each node's column
        generation from every pattern priced anywhere in the tree so far,
        instead of starting from scratch. Both are exact; see docs/STATUS.md
        for measured performance under each.

  --sr3-gap-activation VALUE   (--root-cg / --populate / --branch-price)
        Overrides the historical PARAM_TRIPLET_GAP_ACT gate that throttles
        how often SR3/triplet separation runs (default: permissive). Has no
        effect with --no-populate/--legacy-root-cg, which never separates
        SR3 cuts.

  --sr3-max-cuts VALUE         (--root-cg / --populate / --branch-price)
        Overrides the historical PARAM_MAX_TRIPLETS cap on the total number
        of SR3 cuts that may be active at once (default: permissive). Same
        no-effect caveat as --sr3-gap-activation above.

  --sr3-max-rounds N           (--root-cg / --populate / --branch-price; default: 4)
        Caps how many automatic SR3 separation rounds run per call (not a
        historical parameter -- an implementation-side throttle on the
        SR3-aware pricing DP's cost, see docs/STATUS.md). Raising it lets
        more simultaneous cuts accumulate at the cost of a slower pricing
        DP per call; the DP's cut-state dominance pruning
        (prune_dominated_sr3_labels) keeps this sub-quadratic rather than
        the ~3x-per-cut growth without it.

  --diving                     (--branch-price only; default: off)
        Enables the historical diving heuristic (BPPS_BP_DIVING.cpp): a
        bounded depth-first dive, always into the Together branch for the
        fractional pair the LP relaxation already leans towards, run once
        after the main tree search and seeded with its incumbent. Only
        adopted if it strictly improves on the main search's result.

  --diving-down-budget N       (only meaningful with --diving; default: 1)
        Number of simultaneous Different branches the diving pass may open
        at once, matching the historical PARAM_TOKEN_DIV parameter.

  --diving-time-limit SECONDS  (only meaningful with --diving; default: 400)
        Wall-clock budget for the diving pass.

  --stabilization              (--root-cg / --populate / --branch-price; default: off)
        Enables the historical static dual-value smoothing heuristic
        (BPPS_BP_MASTER.cpp's SMOOTHING_* routines, legacy PARAM_SMOOTH):
        pricing duals are a convex combination of a stability center and the
        current LP dual solution, with a safeguard that discards any column
        found under the smoothed duals if it turns out not to actually
        improve under the real ones. Only ever active at the root with no
        SR3 cuts and no branching (silently has no effect elsewhere, exactly
        like legacy). Every historical parameter file found -- including the
        ones used to produce the paper's reported results -- has this
        switched off, so it stays off by default here too; it exists purely
        as an option to test.

  --stabilization-alpha VALUE  (only meaningful with --stabilization; default: 0.3)
        The smoothing convex-combination weight (legacy PARAM_SMOOTH_ALPHA),
        must be in [0,1).

  --solver cplex|gurobi        (default: whichever backend this binary was built with;
                                 CPLEX if both)
        Which floating-point LP backend solves the restricted master
        (legacy PARAM_SOLVER). The two are alternative implementations of
        the same role, not different algorithms; CPLEX wins by default
        when both are built in, matching every historical ANI-comparison
        parameter file. Selecting a backend that was not compiled in
        fails with an explicit error. Running the complete test suite
        needs both BPP_ENABLE_CPLEX and BPP_ENABLE_GUROBI; either alone
        is enough for normal use and picked up automatically.

  --help, -h
        Print this text and exit.

OUTPUT
  One "key value" pair per line on stdout, intended to be machine-parsed
  (see scripts/run_ani_comparison.sh for an example consumer). Diagnostics
  and errors go to stderr, never stdout.

EXIT CODES
  0  Success: converged (root/populate modes) or optimal with a certified
     bound (--branch-price), or the greedy fallback ran without error.
  1  Runtime error (invalid instance file, solver exception, etc).
  2  Usage error (missing/invalid arguments).
  3  The iteration/node limit was reached before reaching a certified result.

EXAMPLES
  bpp-solve instance.txt
  bpp-solve instance.txt --root-cg 500
  bpp-solve instance.txt --branch-price 200 --strategy depth-first --diving
  bpp-solve instance.txt --populate 5000 --sr3-max-cuts 20
  bpp-solve instance.txt --root-cg --solver gurobi

See README.md and docs/STATUS.md for full detail, build instructions and
measured performance figures.
)";
}

ParsedArgs parse_args(int argc, char** argv) {
  if (argc < 2) usage_error("missing INSTANCE");
  ParsedArgs parsed;
  parsed.instance_path = argv[1];
  if (argc >= 3) parsed.mode = argv[2];
  for (int i = 3; i < argc; ++i) {
    const std::string token = argv[i];
    auto require_value = [&](const char* flag_name) -> std::string {
      if (i + 1 >= argc) usage_error(std::string(flag_name) + " requires a value");
      return argv[++i];
    };
    if (token == "--strategy") {
      parsed.strategy = require_value("--strategy");
      if (parsed.strategy != "best-bound" && parsed.strategy != "depth-first") {
        usage_error("--strategy must be best-bound or depth-first, got: " + parsed.strategy);
      }
    } else if (token == "--sr3-gap-activation") {
      parsed.sr3_gap_activation = std::stod(require_value("--sr3-gap-activation"));
    } else if (token == "--sr3-max-cuts") {
      parsed.sr3_max_cuts = std::stoll(require_value("--sr3-max-cuts"));
    } else if (token == "--sr3-max-rounds") {
      parsed.sr3_max_rounds = std::stoll(require_value("--sr3-max-rounds"));
    } else if (token == "--diving") {
      parsed.diving = true;
    } else if (token == "--diving-down-budget") {
      parsed.diving_down_budget = std::stoll(require_value("--diving-down-budget"));
    } else if (token == "--diving-time-limit") {
      parsed.diving_time_limit_seconds = std::stod(require_value("--diving-time-limit"));
    } else if (token == "--stabilization") {
      parsed.stabilization = true;
    } else if (token == "--stabilization-alpha") {
      parsed.stabilization_alpha = std::stod(require_value("--stabilization-alpha"));
    } else if (token == "--solver") {
      parsed.solver = require_value("--solver");
      if (parsed.solver != "cplex" && parsed.solver != "gurobi") {
        usage_error("--solver must be cplex or gurobi, got: " + parsed.solver);
      }
    } else if (!token.empty() && token[0] != '-') {
      if (parsed.has_positional) usage_error("unexpected extra positional argument: " + token);
      parsed.positional = std::stoull(token);
      parsed.has_positional = true;
    } else {
      usage_error("unrecognized option: " + token);
    }
  }
  return parsed;
}

#if defined(BPP_HAS_CPLEX) || defined(BPP_HAS_GUROBI)
// parse_args already rejects any value other than the two below. Only used
// by --branch-price, which is itself only available in a backend-enabled
// build.
bpp::NodeStrategy parse_strategy(const std::string& name) {
  return name == "depth-first" ? bpp::NodeStrategy::DepthFirst : bpp::NodeStrategy::BestBound;
}

// parse_args already rejects any value other than the two below.
bpp::LpBackend parse_backend(const std::string& name) {
  return name == "gurobi" ? bpp::LpBackend::Gurobi : bpp::LpBackend::Cplex;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string token = argv[i];
    if (token == "--help" || token == "-h") {
      print_help();
      return 0;
    }
  }
  const ParsedArgs args = parse_args(argc, argv);
  try {
    const auto instance = bpp::read_instance(args.instance_path);
#if defined(BPP_HAS_CPLEX) || defined(BPP_HAS_GUROBI)
    if (args.mode == "--branch-price") {
      bpp::BranchAndPriceOptions options;
      // The production tree uses the same bounded SR3 separation as the
      // root workflow. The historical no-populate compatibility mode is
      // selected only by the dedicated root flags below.
      options.node_options.automatic_sr3_separation = true;
      options.node_options.backend = parse_backend(args.solver);
      if (args.has_positional) options.max_nodes = static_cast<std::size_t>(args.positional);
      options.node_strategy = parse_strategy(args.strategy);
      if (args.sr3_gap_activation >= 0.0) options.node_options.sr3_gap_activation = args.sr3_gap_activation;
      if (args.sr3_max_cuts >= 0) options.node_options.max_sr3_cuts_total = static_cast<std::size_t>(args.sr3_max_cuts);
      if (args.sr3_max_rounds >= 0) options.node_options.max_sr3_separation_rounds = static_cast<std::size_t>(args.sr3_max_rounds);
      options.diving_enabled = args.diving;
      if (args.diving_down_budget >= 0) options.diving_down_budget = static_cast<std::size_t>(args.diving_down_budget);
      if (args.diving_time_limit_seconds >= 0.0) options.diving_time_limit_seconds = args.diving_time_limit_seconds;
      options.node_options.dual_stabilization = args.stabilization;
      if (args.stabilization_alpha >= 0.0) options.node_options.stabilization_alpha = args.stabilization_alpha;
      const auto result = bpp::solve_branch_and_price(instance, options);
      const bool integer_certificate = result.optimal && result.lower_bound.has_value() &&
                                       result.lower_bound->ceil_bins() == result.incumbent_bins;
      std::cout << "status " << (result.optimal ? "optimal" : "node_limit") << '\n'
                << "strategy " << args.strategy << '\n'
                << "diving " << (args.diving ? 1 : 0) << '\n'
                << "stabilization " << (args.stabilization ? 1 : 0) << '\n'
                << "solver " << args.solver << '\n'
                << "incumbent " << result.incumbent_bins << '\n'
                << "lower_bound_safe " << (result.lower_bound.has_value() ? result.lower_bound->str() : "unavailable") << '\n'
                << "lower_bound_safe_ceil " << (result.lower_bound.has_value() ? std::to_string(result.lower_bound->ceil_bins()) : "unavailable") << '\n'
                << "integer_optimum_certified " << (integer_certificate ? 1 : 0) << '\n'
                << "processed_nodes " << result.processed_nodes << '\n'
                << "generated_nodes " << result.generated_nodes << '\n'
                << "pruned_nodes " << result.pruned_nodes << '\n';
      return result.optimal ? 0 : 3;
    }
    if (args.mode == "--root-cg" || args.mode == "--populate" ||
        args.mode == "--legacy-root-cg" || args.mode == "--no-populate") {
      bpp::ColumnGenerationOptions options;
      if (args.has_positional) {
        if (args.mode == "--populate") {
          options.populate_max_columns = static_cast<std::size_t>(args.positional);
        } else {
          options.max_iterations = static_cast<std::size_t>(args.positional);
        }
      }
      // `--no-populate` is the historical baseline alias: it runs the
      // floating root loop only.  `--root-cg` explicitly opts into the
      // post-CG safe phase (GMP or SoPlex, depending on the build).
      const bool legacy_root = args.mode == "--legacy-root-cg" || args.mode == "--no-populate";
      const bool populate = args.mode == "--populate";
      options.populate = populate;
      options.automatic_sr3_separation = !legacy_root;
      if (args.sr3_gap_activation >= 0.0) options.sr3_gap_activation = args.sr3_gap_activation;
      if (args.sr3_max_cuts >= 0) options.max_sr3_cuts_total = static_cast<std::size_t>(args.sr3_max_cuts);
      if (args.sr3_max_rounds >= 0) options.max_sr3_separation_rounds = static_cast<std::size_t>(args.sr3_max_rounds);
      options.dual_stabilization = args.stabilization;
      if (args.stabilization_alpha >= 0.0) options.stabilization_alpha = args.stabilization_alpha;
      options.backend = parse_backend(args.solver);
      const auto result = legacy_root
                              ? bpp::solve_root_column_generation(instance, options)
                              : bpp::solve_two_phase_root_column_generation(instance, options);
      const bool integer_certificate = result.converged && result.safe_duals_feasible &&
                                       result.safe_bound.has_value() &&
                                       result.safe_bound->ceil_bins() == result.incumbent_bins;
      std::cout << "status " << (result.converged ? "converged" : "iteration_limit") << '\n'
                << "lp_bound " << result.lp_bound << '\n'
                << "upper_bound " << result.incumbent_bins << '\n'
                << "incumbent " << result.incumbent_bins << '\n'
                << "lower_bound_safe " << (result.safe_bound.has_value() ? result.safe_bound->str() : "unavailable") << '\n'
                << "lower_bound_safe_ceil " << (result.safe_bound.has_value() ? std::to_string(result.safe_bound->ceil_bins()) : "unavailable") << '\n'
                << "integer_optimum_certified " << (integer_certificate ? 1 : 0) << '\n'
                << "safe_bound " << (result.safe_bound.has_value() ? result.safe_bound->str() : "unavailable") << '\n'
                << "safe_duals_feasible " << (result.safe_duals_feasible ? 1 : 0) << '\n'
                << "iterations " << result.iterations << '\n'
                << "phase1_iterations " << result.phase1_iterations << '\n'
                << "phase2_iterations " << result.phase2_iterations << '\n'
                << "populate_columns " << result.populate_columns << '\n'
                << "populate_complete " << (result.populate_complete ? 1 : 0) << '\n'
                << "sr3_cuts " << result.active_sr3_cuts.size() << '\n'
                << "sr3_cuts_added " << result.sr3_cuts_added << '\n'
                << "automatic_sr3 " << ((!legacy_root) ? 1 : 0) << '\n'
                << "stabilization " << (args.stabilization ? 1 : 0) << '\n'
                << "solver " << args.solver << '\n'
                << "phase2_verified " << (result.phase2_verified ? 1 : 0) << '\n'
                << "phase2_backend " << result.phase2_backend << '\n'
                << "columns " << result.patterns.size() << '\n'
                << "generated_columns " << result.generated_columns << '\n';
      return result.converged ? 0 : 3;
    }
#else
    if (!args.mode.empty()) {
      throw std::runtime_error("--root-cg/--branch-price require a CPLEX- and/or Gurobi-enabled build");
    }
#endif
    const auto solution = bpp::solve_greedy(instance);
    std::string error;
    if (!solution.is_valid(&error)) throw std::runtime_error("internal invalid solution: " + error);
    bpp::write_solution(solution, std::cout);
  } catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }
}
