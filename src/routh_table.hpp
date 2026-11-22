#pragma once

#include "signal_flow_graph.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace control::routh {

using sfg::Result;

using Coefficients = std::vector<double>;
using Complex = std::complex<double>;

inline constexpr int MAX_DEGREE = 20;
inline constexpr double EPSILON = 1e-6;
inline constexpr double AXIS_TOLERANCE = 1e-3;
inline constexpr int ABERTH_ITERATIONS = 160;
inline constexpr double ABERTH_CONVERGENCE_TOL = 1e-14;
inline constexpr double PI = 3.14159265358979323846;

enum class HalfPlane {
  Left,
  ImagAxis,
  Right,
};

struct RouthRow {
  int power = 0;
  std::vector<double> values;
  std::vector<std::string> steps;
};

struct Pole {
  double re = 0.0;
  double im = 0.0;
  HalfPlane region = HalfPlane::ImagAxis;
};

struct RouthReport {
  Coefficients coeffs;
  int degree = 0;
  int columns = 0;
  std::vector<RouthRow> rows;
  int sign_changes = 0;
  bool is_stable = true;
  std::vector<Pole> poles;
  int numeric_rhp = 0;
};

struct PoleCompare {
  bool operator()(const Pole &a, const Pole &b) const {
    if (std::abs(a.re - b.re) > 1e-5) {
      return a.re > b.re;
    }
    return a.im > b.im;
  }
};

inline std::string trim_spaces(const std::string &input) {
  size_t first = 0;
  while (first < input.size() &&
         std::isspace(static_cast<unsigned char>(input[first]))) {
    ++first;
  }
  size_t last = input.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(input[last - 1]))) {
    --last;
  }
  return input.substr(first, last - first);
}

inline void replace_all(std::string &text, const std::string &from,
                        const std::string &to) {
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

inline std::optional<double> to_double(const std::string &text) {
  try {
    size_t consumed = 0;
    double val = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(val)) {
      return std::nullopt;
    }
    return val;
  } catch (...) {
    return std::nullopt;
  }
}

inline std::optional<double> parse_fraction(const std::string &num_text,
                                            const std::string &den_text) {
  if (den_text.find('/') != std::string::npos) {
    return std::nullopt;
  }

  auto num = to_double(trim_spaces(num_text));
  auto den = to_double(trim_spaces(den_text));
  if (!num.has_value() || !den.has_value() || std::abs(*den) <= 1e-15) {
    return std::nullopt;
  }

  double ratio = (*num) / (*den);
  if (!std::isfinite(ratio)) {
    return std::nullopt;
  }
  return ratio;
}

inline std::optional<double> parse_double(const std::string &raw) {
  std::string text = trim_spaces(raw);

  size_t slash = text.find('/');
  if (slash == std::string::npos) {
    return to_double(text);
  }
  return parse_fraction(text.substr(0, slash), text.substr(slash + 1));
}

inline Result<Coefficients> check_coeffs(const Coefficients &coeffs) {
  using CoeffResult = Result<Coefficients>;

  if (coeffs.size() < 2) {
    return CoeffResult::failure(
        "The polynomial degree must be at least 1 (at least two "
        "coefficients).");
  }
  if (sfg::count_of(coeffs) - 1 > MAX_DEGREE) {
    return CoeffResult::failure(
        "The maximum supported polynomial degree is 20.");
  }
  for (double coeff : coeffs) {
    if (!std::isfinite(coeff)) {
      return CoeffResult::failure(
          "All polynomial coefficients must be finite numbers.");
    }
  }
  if (std::abs(coeffs[0]) <= EPSILON) {
    return CoeffResult::failure("The leading coefficient must not be zero.");
  }
  for (double coeff : coeffs) {
    if (std::abs(coeff) <= EPSILON || (coeff > 0.0) != (coeffs[0] > 0.0)) {
      return CoeffResult::failure(
          "All coefficients from s^0 to s^n must be present and have the "
          "same sign.");
    }
  }
  return CoeffResult::success(coeffs);
}

inline std::string normalize_symbols(std::string text) {
  replace_all(text, "\xE2\x88\x92", "-");
  replace_all(text, "\xE2\x80\x93", "-");
  replace_all(text, "\xC2\xB7", "*");
  replace_all(text, "**", "^");
  return text;
}

inline std::string drop_equals_zero(const std::string &text) {
  size_t eq_pos = text.find('=');
  if (eq_pos == std::string::npos) {
    return text;
  }

  std::string lhs = text.substr(0, eq_pos);
  std::string rhs = trim_spaces(text.substr(eq_pos + 1));
  if (rhs.empty()) {
    return lhs;
  }

  auto rhs_val = to_double(rhs);
  if (rhs_val.has_value() && std::abs(*rhs_val) < 1e-12) {
    return lhs;
  }
  return text;
}

inline bool mentions_variable(const std::string &text) {
  return text.find('s') != std::string::npos ||
         text.find('S') != std::string::npos;
}

inline Result<Coefficients> parse_coefficient_list(std::string text) {
  std::replace(text.begin(), text.end(), ',', ' ');
  std::replace(text.begin(), text.end(), ';', ' ');

  std::stringstream stream(text);
  std::string token;
  Coefficients coeffs;
  while (stream >> token) {
    auto val = to_double(token);
    if (!val.has_value()) {
      return Result<Coefficients>::failure("'" + token +
                                           "' is not a valid number.");
    }
    coeffs.push_back(*val);
  }
  return check_coeffs(coeffs);
}

// remove spaces and '*' then split into signed terms
inline std::vector<std::string> split_into_terms(const std::string &text) {
  std::string compact;
  for (char c : text) {
    if (!std::isspace(static_cast<unsigned char>(c)) && c != '*') {
      compact.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }

  std::vector<std::string> terms;
  std::string curr_term;
  for (char c : compact) {
    bool starts_new_term =
        (c == '+' || c == '-') && !curr_term.empty() && curr_term.back() != '^';
    if (starts_new_term) {
      terms.push_back(curr_term);
      curr_term.clear();
    }
    curr_term.push_back(c);
  }
  if (!curr_term.empty()) {
    terms.push_back(curr_term);
  }
  return terms;
}

struct Term {
  int power = 0;
  double coeff = 0.0;
};

inline Result<double> parse_term_coefficient(const std::string &coeff_text,
                                             const std::string &term) {
  if (coeff_text.empty() || coeff_text == "+") {
    return Result<double>::success(1.0);
  }
  if (coeff_text == "-") {
    return Result<double>::success(-1.0);
  }

  auto val = to_double(coeff_text);
  if (!val.has_value()) {
    return Result<double>::failure("Invalid coefficient in term '" + term +
                                   "'.");
  }
  return Result<double>::success(*val);
}

inline Result<int> parse_term_power(const std::string &after_s,
                                    const std::string &term) {
  if (after_s.empty()) {
    return Result<int>::success(1);
  }
  if (after_s[0] != '^') {
    return Result<int>::failure("Unexpected characters in term '" + term +
                                "'.");
  }

  std::string digits = after_s.substr(1);
  if (digits.empty()) {
    return Result<int>::failure("Missing exponent in term '" + term + "'.");
  }
  for (char c : digits) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return Result<int>::failure("Invalid exponent in term '" + term + "'.");
    }
  }

  int power = 0;
  for (char c : digits) {
    power = power * 10 + (c - '0');
    if (power > MAX_DEGREE) {
      return Result<int>::failure(
          "The maximum supported polynomial degree is 20.");
    }
  }
  return Result<int>::success(power);
}

inline Result<Term> parse_term(const std::string &term) {
  if (term == "+" || term == "-") {
    return Result<Term>::failure(
        "There is a '+' or '-' sign with no term after it.");
  }

  size_t s_pos = term.find('s');
  if (s_pos == std::string::npos) {
    auto val = to_double(term);
    if (!val.has_value()) {
      return Result<Term>::failure("Cannot read term '" + term + "'.");
    }
    return Result<Term>::success({0, *val});
  }

  auto coeff = parse_term_coefficient(term.substr(0, s_pos), term);
  if (!coeff.ok()) {
    return Result<Term>::failure(coeff.error);
  }
  auto power = parse_term_power(term.substr(s_pos + 1), term);
  if (!power.ok()) {
    return Result<Term>::failure(power.error);
  }
  return Result<Term>::success({*power.value, *coeff.value});
}

inline Coefficients to_coefficients(const std::vector<Term> &terms) {
  int max_power = 0;
  for (const Term &t : terms) {
    max_power = std::max(max_power, t.power);
  }

  Coefficients coeffs(max_power + 1, 0.0);
  for (const Term &t : terms) {
    coeffs[max_power - t.power] += t.coeff;
  }
  return coeffs;
}

inline Result<Coefficients> parse_polynomial_terms(const std::string &text) {
  std::vector<Term> terms;
  for (const std::string &raw_term : split_into_terms(text)) {
    auto term = parse_term(raw_term);
    if (!term.ok()) {
      return Result<Coefficients>::failure(term.error);
    }
    terms.push_back(*term.value);
  }
  return check_coeffs(to_coefficients(terms));
}

inline Result<Coefficients> parse_polynomial(const std::string &input) {
  std::string text = trim_spaces(drop_equals_zero(normalize_symbols(input)));
  if (text.empty()) {
    return Result<Coefficients>::failure("Please enter a polynomial.");
  }

  if (!mentions_variable(text)) {
    return parse_coefficient_list(text);
  }
  return parse_polynomial_terms(text);
}

inline std::string format_term(double abs_coeff, int power) {
  std::string text;
  if (power == 0 || std::abs(abs_coeff - 1.0) > 1e-12) {
    text += sfg::format_num(abs_coeff);
  }

  if (power == 1) {
    text += "s";
  } else if (power > 1) {
    text += "s^" + std::to_string(power);
  }
  return text;
}

inline std::string format_polynomial(const Coefficients &coeffs) {
  int degree = sfg::count_of(coeffs) - 1;
  std::string output;
  bool first_term = true;

  for (int i = 0; i <= degree; ++i) {
    double coeff = coeffs[i];
    if (std::abs(coeff) < 1e-12) {
      continue;
    }

    if (first_term) {
      if (coeff < 0.0) {
        output += "-";
      }
    } else {
      output += (coeff < 0.0) ? " - " : " + ";
    }

    output += format_term(std::abs(coeff), degree - i);
    first_term = false;
  }

  if (output.empty()) {
    output = "0";
  }
  return output + " = 0";
}

inline Coefficients make_monic(const Coefficients &coeffs) {
  Coefficients monic(coeffs.size());
  for (size_t i = 0; i < coeffs.size(); ++i) {
    monic[i] = coeffs[i] / coeffs[0];
  }
  return monic;
}

inline std::vector<Complex> initial_root_guesses(const Coefficients &monic) {
  int degree = sfg::count_of(monic) - 1;

  double max_coeff = 0.0;
  for (int i = 1; i <= degree; ++i) {
    max_coeff = std::max(max_coeff, std::abs(monic[i]));
  }
  double radius = 1.0 + max_coeff;

  std::vector<Complex> roots(degree);
  for (int i = 0; i < degree; ++i) {
    double angle =
        2.0 * PI * static_cast<double>(i) / static_cast<double>(degree) + 0.37;
    double r = radius * (0.85 + 0.3 * (static_cast<double>(i % 3) / 2.0));
    roots[i] = Complex(r * std::cos(angle), r * std::sin(angle));
  }
  return roots;
}

struct PolyValues {
  Complex value;
  Complex derivative;
};

// evaluate P(z) and P'(z)
inline PolyValues evaluate_polynomial(const Coefficients &monic, Complex z) {
  Complex poly_val(1.0, 0.0);
  Complex deriv_val(0.0, 0.0);

  for (size_t j = 1; j < monic.size(); ++j) {
    deriv_val = deriv_val * z + poly_val;
    poly_val = poly_val * z + monic[j];
  }
  return {poly_val, deriv_val};
}

inline Complex aberth_step(const Coefficients &monic,
                           const std::vector<Complex> &roots, int i) {
  Complex z = roots[i];
  PolyValues p = evaluate_polynomial(monic, z);

  if (std::abs(p.value) < 1e-15 || std::abs(p.derivative) < 1e-18) {
    return 0.0;
  }

  Complex newton_step = p.value / p.derivative;
  Complex sum(0.0, 0.0);
  for (int j = 0; j < sfg::count_of(roots); ++j) {
    if (j == i) {
      continue;
    }
    Complex diff = z - roots[j];
    if (std::abs(diff) > 1e-18) {
      sum += 1.0 / diff;
    }
  }

  Complex denom = 1.0 - newton_step * sum;
  if (std::abs(denom) > 1e-18) {
    return newton_step / denom;
  }
  return 0.0;
}

inline Pole make_pole(Complex root) {
  Pole p;
  p.re = std::abs(root.real()) < 1e-7 ? 0.0 : root.real();
  p.im = std::abs(root.imag()) < 1e-6 ? 0.0 : root.imag();

  if (p.re > AXIS_TOLERANCE) {
    p.region = HalfPlane::Right;
  } else if (p.re < -AXIS_TOLERANCE) {
    p.region = HalfPlane::Left;
  } else {
    p.region = HalfPlane::ImagAxis;
  }
  return p;
}

// solve for all roots simultaneously using the Aberth-Ehrlich method
inline std::vector<Pole> find_poles(const Coefficients &coeffs) {
  Coefficients monic = make_monic(coeffs);
  std::vector<Complex> roots = initial_root_guesses(monic);
  int degree = sfg::count_of(roots);

  for (int iter = 0; iter < ABERTH_ITERATIONS; ++iter) {
    std::vector<Complex> steps(degree);
    double max_step = 0.0;
    for (int i = 0; i < degree; ++i) {
      steps[i] = aberth_step(monic, roots, i);
      max_step = std::max(max_step, std::abs(steps[i]));
    }

    for (int i = 0; i < degree; ++i) {
      roots[i] -= steps[i];
    }

    if (max_step < ABERTH_CONVERGENCE_TOL) {
      break;
    }
  }

  std::vector<Pole> poles;
  for (Complex root : roots) {
    poles.push_back(make_pole(root));
  }
  std::sort(poles.begin(), poles.end(), PoleCompare());
  return poles;
}

inline RouthRow make_first_row(const Coefficients &coeffs, int degree,
                               int columns) {
  RouthRow row;
  row.power = degree;
  row.values.assign(columns, 0.0);
  for (int i = 0; i < sfg::count_of(coeffs); i += 2) {
    row.values[i / 2] = coeffs[i];
  }
  row.steps.push_back(
      "Filled from every second coefficient starting at the highest power.");
  return row;
}

inline RouthRow make_second_row(const Coefficients &coeffs, int degree,
                                int columns) {
  RouthRow row;
  row.power = degree - 1;
  row.values.assign(columns, 0.0);
  for (int i = 1; i < sfg::count_of(coeffs); i += 2) {
    row.values[i / 2] = coeffs[i];
  }
  row.steps.push_back("Filled from the next coefficients of the polynomial.");
  return row;
}

inline std::string describe_entry(int power, int col, double pivot,
                                  double top_left, double top_right,
                                  double bottom_right, double value) {
  return "s^" + std::to_string(power) + ", col " + std::to_string(col + 1) +
         ": (" + sfg::format_num(pivot) + " * " +
         sfg::wrap_neg(sfg::format_num(top_right)) + " - " +
         sfg::wrap_neg(sfg::format_num(top_left)) + " * " +
         sfg::wrap_neg(sfg::format_num(bottom_right)) + ") / " +
         sfg::wrap_neg(sfg::format_num(pivot)) + " = " +
         sfg::format_num_with_decimal(value);
}

inline RouthRow make_next_row(const RouthRow &above2, const RouthRow &above1,
                              int power, int columns) {
  RouthRow row;
  row.power = power;
  row.values.assign(columns, 0.0);

  double pivot = above1.values[0];
  double top_left = above2.values[0];

  for (int col = 0; col + 1 < columns; ++col) {
    double top_right = above2.values[col + 1];
    double bottom_right = above1.values[col + 1];
    double value = (pivot * top_right - top_left * bottom_right) / pivot;
    row.values[col] = value;

    bool worth_showing = std::abs(top_right) > EPSILON ||
                         std::abs(bottom_right) > EPSILON || col == 0;
    if (worth_showing) {
      row.steps.push_back(describe_entry(power, col, pivot, top_left, top_right,
                                         bottom_right, value));
    }
  }
  return row;
}

inline int count_sign_changes(const std::vector<RouthRow> &rows) {
  int changes = 0;
  for (size_t i = 0; i + 1 < rows.size(); ++i) {
    bool curr_pos = rows[i].values[0] > 0.0;
    bool next_pos = rows[i + 1].values[0] > 0.0;
    if (curr_pos != next_pos) {
      ++changes;
    }
  }
  return changes;
}

inline int count_right_half_plane(const std::vector<Pole> &poles) {
  int count = 0;
  for (const Pole &pole : poles) {
    if (pole.region == HalfPlane::Right) {
      ++count;
    }
  }
  return count;
}

inline RouthReport build_table(const Coefficients &coeffs) {
  int degree = sfg::count_of(coeffs) - 1;
  int columns = degree / 2 + 1;

  std::vector<RouthRow> rows;
  rows.push_back(make_first_row(coeffs, degree, columns));
  rows.push_back(make_second_row(coeffs, degree, columns));
  for (int r = 2; r <= degree; ++r) {
    RouthRow next =
        make_next_row(rows[r - 2], rows[r - 1], degree - r, columns);
    rows.push_back(next);
  }

  RouthReport report;
  report.coeffs = coeffs;
  report.degree = degree;
  report.columns = columns;
  report.rows = rows;
  report.sign_changes = count_sign_changes(rows);
  report.is_stable = (report.sign_changes == 0);
  report.poles = find_poles(coeffs);
  report.numeric_rhp = count_right_half_plane(report.poles);
  return report;
}

inline Result<RouthReport> analyze_routh(const std::string &input) {
  auto parsed = parse_polynomial(input);
  if (!parsed.ok()) {
    return Result<RouthReport>::failure(parsed.error);
  }
  return Result<RouthReport>::success(build_table(*parsed.value));
}

} // namespace control::routh
