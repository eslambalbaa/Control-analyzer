#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace control::sfg {

inline constexpr int MAX_NODES = 20;
inline constexpr double ZERO_TOLERANCE = 1e-12;
inline constexpr int64_t MAX_RATIONAL_DENOMINATOR = 10000;
inline constexpr double MAX_RATIONAL_VALUE = 1e12;
inline constexpr double RATIONAL_MATCH_TOLERANCE = 1e-9;

template <typename T> struct Result {
  std::optional<T> value;
  std::string error;

  static Result<T> success(T val) {
    Result<T> res;
    res.value = val;
    return res;
  }

  static Result<T> failure(std::string message) {
    Result<T> res;
    res.error = message;
    return res;
  }

  bool ok() const { return value.has_value(); }
};

template <typename T> int count_of(const std::vector<T> &items) {
  return static_cast<int>(items.size());
}

struct Branch {
  int from = 0;
  int to = 0;
  double gain = 0.0;
};

struct SignalFlowGraph {
  int nodes = 0;
  int source = 0;
  int sink = 0;
  std::vector<Branch> branches;
};

struct Path {
  std::vector<int> nodes;
  std::vector<int> branches;
  double gain = 0.0;
  uint64_t mask = 0;
};

struct Loop {
  std::vector<int> nodes;
  std::vector<int> branches;
  double gain = 0.0;
  uint64_t mask = 0;
};

struct LoopGroup {
  std::vector<int> loops;
  double product = 0.0;
};

struct Determinant {
  std::vector<std::vector<LoopGroup>> levels;
  std::vector<double> level_sums;
  double value = 1.0;
};

struct PathTerm {
  Path path;
  Determinant cofactor;
  std::vector<int> touching;
  double contribution = 0.0;
};

struct MasonAnalysis {
  std::vector<Path> paths;
  std::vector<Loop> loops;
  Determinant delta;
  std::vector<PathTerm> terms;
  double numerator = 0.0;
  double transfer_function = 0.0;
};

struct OutgoingEdge {
  int id = 0;
  int to = 0;
  double gain = 0.0;
};

using AdjacencyList = std::vector<std::vector<OutgoingEdge>>;

inline double zero_if_tiny(double value) {
  return std::abs(value) < ZERO_TOLERANCE ? 0.0 : value;
}

inline std::optional<std::string> non_finite_text(double value) {
  if (std::isnan(value)) {
    return "undefined";
  }
  if (std::isinf(value)) {
    return value > 0.0 ? "inf" : "-inf";
  }
  return std::nullopt;
}

// approximate decimal as p/q fraction
inline std::optional<std::pair<int64_t, int64_t>> to_rational(double value) {
  double abs_val = std::abs(value);
  if (abs_val > MAX_RATIONAL_VALUE) {
    return std::nullopt;
  }

  int64_t sign = value < 0.0 ? -1 : 1;
  int64_t prev_num = 0, curr_num = 1;
  int64_t prev_den = 1, curr_den = 0;
  double rem = abs_val;

  for (int iter = 0; iter < 32; ++iter) {
    int64_t whole = static_cast<int64_t>(std::floor(rem + 1e-11));

    int64_t next_den = whole * curr_den + prev_den;
    if (next_den > MAX_RATIONAL_DENOMINATOR) {
      break;
    }
    int64_t next_num = whole * curr_num + prev_num;

    prev_num = curr_num;
    curr_num = next_num;
    prev_den = curr_den;
    curr_den = next_den;

    double approx =
        static_cast<double>(curr_num) / static_cast<double>(curr_den);
    if (std::abs(approx - abs_val) <=
        RATIONAL_MATCH_TOLERANCE * std::max(1.0, abs_val)) {
      return std::make_pair(sign * curr_num, curr_den);
    }

    double frac = rem - static_cast<double>(whole);
    if (frac <= 1e-11) {
      break;
    }
    rem = 1.0 / frac;
  }

  return std::nullopt;
}

inline std::string fixed_decimal_trimmed(double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(6) << value;
  std::string text = stream.str();

  while (text.back() == '0') {
    text.pop_back();
  }
  if (text.back() == '.') {
    text.pop_back();
  }
  return text;
}

inline std::string format_decimal(double value) {
  if (auto special = non_finite_text(value)) {
    return *special;
  }

  double clean = zero_if_tiny(value);
  double rounded = std::round(clean);
  if (std::abs(clean - rounded) < 1e-9 && std::abs(rounded) < 1e15) {
    return std::to_string(static_cast<int64_t>(rounded));
  }
  return fixed_decimal_trimmed(clean);
}

inline std::string format_num(double value) {
  if (auto special = non_finite_text(value)) {
    return *special;
  }

  double clean = zero_if_tiny(value);
  if (auto fraction = to_rational(clean)) {
    auto [num, den] = *fraction;
    if (den == 1) {
      return std::to_string(num);
    }
    return std::to_string(num) + "/" + std::to_string(den);
  }
  return format_decimal(clean);
}

inline std::string format_num_with_decimal(double value) {
  std::string primary = format_num(value);
  if (primary.find('/') != std::string::npos) {
    return primary + " (" + format_decimal(value) + ")";
  }
  return primary;
}

inline std::string format_signed(double value) {
  if (std::abs(value) < ZERO_TOLERANCE) {
    return "+0";
  }
  if (value > 0.0) {
    return "+" + format_num(value);
  }
  return format_num(value);
}

inline std::string wrap_neg(const std::string &text) {
  if (!text.empty() && text[0] == '-') {
    return "(" + text + ")";
  }
  return text;
}

// pack visited node indices into a 64-bit integer bitmask
inline uint64_t nodes_mask(const std::vector<int> &nodes) {
  uint64_t mask = 0;
  for (int node : nodes) {
    mask |= (uint64_t{1} << node);
  }
  return mask;
}

inline AdjacencyList build_outgoing_list(const SignalFlowGraph &graph) {
  AdjacencyList outgoing(graph.nodes);
  for (int id = 0; id < count_of(graph.branches); ++id) {
    const Branch &b = graph.branches[id];
    outgoing[b.from].push_back({id, b.to, b.gain});
  }
  return outgoing;
}

class PathSearch {
public:
  explicit PathSearch(const SignalFlowGraph &graph)
      : sink_(graph.sink), outgoing_(build_outgoing_list(graph)),
        visited_(graph.nodes, false), path_nodes_{graph.source} {
    visited_[graph.source] = true;
    extend(graph.source, 1.0);
  }

  const std::vector<Path> &paths() const { return paths_; }

private:
  void extend(int curr, double gain) {
    // reached the output node, save the forward path
    if (curr == sink_) {
      record_path(gain);
      return;
    }

    for (const OutgoingEdge &edge : outgoing_[curr]) {
      // skip visites nodes to ensure no loops
      if (visited_[edge.to]) {
        continue;
      }

      // mark node and accumulate path state
      visited_[edge.to] = true;
      path_nodes_.push_back(edge.to);
      path_branches_.push_back(edge.id);

      // explore, recurse with updated cumulative gain
      extend(edge.to, gain * edge.gain);

      // undo state changes to explore alternative paths
      path_branches_.pop_back();
      path_nodes_.pop_back();
      visited_[edge.to] = false;
    }
  }

  void record_path(double gain) {
    Path p;
    p.nodes = path_nodes_;
    p.branches = path_branches_;
    p.gain = gain;
    p.mask = nodes_mask(path_nodes_);
    paths_.push_back(p);
  }

  int sink_;
  AdjacencyList outgoing_;
  std::vector<bool> visited_;
  std::vector<int> path_nodes_;
  std::vector<int> path_branches_;
  std::vector<Path> paths_;
};

class LoopSearch {
public:
  explicit LoopSearch(const SignalFlowGraph &graph)
      : outgoing_(build_outgoing_list(graph)), visited_(graph.nodes, false) {
    for (start_ = 0; start_ < graph.nodes; ++start_) {
      visited_[start_] = true;
      path_nodes_ = {start_};
      path_branches_.clear();

      extend(start_, 1.0);

      visited_[start_] = false;
    }
  }

  const std::vector<Loop> &loops() const { return loops_; }

private:
  void extend(int curr, double gain) {
    for (const OutgoingEdge &edge : outgoing_[curr]) {
      // skip nodes smaller than start_ to prevent
      // duplicate cycles
      if (edge.to < start_) {
        continue;
      }

      // returned to start_ node, record the completed feedback loop
      if (edge.to == start_) {
        record_loop(edge, gain * edge.gain);
      } else if (!visited_[edge.to]) {
        // mark node and accumulate loop path state
        visited_[edge.to] = true;
        path_nodes_.push_back(edge.to);
        path_branches_.push_back(edge.id);

        // continue DFS with updated loop gain
        extend(edge.to, gain * edge.gain);

        // restore state to search for other cycles
        path_branches_.pop_back();
        path_nodes_.pop_back();
        visited_[edge.to] = false;
      }
    }
  }

  void record_loop(const OutgoingEdge &closing_edge, double gain) {
    Loop loop;
    loop.nodes = path_nodes_;
    loop.nodes.push_back(start_);
    loop.branches = path_branches_;
    loop.branches.push_back(closing_edge.id);
    loop.gain = gain;
    loop.mask = nodes_mask(path_nodes_);
    loops_.push_back(loop);
  }

  int start_ = 0;
  AdjacencyList outgoing_;
  std::vector<bool> visited_;
  std::vector<int> path_nodes_;
  std::vector<int> path_branches_;
  std::vector<Loop> loops_;
};

inline std::vector<Path> find_forward_paths(const SignalFlowGraph &graph) {
  return PathSearch(graph).paths();
}

inline std::vector<Loop> find_loops(const SignalFlowGraph &graph) {
  return LoopSearch(graph).loops();
}

class NonTouchingGroupSearch {
public:
  NonTouchingGroupSearch(const std::vector<Loop> &loops,
                         const std::vector<int> &candidates)
      : loops_(loops), candidates_(candidates) {
    extend(0, 1.0);
  }

  const std::vector<std::vector<LoopGroup>> &levels() const { return levels_; }

private:
  void extend(int first, double product) {
    for (int i = first; i < count_of(candidates_); ++i) {
      int candidate = candidates_[i];
      if (touches_chosen(candidate)) {
        continue;
      }

      chosen_.push_back(candidate);
      double group_product = product * loops_[candidate].gain;
      record_group(group_product);

      extend(i + 1, group_product);
      chosen_.pop_back();
    }
  }

  bool touches_chosen(int candidate) const {
    for (int loop : chosen_) {
      if ((loops_[loop].mask & loops_[candidate].mask) != 0) {
        return true;
      }
    }
    return false;
  }

  void record_group(double product) {
    int level = count_of(chosen_) - 1;
    if (count_of(levels_) <= level) {
      levels_.push_back({});
    }
    levels_[level].push_back({chosen_, product});
  }

  const std::vector<Loop> &loops_;
  const std::vector<int> &candidates_;
  std::vector<int> chosen_;
  std::vector<std::vector<LoopGroup>> levels_;
};

inline Determinant compute_determinant(const std::vector<Loop> &loops,
                                       const std::vector<int> &active_indices) {
  Determinant det;
  det.levels = NonTouchingGroupSearch(loops, active_indices).levels();

  det.value = 1.0;
  for (int level = 0; level < count_of(det.levels); ++level) {
    double sum = 0.0;
    for (const LoopGroup &group : det.levels[level]) {
      sum += group.product;
    }
    det.level_sums.push_back(sum);

    double sign = (level % 2 == 0) ? -1.0 : 1.0;
    det.value += sign * sum;
  }

  return det;
}

inline bool is_singular(const Determinant &det) {
  return std::abs(det.value) <= ZERO_TOLERANCE;
}

inline PathTerm compute_path_term(const Path &path,
                                  const std::vector<Loop> &loops) {
  std::vector<int> touching;
  std::vector<int> non_touching;

  for (int i = 0; i < count_of(loops); ++i) {
    if ((loops[i].mask & path.mask) != 0) {
      touching.push_back(i);
    } else {
      non_touching.push_back(i);
    }
  }

  PathTerm term;
  term.path = path;
  term.cofactor = compute_determinant(loops, non_touching);
  term.touching = touching;
  term.contribution = path.gain * term.cofactor.value;
  return term;
}

inline bool is_valid_node(const SignalFlowGraph &graph, int node) {
  return 0 <= node && node < graph.nodes;
}

inline std::optional<std::string>
find_graph_error(const SignalFlowGraph &graph) {
  if (graph.nodes < 2 || graph.nodes > MAX_NODES) {
    return "The number of nodes must be between 2 and 20.";
  }
  if (!is_valid_node(graph, graph.source) ||
      !is_valid_node(graph, graph.sink)) {
    return "Input and output node indices must be within the graph node "
           "range.";
  }
  if (graph.source == graph.sink) {
    return "The input node and output node must be different.";
  }

  for (const Branch &b : graph.branches) {
    if (!is_valid_node(graph, b.from) || !is_valid_node(graph, b.to)) {
      return "Branch node index is out of bounds.";
    }
    if (!std::isfinite(b.gain)) {
      return "Branch gain must be a finite number.";
    }
  }
  return std::nullopt;
}

inline Result<MasonAnalysis> analyze_graph(const SignalFlowGraph &graph) {
  if (auto error = find_graph_error(graph)) {
    return Result<MasonAnalysis>::failure(*error);
  }
  // **
  MasonAnalysis analysis;
  analysis.paths = find_forward_paths(graph);
  analysis.loops = find_loops(graph);

  std::vector<int> all_loops(analysis.loops.size());
  // fll with 0, 1,..
  std::iota(all_loops.begin(), all_loops.end(), 0);
  analysis.delta = compute_determinant(analysis.loops, all_loops);

  analysis.numerator = 0.0;
  for (const Path &path : analysis.paths) {
    PathTerm term = compute_path_term(path, analysis.loops);
    analysis.numerator += term.contribution;
    analysis.terms.push_back(term);
  }

  if (is_singular(analysis.delta)) {
    analysis.transfer_function = std::numeric_limits<double>::quiet_NaN();
  } else {
    analysis.transfer_function = analysis.numerator / analysis.delta.value;
  }

  return Result<MasonAnalysis>::success(analysis);
}

} // namespace control::sfg
