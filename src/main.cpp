#include "routh_table.hpp"
#include "signal_flow_graph.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using control::routh::HalfPlane;
using control::routh::Pole;
using control::routh::RouthReport;
using control::routh::RouthRow;
using control::sfg::Branch;
using control::sfg::Determinant;
using control::sfg::format_num;
using control::sfg::Loop;
using control::sfg::LoopGroup;
using control::sfg::MasonAnalysis;
using control::sfg::Path;
using control::sfg::PathTerm;
using control::sfg::Result;
using control::sfg::SignalFlowGraph;

std::string escape_json(const std::string &text) {
  std::string out;
  for (char c : text) {
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

std::string json_quote(const std::string &text) {
  return "\"" + escape_json(text) + "\"";
}

std::string json_number(double value) {
  if (!std::isfinite(value)) {
    return "null";
  }
  std::ostringstream out;
  out << std::setprecision(15) << value;
  return out.str();
}

std::string json_bool(bool value) { return value ? "true" : "false"; }

std::string json_int(int value) { return std::to_string(value); }

std::string json_quoted_num(double value) {
  return json_quote(format_num(value));
}

using JsonField = std::pair<std::string, std::string>;

std::string json_object(const std::vector<JsonField> &fields) {
  std::string out = "{";
  for (size_t i = 0; i < fields.size(); ++i) {
    out += (i > 0 ? "," : "") + json_quote(fields[i].first) + ":" +
           fields[i].second;
  }
  return out + "}";
}

std::string json_list(const std::vector<std::string> &items) {
  std::string out = "[";
  for (size_t i = 0; i < items.size(); ++i) {
    out += (i > 0 ? "," : "") + items[i];
  }
  return out + "]";
}

template <typename Items, typename ToJson>
std::string json_array(const Items &items, ToJson to_json) {
  std::vector<std::string> rendered;
  for (const auto &item : items) {
    rendered.push_back(to_json(item));
  }
  return json_list(rendered);
}

std::string json_indices(const std::vector<int> &values) {
  return json_array(values, json_int);
}

std::string build_error_json(const std::string &message) {
  return json_object({{"ok", "false"}, {"error", json_quote(message)}});
}

std::string branch_json(const Branch &b) {
  return json_object({
      {"from", json_int(b.from)},
      {"to", json_int(b.to)},
      {"gain", json_number(b.gain)},
      {"gain_num", json_quoted_num(b.gain)},
  });
}

std::string graph_json(const SignalFlowGraph &graph) {
  return json_object({
      {"nodes", json_int(graph.nodes)},
      {"source", json_int(graph.source)},
      {"sink", json_int(graph.sink)},
      {"branches", json_array(graph.branches, branch_json)},
  });
}

std::string path_json(const Path &p) {
  return json_object({
      {"nodes", json_indices(p.nodes)},
      {"branches", json_indices(p.branches)},
      {"gain", json_number(p.gain)},
      {"gain_num", json_quoted_num(p.gain)},
  });
}

std::string loop_json(const Loop &l) {
  return json_object({
      {"nodes", json_indices(l.nodes)},
      {"branches", json_indices(l.branches)},
      {"gain", json_number(l.gain)},
      {"gain_num", json_quoted_num(l.gain)},
  });
}

std::string loop_group_json(const LoopGroup &group) {
  std::string product_num = format_num(group.product);
  return json_object({
      {"loops", json_indices(group.loops)},
      {"product", json_number(group.product)},
      {"product_num", json_quote(product_num)},
      {"product_paren", json_quote(control::sfg::wrap_neg(product_num))},
  });
}

std::string determinant_level_json(const std::vector<LoopGroup> &groups,
                                   double sum) {
  return json_object({
      {"sum", json_number(sum)},
      {"sum_num", json_quoted_num(sum)},
      {"groups", json_array(groups, loop_group_json)},
  });
}

std::string determinant_json(const Determinant &det) {
  std::vector<std::string> levels;
  for (int i = 0; i < control::sfg::count_of(det.levels); ++i) {
    levels.push_back(determinant_level_json(det.levels[i], det.level_sums[i]));
  }

  return json_object({
      {"value", json_number(det.value)},
      {"value_num", json_quoted_num(det.value)},
      {"value_full",
       json_quote(control::sfg::format_num_with_decimal(det.value))},
      {"levels", json_list(levels)},
  });
}

std::string path_term_json(const PathTerm &t) {
  return json_object({
      {"touching", json_indices(t.touching)},
      {"contribution", json_number(t.contribution)},
      {"contribution_num", json_quoted_num(t.contribution)},
      {"cofactor", determinant_json(t.cofactor)},
  });
}

std::string analysis_json(const MasonAnalysis &analysis) {
  double tf = analysis.transfer_function;
  return json_object({
      {"numerator", json_number(analysis.numerator)},
      {"numerator_num", json_quoted_num(analysis.numerator)},
      {"transfer_function", json_number(tf)},
      {"transfer_num", json_quoted_num(tf)},
      {"transfer_decimal", json_quote(control::sfg::format_decimal(tf))},
      {"transfer_full", json_quote(control::sfg::format_num_with_decimal(tf))},
      {"delta_zero", json_bool(control::sfg::is_singular(analysis.delta))},
      {"paths", json_array(analysis.paths, path_json)},
      {"loops", json_array(analysis.loops, loop_json)},
      {"delta", determinant_json(analysis.delta)},
      {"terms", json_array(analysis.terms, path_term_json)},
  });
}

std::string build_sfg_json(const SignalFlowGraph &graph,
                           const MasonAnalysis &analysis) {
  return json_object({
      {"ok", "true"},
      {"graph", graph_json(graph)},
      {"analysis", analysis_json(analysis)},
  });
}

std::string region_name(HalfPlane region) {
  if (region == HalfPlane::Left) {
    return "Left";
  }
  if (region == HalfPlane::Right) {
    return "Right";
  }
  return "ImagAxis";
}

std::string pole_json(const Pole &p) {
  std::string im_str =
      std::abs(p.im) < 1e-6 ? "0" : (control::sfg::format_signed(p.im) + "j");

  return json_object({
      {"re", json_quoted_num(p.re)},
      {"im", json_quote(im_str)},
      {"region", json_quote(region_name(p.region))},
  });
}

std::string routh_row_json(const RouthRow &row) {
  std::string sign_label = row.values[0] > 0.0 ? "+" : "-";

  return json_object({
      {"power", json_int(row.power)},
      {"power_label", json_quote("s^" + std::to_string(row.power))},
      {"sign", json_quote(sign_label)},
      {"values", json_array(row.values, json_quoted_num)},
      {"raw_values", json_array(row.values, json_number)},
      {"steps", json_array(row.steps, json_quote)},
  });
}

std::string build_routh_json(const RouthReport &report) {
  return json_object({
      {"ok", "true"},
      {"polynomial",
       json_quote(control::routh::format_polynomial(report.coeffs))},
      {"degree", json_int(report.degree)},
      {"columns", json_int(report.columns)},
      {"sign_changes", json_int(report.sign_changes)},
      {"is_stable", json_bool(report.is_stable)},
      {"numeric_rhp", json_int(report.numeric_rhp)},
      {"axis_tolerance", json_quoted_num(control::routh::AXIS_TOLERANCE)},
      {"rows", json_array(report.rows, routh_row_json)},
      {"poles", json_array(report.poles, pole_json)},
  });
}

bool read_index(int &out_val) {
  long long raw = -1;
  if (!(std::cin >> raw) || raw < 0) {
    return false;
  }
  constexpr long long int_max = std::numeric_limits<int>::max();
  out_val = static_cast<int>(std::min(raw, int_max));
  return true;
}

Result<Branch> read_branch(int branch_number) {
  Branch branch;
  std::string gain_token;

  if (!read_index(branch.from) || !read_index(branch.to) ||
      !(std::cin >> gain_token)) {
    return Result<Branch>::failure("Invalid branch line at w" +
                                   std::to_string(branch_number) + ".");
  }

  auto parsed_gain = control::routh::parse_double(gain_token);
  if (!parsed_gain.has_value()) {
    return Result<Branch>::failure("Branch w" + std::to_string(branch_number) +
                                   " has an invalid gain '" + gain_token +
                                   "'.");
  }

  branch.gain = *parsed_gain;
  return Result<Branch>::success(branch);
}

Result<SignalFlowGraph> read_graph() {
  SignalFlowGraph graph;
  int branch_count = 0;

  if (!read_index(graph.nodes) || !read_index(graph.source) ||
      !read_index(graph.sink) || !read_index(branch_count)) {
    return Result<SignalFlowGraph>::failure("Invalid graph header.");
  }

  for (int number = 1; number <= branch_count; ++number) {
    auto branch = read_branch(number);
    if (!branch.ok()) {
      return Result<SignalFlowGraph>::failure(branch.error);
    }
    graph.branches.push_back(*branch.value);
  }
  return Result<SignalFlowGraph>::success(graph);
}

int reply(const std::string &json) {
  std::cout << json << '\n';
  return 0;
}

int handle_sfg() {
  auto graph = read_graph();
  if (!graph.ok()) {
    return reply(build_error_json(graph.error));
  }

  auto analysis = control::sfg::analyze_graph(*graph.value);
  if (!analysis.ok()) {
    return reply(build_error_json(analysis.error));
  }

  return reply(build_sfg_json(*graph.value, *analysis.value));
}

int handle_routh() {
  std::string equation;
  std::getline(std::cin, equation);

  auto report = control::routh::analyze_routh(equation);
  if (!report.ok()) {
    return reply(build_error_json(report.error));
  }

  return reply(build_routh_json(*report.value));
}

std::optional<std::string> read_mode() {
  std::string mode;
  if (!std::getline(std::cin, mode)) {
    return std::nullopt;
  }

  while (!mode.empty() && (mode.back() == '\r' || mode.back() == ' ')) {
    mode.pop_back();
  }
  return mode;
}

int main() {
  auto mode = read_mode();
  if (!mode.has_value()) {
    reply(build_error_json("No command provided."));
    return 1;
  }

  if (*mode == "sfg") {
    return handle_sfg();
  }
  if (*mode == "routh") {
    return handle_routh();
  }

  reply(build_error_json("Unknown mode '" + *mode + "'."));
  return 1;
}
