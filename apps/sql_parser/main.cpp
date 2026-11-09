/// \file
/// \brief Interactive LL(1) SELECT parser visualizer built on FTXUI.
///
/// Shows the source, token stream, parse stack, active M[A,a] cell and trace
/// one step at a time.

import std;
import kravidb;

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

namespace {

namespace sql = kravidb::sql;
namespace ll1 = sql::ll1;

// Bright truecolor palette tuned for terminals with a dark background.
// Color channels are data, not magic constants.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace palette {

[[nodiscard]] auto keyword() -> ftxui::Color {
  return ftxui::Color::RGB(103, 232, 249);
}
[[nodiscard]] auto identifier() -> ftxui::Color {
  return ftxui::Color::RGB(147, 197, 253);
}
[[nodiscard]] auto integer() -> ftxui::Color {
  return ftxui::Color::RGB(253, 230, 138);
}
[[nodiscard]] auto decimal() -> ftxui::Color {
  return ftxui::Color::RGB(240, 171, 252);
}
[[nodiscard]] auto string_literal() -> ftxui::Color {
  return ftxui::Color::RGB(134, 239, 172);
}
[[nodiscard]] auto operation() -> ftxui::Color {
  return ftxui::Color::RGB(252, 165, 165);
}
[[nodiscard]] auto punctuation() -> ftxui::Color {
  return ftxui::Color::RGB(209, 213, 219);
}
[[nodiscard]] auto text() -> ftxui::Color {
  return ftxui::Color::RGB(229, 231, 235);
}
[[nodiscard]] auto muted() -> ftxui::Color {
  return ftxui::Color::RGB(156, 163, 175);
}
[[nodiscard]] auto success() -> ftxui::Color {
  return ftxui::Color::RGB(134, 239, 172);
}
[[nodiscard]] auto failure() -> ftxui::Color {
  return ftxui::Color::RGB(252, 165, 165);
}
[[nodiscard]] auto cursor_text() -> ftxui::Color {
  return ftxui::Color::RGB(15, 23, 42);
}

// One accent per zone so the eye can separate the panels at a glance.
[[nodiscard]] auto zone_source() -> ftxui::Color {
  return ftxui::Color::RGB(96, 165, 250);
}
[[nodiscard]] auto zone_action() -> ftxui::Color {
  return ftxui::Color::RGB(244, 114, 182);
}
[[nodiscard]] auto chrome() -> ftxui::Color {
  return ftxui::Color::RGB(148, 163, 184);
}

// One hue per grammar variable so the stack does not read as a single color.
// A symbol keeps the same color wherever it appears.
struct Rgb {
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

[[nodiscard]] auto nonterminal(ll1::Nonterminal value) -> ftxui::Color {
  // Maximum-contrast categorical palette. Each word keeps its color in the
  // stack, the table row headers, the action panel and the trace.
  static constexpr std::array<Rgb, static_cast<std::size_t>(ll1::Nonterminal::count)> ramp{
      {
          {.red = 230, .green = 25, .blue = 75},    // statement
          {.red = 66, .green = 212, .blue = 244},   // targets
          {.red = 255, .green = 225, .blue = 25},   // target
          {.red = 67, .green = 99, .blue = 216},    // targets_tail
          {.red = 245, .green = 130, .blue = 49},   // table
          {.red = 145, .green = 30, .blue = 180},   // where_opt
          {.red = 60, .green = 180, .blue = 75},    // expr
          {.red = 240, .green = 50, .blue = 230},   // or_tail
          {.red = 250, .green = 190, .blue = 212},  // and_expr
          {.red = 70, .green = 153, .blue = 144},   // and_tail
          {.red = 191, .green = 239, .blue = 69},   // predicate
          {.red = 255, .green = 216, .blue = 177},  // predicate_tail
          {.red = 255, .green = 250, .blue = 200},  // primary
          {.red = 170, .green = 255, .blue = 195},  // comparison
          {.red = 220, .green = 190, .blue = 255},  // not_opt
          {.red = 169, .green = 169, .blue = 169},  // literal
          {.red = 255, .green = 255, .blue = 255},  // semicolon_opt
      },
  };
  const auto index = static_cast<std::size_t>(value);
  if (index >= ramp.size()) {
    return muted();
  }
  const auto& rgb = ramp.at(index);
  return ftxui::Color::RGB(rgb.red, rgb.green, rgb.blue);
}

}  // namespace palette
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

constexpr std::size_t max_stack_rows = 9;
constexpr int stack_width = 28;
constexpr int lookahead_width = 12;
constexpr int nonterminal_width = 16;
constexpr int production_width = 4;
constexpr int min_table_height = 6;
constexpr int min_trace_height = 4;
constexpr int min_trace_rows = 3;
constexpr int max_trace_height = 14;
constexpr int layout_trace_header_rows = 2;
constexpr int min_content_height = 12;
constexpr int layout_border_rows = 2;
constexpr int layout_fixed_rows = 8;
constexpr int layout_error_rows = 4;
constexpr int trace_divisor = 3;
constexpr auto play_interval = std::chrono::milliseconds(450);

struct Demo {
  std::string name;
  std::string sql;
};

struct Parsed {
  std::vector<sql::Token> tokens;
  std::vector<ll1::TraceStep> steps;
  std::optional<sql::SqlError> error;
  bool accepted{false};
};

struct State {
  std::size_t demo{0};
  std::size_t step{0};
  bool full_table{true};
  bool editing{false};
  int cursor{0};
  Parsed parsed;
  std::string source;
  std::string validated;
  std::string title;
};

struct Options {
  bool help{false};
  bool error{false};
  std::optional<std::string> query;
};

[[nodiscard]] auto escaped(std::string_view value) -> std::string {
  std::string result;
  result.reserve(value.size());
  for (char character : value) {
    switch (character) {
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result.push_back(character);
        break;
    }
  }
  return result;
}

[[nodiscard]] auto nonterminal_name(ll1::Nonterminal nonterminal) -> std::string_view {
  switch (nonterminal) {
    case ll1::Nonterminal::statement:
      return "statement";
    case ll1::Nonterminal::targets:
      return "targets";
    case ll1::Nonterminal::target:
      return "target";
    case ll1::Nonterminal::targets_tail:
      return "targets_tail";
    case ll1::Nonterminal::table:
      return "table";
    case ll1::Nonterminal::where_opt:
      return "where_opt";
    case ll1::Nonterminal::expr:
      return "expr";
    case ll1::Nonterminal::or_tail:
      return "or_tail";
    case ll1::Nonterminal::and_expr:
      return "and_expr";
    case ll1::Nonterminal::and_tail:
      return "and_tail";
    case ll1::Nonterminal::predicate:
      return "predicate";
    case ll1::Nonterminal::predicate_tail:
      return "predicate_tail";
    case ll1::Nonterminal::primary:
      return "primary";
    case ll1::Nonterminal::comparison:
      return "comparison";
    case ll1::Nonterminal::not_opt:
      return "not_opt";
    case ll1::Nonterminal::literal:
      return "literal";
    case ll1::Nonterminal::semicolon_opt:
      return "semicolon_opt";
    case ll1::Nonterminal::count:
      return "count";
  }
  return "?";
}

[[nodiscard]] auto keyword_name(sql::Keyword keyword) -> std::string_view {
  switch (keyword) {
    case sql::Keyword::select:
      return "SELECT";
    case sql::Keyword::from:
      return "FROM";
    case sql::Keyword::where:
      return "WHERE";
    case sql::Keyword::and_:
      return "AND";
    case sql::Keyword::or_:
      return "OR";
    case sql::Keyword::is:
      return "IS";
    case sql::Keyword::not_:
      return "NOT";
    case sql::Keyword::null:
      return "NULL";
    default:
      return "KEYWORD";
  }
}

[[nodiscard]] auto terminal_name(const ll1::Terminal& terminal) -> std::string {
  switch (terminal.kind) {
    case sql::TokenKind::identifier:
      return "identifier";
    case sql::TokenKind::keyword:
      return std::string{keyword_name(terminal.keyword)};
    case sql::TokenKind::integer_literal:
      return "integer";
    case sql::TokenKind::decimal_literal:
      return "decimal";
    case sql::TokenKind::string_literal:
      return "string";
    case sql::TokenKind::comma:
      return ",";
    case sql::TokenKind::left_paren:
      return "(";
    case sql::TokenKind::right_paren:
      return ")";
    case sql::TokenKind::star:
      return "*";
    case sql::TokenKind::semicolon:
      return ";";
    case sql::TokenKind::equal:
      return "=";
    case sql::TokenKind::not_equal:
      return "<>";
    case sql::TokenKind::less:
      return "<";
    case sql::TokenKind::less_equal:
      return "<=";
    case sql::TokenKind::greater:
      return ">";
    case sql::TokenKind::greater_equal:
      return ">=";
    case sql::TokenKind::end_of_input:
      return "$";
  }
  return "?";
}

[[nodiscard]] auto symbol_name(const ll1::Symbol& symbol) -> std::string {
  if (const auto* nonterminal = std::get_if<ll1::Nonterminal>(&symbol)) {
    return std::string{nonterminal_name(*nonterminal)};
  }
  return terminal_name(std::get<ll1::Terminal>(symbol));
}

[[nodiscard]] auto token_text(const sql::Token& token) -> std::string {
  if (token.kind == sql::TokenKind::end_of_input) {
    return "$";
  }
  return escaped(token.text);
}

[[nodiscard]] auto token_color(sql::TokenKind kind) -> ftxui::Color {
  switch (kind) {
    case sql::TokenKind::keyword:
      return palette::keyword();
    case sql::TokenKind::identifier:
      return palette::identifier();
    case sql::TokenKind::integer_literal:
      return palette::integer();
    case sql::TokenKind::decimal_literal:
      return palette::decimal();
    case sql::TokenKind::string_literal:
      return palette::string_literal();
    case sql::TokenKind::equal:
    case sql::TokenKind::not_equal:
    case sql::TokenKind::less:
    case sql::TokenKind::less_equal:
    case sql::TokenKind::greater:
    case sql::TokenKind::greater_equal:
      return palette::operation();
    case sql::TokenKind::comma:
    case sql::TokenKind::left_paren:
    case sql::TokenKind::right_paren:
    case sql::TokenKind::star:
    case sql::TokenKind::semicolon:
      return palette::punctuation();
    case sql::TokenKind::end_of_input:
      return palette::muted();
  }
  return palette::text();
}

[[nodiscard]] auto terminal_color(const ll1::Terminal& terminal) -> ftxui::Color {
  return token_color(terminal.kind);
}

[[nodiscard]] auto symbol_color(const ll1::Symbol& symbol) -> ftxui::Color {
  if (const auto* nonterminal = std::get_if<ll1::Nonterminal>(&symbol)) {
    return palette::nonterminal(*nonterminal);
  }
  return terminal_color(std::get<ll1::Terminal>(symbol));
}

[[nodiscard]] auto symbol_element(const ll1::Symbol& symbol) -> ftxui::Element {
  return ftxui::text(symbol_name(symbol)) | ftxui::color(symbol_color(symbol));
}

[[nodiscard]] auto production_element(const ll1::Production& production) -> ftxui::Element {
  const auto neutral = palette::muted();
  ftxui::Elements parts;
  parts.push_back(ftxui::text(nonterminal_name(production.left)) |
                  ftxui::color(palette::nonterminal(production.left)));
  parts.push_back(ftxui::text(" ->") | ftxui::color(neutral));
  if (production.right.empty()) {
    parts.push_back(ftxui::text(" ε") | ftxui::color(neutral));
    return ftxui::hbox(std::move(parts));
  }
  for (const auto& symbol : production.right) {
    parts.push_back(ftxui::text(" ") | ftxui::color(neutral));
    parts.push_back(symbol_element(symbol));
  }
  return ftxui::hbox(std::move(parts));
}

[[nodiscard]] auto action_name(ll1::TraceAction action) -> std::string_view {
  switch (action) {
    case ll1::TraceAction::expand:
      return "expand";
    case ll1::TraceAction::match:
      return "match";
    case ll1::TraceAction::accept:
      return "accept";
    case ll1::TraceAction::error:
      return "error";
  }
  return "?";
}

[[nodiscard]] auto action_color(ll1::TraceAction action) -> ftxui::Color {
  switch (action) {
    case ll1::TraceAction::expand:
      return palette::keyword();
    case ll1::TraceAction::match:
      return palette::identifier();
    case ll1::TraceAction::accept:
      return palette::success();
    case ll1::TraceAction::error:
      return palette::failure();
  }
  return palette::text();
}

[[nodiscard]] auto analyze(std::string_view source) -> Parsed {
  Parsed parsed;
  auto tokenized = sql::tokenize(source);
  if (!tokenized) {
    parsed.error = std::move(tokenized.error());
    return parsed;
  }
  parsed.tokens = std::move(tokenized.value());
  auto validation = sql::validate_select(
      parsed.tokens, [&](const ll1::TraceStep& step) -> void { parsed.steps.push_back(step); });
  if (!validation) {
    parsed.error = std::move(validation.error());
  } else {
    parsed.accepted = true;
  }
  return parsed;
}

[[nodiscard]] auto current_step(const State& state) -> const ll1::TraceStep* {
  if (state.step < state.parsed.steps.size()) {
    return &state.parsed.steps.at(state.step);
  }
  return nullptr;
}

[[nodiscard]] auto production_at(std::span<const ll1::Production> grammar, std::size_t index)
    -> const ll1::Production& {
  return grammar.subspan(index, 1).front();
}

[[nodiscard]] auto step_detail_element(const Parsed& parsed, const ll1::TraceStep& step,
                                       std::span<const ll1::Production> grammar) -> ftxui::Element {
  switch (step.action) {
    case ll1::TraceAction::expand:
      if (step.production.has_value()) {
        return production_element(production_at(grammar, *step.production));
      }
      return ftxui::text("?");
    case ll1::TraceAction::match:
      if (step.token_index < parsed.tokens.size()) {
        const auto& token = parsed.tokens.at(step.token_index);
        return ftxui::text(token_text(token)) | ftxui::color(token_color(token.kind));
      }
      return ftxui::text("$") | ftxui::color(palette::muted());
    case ll1::TraceAction::accept:
      return ftxui::text("accepted") | ftxui::color(palette::success());
    case ll1::TraceAction::error:
      return ftxui::text("rejected") | ftxui::color(palette::failure());
  }
  return ftxui::text("?");
}

[[nodiscard]] auto rule() -> ftxui::Element {
  return ftxui::separator() | ftxui::color(palette::chrome());
}

[[nodiscard]] auto section_header(std::string_view label) -> ftxui::Element {
  return ftxui::text(" " + std::string{label}) | ftxui::bold | ftxui::color(palette::chrome());
}

[[nodiscard]] auto highlighted_source(const State& state) -> ftxui::Element {
  if (state.parsed.tokens.empty()) {
    return ftxui::text(state.source) | ftxui::color(palette::text());
  }
  const auto* step = current_step(state);
  const std::optional<std::size_t> active_token =
      step == nullptr ? std::nullopt : std::optional<std::size_t>{step->token_index};
  ftxui::Elements parts;
  std::size_t offset = 0;
  for (std::size_t index = 0; index < state.parsed.tokens.size(); ++index) {
    const auto& token = state.parsed.tokens.at(index);
    if (token.kind == sql::TokenKind::end_of_input) {
      continue;
    }
    const std::size_t start = token.position.offset > 0 ? token.position.offset - 1 : 0;
    if (start > offset) {
      parts.push_back(ftxui::text(std::string_view{state.source}.substr(offset, start - offset)) |
                      ftxui::color(palette::muted()));
    }
    std::size_t next = state.source.size();
    if (index + 1 < state.parsed.tokens.size()) {
      const std::size_t next_offset = state.parsed.tokens.at(index + 1).position.offset;
      next = next_offset > 0 ? next_offset - 1 : 0;
    }
    std::size_t end = std::min(next, state.source.size());
    while (end > start) {
      const char character = state.source.at(end - 1);
      if (character != ' ' && character != '\t' && character != '\n' && character != '\r') {
        break;
      }
      --end;
    }
    if (end > start) {
      auto segment = ftxui::text(std::string_view{state.source}.substr(start, end - start)) |
                     ftxui::color(token_color(token.kind));
      if (active_token.has_value() && index == *active_token) {
        segment |= ftxui::bold;
        segment |= ftxui::underlined;
      }
      parts.push_back(std::move(segment));
    }
    offset = std::max(offset, end);
  }
  if (offset < state.source.size()) {
    parts.push_back(ftxui::text(std::string_view{state.source}.substr(offset)) |
                    ftxui::color(palette::muted()));
  }
  if (parts.empty()) {
    return ftxui::text(state.source) | ftxui::color(palette::text());
  }
  return ftxui::hflow(std::move(parts));
}

[[nodiscard]] auto source_line(const State& state, const ftxui::Component& input)
    -> ftxui::Element {
  if (state.editing) {
    return ftxui::hbox({
        ftxui::text("SQL> ") | ftxui::bold | ftxui::color(palette::zone_source()),
        input->Render() | ftxui::flex,
    });
  }
  if (state.source.empty()) {
    return ftxui::hbox({
        ftxui::text("SQL> ") | ftxui::bold | ftxui::color(palette::zone_source()),
        ftxui::text("(vacío)") | ftxui::color(palette::muted()),
    });
  }
  return ftxui::hbox({
      ftxui::text("SQL> ") | ftxui::bold | ftxui::color(palette::zone_source()),
      highlighted_source(state) | ftxui::flex,
  });
}

[[nodiscard]] auto stack_pane(const State& state) -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(section_header("STACK · top first"));
  rows.push_back(rule());
  const auto* step = current_step(state);
  if (step == nullptr || step->stack.empty()) {
    rows.push_back(ftxui::text(" ε") | ftxui::color(palette::muted()));
  } else {
    const std::size_t count = std::min(step->stack.size(), max_stack_rows);
    for (std::size_t offset = 0; offset < count; ++offset) {
      const std::size_t index = step->stack.size() - 1 - offset;
      const auto& symbol = step->stack.at(index);
      auto row = ftxui::text(" " + symbol_name(symbol)) | ftxui::color(symbol_color(symbol));
      if (offset == 0) {
        row |= ftxui::bold;
        row |= ftxui::underlined;
      }
      rows.push_back(std::move(row));
    }
    if (step->stack.size() > count) {
      rows.push_back(ftxui::text(" ...") | ftxui::color(palette::muted()));
    }
  }
  return ftxui::vbox(std::move(rows)) | ftxui::flex;
}

[[nodiscard]] auto last_table_cell(const State& state) -> std::optional<ll1::Cell> {
  const std::size_t end = std::min(state.step + 1, state.parsed.steps.size());
  for (std::size_t index = end; index > 0; --index) {
    const auto& previous = state.parsed.steps.at(index - 1);
    if (previous.cell.has_value()) {
      return previous.cell;
    }
  }
  return std::nullopt;
}

[[nodiscard]] auto action_cell_line(const std::optional<ll1::Cell>& cell,
                                    const std::optional<std::size_t>& production)
    -> ftxui::Element {
  if (!cell.has_value()) {
    return ftxui::text("");
  }
  const auto& [nonterminal, terminal] = *cell;
  const std::string number = production.has_value() ? std::to_string(*production) : "?";
  return ftxui::hbox({
      ftxui::text(" A=") | ftxui::color(palette::muted()),
      ftxui::text(nonterminal_name(nonterminal)) | ftxui::color(palette::nonterminal(nonterminal)),
      ftxui::text("  a=") | ftxui::color(palette::muted()),
      ftxui::text(terminal_name(terminal)) | ftxui::color(terminal_color(terminal)),
      ftxui::text("  M[A,a]=p" + number) | ftxui::color(palette::muted()),
  });
}

[[nodiscard]] auto table_row(const ll1::Terminal& terminal, std::size_t production, bool active)
    -> ftxui::Element {
  auto row = ftxui::hbox({
      ftxui::text(" " + terminal_name(terminal)) | ftxui::color(terminal_color(terminal)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, lookahead_width),
      ftxui::text(std::format("p{}", production)) | ftxui::color(palette::text()) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, production_width),
  });
  if (active) {
    row |= ftxui::bold;
    row |= ftxui::underlined;
    row |= ftxui::focus;
  }
  return row;
}

[[nodiscard]] auto table_pane(const State& state, const ll1::ParseTable& table) -> ftxui::Element {
  const auto* step = current_step(state);
  const auto current_cell = step == nullptr ? std::optional<ll1::Cell>{} : step->cell;
  const auto current_production = step == nullptr ? std::optional<std::size_t>{} : step->production;
  const auto previous_cell = last_table_cell(state);
  const ll1::Nonterminal active_row = current_cell.has_value()    ? current_cell->first
                                      : previous_cell.has_value() ? previous_cell->first
                                                                  : ll1::Nonterminal::statement;
  const auto display_cell = current_cell.has_value() ? current_cell : previous_cell;
  std::optional<std::size_t> display_production = current_production;
  if (display_cell.has_value() && table.contains(*display_cell)) {
    display_production = table.at(*display_cell);
  }
  ftxui::Elements rows;

  if (!state.full_table) {
    rows.push_back(ftxui::hbox({
        ftxui::text(" M[A,a] · row ") | ftxui::bold | ftxui::color(palette::chrome()),
        ftxui::text(nonterminal_name(active_row)) | ftxui::bold |
            ftxui::color(palette::nonterminal(active_row)),
        ftxui::text("   (Tab: all)") | ftxui::color(palette::muted()),
    }));
    rows.push_back(action_cell_line(display_cell, display_production));
    rows.push_back(ftxui::hbox({
        ftxui::text(" lookahead") | ftxui::color(palette::chrome()) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, lookahead_width),
        ftxui::text("prod") | ftxui::color(palette::chrome()) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, production_width),
    }));
    rows.push_back(rule());
    ftxui::Elements entries;
    for (const auto& [cell, production] : table) {
      if (cell.first != active_row) {
        continue;
      }
      const bool active = display_cell.has_value() && cell == *display_cell;
      entries.push_back(table_row(cell.second, production, active));
    }
    if (entries.empty()) {
      entries.push_back(ftxui::text(" (no entries)") | ftxui::color(palette::muted()));
    }
    rows.push_back(ftxui::vbox(std::move(entries)) | ftxui::yframe | ftxui::flex);
  } else {
    rows.push_back(ftxui::hbox({
        ftxui::text(" M[A,a] · all rows") | ftxui::bold | ftxui::color(palette::chrome()),
        ftxui::text("   (Tab: row)") | ftxui::color(palette::muted()),
    }));
    rows.push_back(action_cell_line(display_cell, display_production));
    rows.push_back(ftxui::hbox({
        ftxui::text(" A") | ftxui::color(palette::chrome()) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, nonterminal_width),
        ftxui::text("a") | ftxui::color(palette::chrome()) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, lookahead_width),
        ftxui::text("prod") | ftxui::color(palette::chrome()) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, production_width),
    }));
    rows.push_back(rule());
    ftxui::Elements entries;
    for (const auto& [cell, production] : table) {
      auto row = ftxui::hbox({
          ftxui::text(" " + std::string{nonterminal_name(cell.first)}) |
              ftxui::color(palette::nonterminal(cell.first)) |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, nonterminal_width),
          ftxui::text(" " + terminal_name(cell.second)) |
              ftxui::color(terminal_color(cell.second)) |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, lookahead_width),
          ftxui::text(std::format("p{}", production)) | ftxui::color(palette::text()) |
              ftxui::size(ftxui::WIDTH, ftxui::EQUAL, production_width),
      });
      if (display_cell.has_value() && cell == *display_cell) {
        row |= ftxui::bold;
        row |= ftxui::underlined;
        row |= ftxui::focus;
      }
      entries.push_back(std::move(row));
    }
    rows.push_back(ftxui::vbox(std::move(entries)) | ftxui::yframe | ftxui::flex);
  }
  return ftxui::vbox(std::move(rows)) | ftxui::flex;
}

[[nodiscard]] auto trace_pane(const State& state, std::span<const ll1::Production> grammar,
                              int visible_height) -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(section_header("TRACE"));
  rows.push_back(rule());
  ftxui::Elements lines;
  if (state.parsed.steps.empty()) {
    lines.push_back(ftxui::text(" (no steps)") | ftxui::color(palette::muted()));
  } else {
    const std::size_t available =
        visible_height > layout_trace_header_rows
            ? static_cast<std::size_t>(visible_height - layout_trace_header_rows)
            : 1;
    const std::size_t end = std::min(state.step + 1, state.parsed.steps.size());
    const std::size_t begin = end > available ? end - available : 0;
    for (std::size_t index = begin; index < end; ++index) {
      const auto& step = state.parsed.steps.at(index);
      auto line = ftxui::hbox({
          ftxui::text(std::format(" {:>3}  ", index + 1)) | ftxui::color(palette::muted()),
          ftxui::text(std::format("{:<6}  ", action_name(step.action))) |
              ftxui::color(action_color(step.action)),
          step_detail_element(state.parsed, step, grammar),
      });
      if (index == state.step) {
        line |= ftxui::bold;
        line |= ftxui::underlined;
      }
      lines.push_back(std::move(line));
    }
  }
  rows.push_back(ftxui::vbox(std::move(lines)));
  return ftxui::vbox(std::move(rows));
}

[[nodiscard]] auto source_excerpt(std::string_view source, std::size_t line) -> std::string_view {
  if (line == 0) {
    return {};
  }
  std::size_t current = 1;
  std::size_t begin = 0;
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (source.at(index) != '\n') {
      continue;
    }
    if (current == line) {
      return source.substr(begin, index - begin);
    }
    ++current;
    begin = index + 1;
  }
  return current == line ? source.substr(begin) : std::string_view{};
}

[[nodiscard]] auto error_pane(const sql::SqlError& error, std::string_view source)
    -> ftxui::Element {
  ftxui::Elements header;
  header.push_back(ftxui::text(" ERROR ") | ftxui::bold | ftxui::color(palette::failure()));
  header.push_back(ftxui::text(error.sqlstate) | ftxui::bold | ftxui::color(palette::failure()));
  if (error.position.line != 0) {
    header.push_back(
        ftxui::text(std::format(" · {}:{}", error.position.line, error.position.column)) |
        ftxui::color(palette::muted()));
  }
  ftxui::Elements rows;
  rows.push_back(ftxui::hbox(std::move(header)));
  rows.push_back(ftxui::paragraph(error.message) | ftxui::color(palette::failure()));
  const auto excerpt = source_excerpt(source, error.position.line);
  if (!excerpt.empty()) {
    rows.push_back(ftxui::text(excerpt) | ftxui::color(palette::text()));
    if (error.position.column > 0) {
      const std::size_t caret = std::min(error.position.column - 1, excerpt.size());
      rows.push_back(ftxui::text(std::string(caret, ' ') + "^") | ftxui::color(palette::failure()));
    }
  }
  return ftxui::vbox(std::move(rows));
}

[[nodiscard]] auto status_text(const State& state, bool playing)
    -> std::pair<std::string, ftxui::Color> {
  if (state.editing) {
    return {"EDIT", palette::zone_action()};
  }
  if (state.parsed.error.has_value()) {
    return {"ERROR", palette::failure()};
  }
  if (state.parsed.accepted && state.step >= state.parsed.steps.size()) {
    return {"ACCEPT", palette::success()};
  }
  return {playing ? "PLAY" : "PAUSED", palette::keyword()};
}

[[nodiscard]] auto header_line(const State& state, bool playing) -> ftxui::Element {
  const auto [status, color] = status_text(state, playing);
  const std::size_t total = state.parsed.steps.size();
  const std::size_t current = std::min(state.step + 1, total);
  return ftxui::hbox({
      ftxui::text(" kravidb") | ftxui::bold | ftxui::color(palette::zone_source()),
      ftxui::text(" | LL(1) SELECT") | ftxui::color(palette::text()),
      ftxui::text(" · " + state.title) | ftxui::color(palette::muted()),
      ftxui::filler(),
      ftxui::text(status) | ftxui::bold | ftxui::color(color),
      ftxui::text(std::format("  {}/{} ", current, total)) | ftxui::color(palette::muted()),
  });
}

[[nodiscard]] auto footer_line(const State& state) -> ftxui::Element {
  const std::string hint = state.editing
                               ? " Escribí SQL · Enter verificar · Esc cancelar "
                               : " E editar · ◀▶ paso · Space play · Tab tabla · D demo · Q salir ";
  return ftxui::hbox({
      ftxui::text(hint) | ftxui::color(palette::muted()),
  });
}

[[nodiscard]] auto render_document(const State& state, bool playing, const ll1::ParseTable& table,
                                   std::span<const ll1::Production> grammar,
                                   const ftxui::Component& input) -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(header_line(state, playing));
  rows.push_back(rule());
  rows.push_back(source_line(state, input));
  if (state.parsed.error.has_value()) {
    rows.push_back(rule());
    rows.push_back(error_pane(*state.parsed.error, state.source));
  }
  rows.push_back(rule());
  rows.push_back(ftxui::hbox({
                     stack_pane(state) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, stack_width),
                     rule(),
                     table_pane(state, table) | ftxui::flex |
                         ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, min_table_height),
                 }) |
                 ftxui::flex);
  const int content_height =
      std::max(min_content_height, ftxui::Terminal::Size().dimy - layout_border_rows);
  const int fixed_rows =
      layout_fixed_rows + (state.parsed.error.has_value() ? layout_error_rows : 0);
  const int trace_height =
      std::clamp((content_height - fixed_rows) / trace_divisor, min_trace_height, max_trace_height);
  const int step_rows = static_cast<int>(state.parsed.steps.size()) + layout_trace_header_rows;
  const int visible_trace_height =
      std::clamp(std::min(trace_height, step_rows), min_trace_rows, trace_height);
  rows.push_back(rule());
  rows.push_back(trace_pane(state, grammar, visible_trace_height) |
                 ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, visible_trace_height));
  rows.push_back(rule());
  rows.push_back(footer_line(state));
  return ftxui::hbox({
             ftxui::text(" "),
             ftxui::vbox(std::move(rows)) | ftxui::flex,
             ftxui::text(" "),
         }) |
         ftxui::flex | ftxui::borderStyled(ftxui::LIGHT, palette::chrome());
}

[[nodiscard]] auto default_demos() -> std::vector<Demo> {
  return {
      {"simple", "SELECT id FROM users;"},
      {"columns and AND", "SELECT id, name FROM users WHERE id >= 10 AND name <> 'ada';"},
      {"boolean precedence", "SELECT * FROM t WHERE a = 1 OR b = 2 AND c = 3;"},
      {"invalid token", "SELECT FROM users;"},
  };
}

[[nodiscard]] auto parse_args(std::span<char*> args) -> Options {
  Options options;
  auto iterator = args.begin();
  if (iterator == args.end()) {
    return options;
  }
  ++iterator;
  for (; iterator != args.end(); ++iterator) {
    const std::string_view argument{*iterator};
    if (argument == "--help" || argument == "-h") {
      options.help = true;
      continue;
    }
    if (argument == "--query") {
      ++iterator;
      if (iterator == args.end()) {
        options.error = true;
        return options;
      }
      options.query = std::string{*iterator};
      continue;
    }
    options.error = true;
    return options;
  }
  return options;
}

auto print_usage(std::ostream& output) -> void {
  output << "Usage: kravidb_parser [--query <sql>]\n\n"
            "  (no arguments)       interactive LL(1) SELECT visualizer\n"
            "  --query <sql>        visualize one query\n"
            "  -h, --help           show this help\n";
}

auto load_demo(State& state, const Demo& demo) -> void {
  state.source = demo.sql;
  state.validated = demo.sql;
  state.title = demo.name;
  state.parsed = analyze(state.source);
  state.step = 0;
  state.editing = false;
  state.cursor = static_cast<int>(state.source.size());
}

auto run(const std::vector<Demo>& demos) -> void {
  State state;
  load_demo(state, demos.front());
  std::atomic<bool> playing{false};

  ftxui::InputOption input_option;
  input_option.content = &state.source;
  input_option.cursor_position = &state.cursor;
  input_option.multiline = false;
  // The default transform inverts the whole line; draw the line ourselves so
  // only the cursor cell gets a background.
  // shortcut: the cursor moves byte by byte, upgrade if UTF-8 editing matters.
  input_option.transform = [&](const ftxui::InputState& input_state) -> ftxui::Element {
    auto cursor_cell = ftxui::text(" ") | ftxui::bgcolor(palette::zone_source()) |
                       ftxui::color(palette::cursor_text());
    if (input_state.is_placeholder) {
      return ftxui::hbox({
                 std::move(cursor_cell),
                 ftxui::text("SELECT ...") | ftxui::dim | ftxui::color(palette::muted()),
             }) |
             ftxui::xflex | ftxui::xframe;
    }
    const std::size_t size = state.source.size();
    const auto cursor = std::min(static_cast<std::size_t>(std::max(state.cursor, 0)), size);
    const std::size_t next = std::min(cursor + 1, size);
    auto before = ftxui::text(std::string_view{state.source}.substr(0, cursor));
    auto cursor_char = cursor < size
                           ? ftxui::text(std::string_view{state.source}.substr(cursor, 1)) |
                                 ftxui::bgcolor(palette::zone_source()) |
                                 ftxui::color(palette::cursor_text())
                           : std::move(cursor_cell);
    auto after = ftxui::text(std::string_view{state.source}.substr(next));
    return ftxui::hbox({
               std::move(before),
               std::move(cursor_char),
               std::move(after),
           }) |
           ftxui::xflex | ftxui::xframe;
  };
  input_option.on_change = [&] -> void {
    state.parsed = analyze(state.source);
    state.step = 0;
    playing = false;
  };
  auto input = ftxui::Input(input_option);
  input->TakeFocus();

  const auto table = ll1::build_table(ll1::select_grammar());
  if (!table.has_value()) {
    std::cerr << "kravidb_parser: SELECT grammar has an LL(1) conflict\n";
    return;
  }
  const auto grammar = ll1::select_grammar();

  auto screen = ftxui::App::Fullscreen();
  screen.TrackMouse(false);
  auto root = ftxui::Renderer([&] -> ftxui::Element {
    return render_document(state, playing.load(), *table, grammar, input);
  });
  root = ftxui::CatchEvent(root, [&](const ftxui::Event& event) -> bool {
    if (state.editing) {
      if (event == ftxui::Event::Return) {
        state.validated = state.source;
        state.title = "custom";
        state.parsed = analyze(state.source);
        state.step = 0;
        state.editing = false;
        playing = true;
        return true;
      }
      if (event == ftxui::Event::Escape) {
        state.source = state.validated;
        state.parsed = analyze(state.source);
        state.step = 0;
        state.editing = false;
        return true;
      }
      return input->OnEvent(event);
    }
    if (event == ftxui::Event::Character('e') || event == ftxui::Event::Character('E') ||
        event == ftxui::Event::Character('/')) {
      state.editing = true;
      state.cursor = static_cast<int>(state.source.size());
      playing = false;
      input->TakeFocus();
      return true;
    }
    if (event == ftxui::Event::Escape || event == ftxui::Event::q || event == ftxui::Event::Q) {
      screen.Exit();
      return true;
    }
    if (event == ftxui::Event::Character(' ')) {
      playing = !playing.load();
      return true;
    }
    if (event == ftxui::Event::ArrowRight) {
      playing = false;
      state.step = std::min(state.step + 1, state.parsed.steps.size());
      return true;
    }
    if (event == ftxui::Event::ArrowLeft) {
      playing = false;
      state.step = state.step > 0 ? state.step - 1 : 0;
      return true;
    }
    if (event == ftxui::Event::Home) {
      playing = false;
      state.step = 0;
      return true;
    }
    if (event == ftxui::Event::End) {
      playing = false;
      state.step = state.parsed.steps.size();
      return true;
    }
    if (event == ftxui::Event::Tab) {
      state.full_table = !state.full_table;
      return true;
    }
    if (event == ftxui::Event::d || event == ftxui::Event::D) {
      state.demo = (state.demo + 1) % demos.size();
      load_demo(state, demos.at(state.demo));
      playing = true;
      return true;
    }
    if (event == ftxui::Event::Custom) {
      if (playing.load()) {
        if (state.step < state.parsed.steps.size()) {
          ++state.step;
        }
        if (state.step >= state.parsed.steps.size()) {
          playing = false;
        }
      }
      return true;
    }
    return false;
  });

  std::jthread ticker([&](const std::stop_token& stop) -> void {
    while (!stop.stop_requested()) {
      std::this_thread::sleep_for(play_interval);
      if (playing.load()) {
        screen.PostEvent(ftxui::Event::Custom);
      }
    }
  });
  screen.Loop(root);
  ticker.request_stop();
}

}  // namespace

auto main(int argc, char** argv) -> int {
  const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
  const Options options = parse_args(args);
  if (options.help) {
    print_usage(std::cout);
    return 0;
  }
  if (options.error) {
    print_usage(std::cerr);
    return 2;
  }

  std::vector<Demo> demos = default_demos();
  if (options.query.has_value()) {
    demos = {Demo{.name = "custom", .sql = *options.query}};
  }
  run(demos);
  return 0;
}
