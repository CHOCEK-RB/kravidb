/// \file
/// \brief SQL token viewer built on FTXUI.
///
/// Reads a statement from an interactive input, a file or standard input and
/// shows every token with its category, lexeme, line/column and byte offset.

import std;
import kravidb;

#include <cstdio>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

namespace sql = kravidb::sql;

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
[[nodiscard]] auto string() -> ftxui::Color {
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
[[nodiscard]] auto border() -> ftxui::Color {
  return ftxui::Color::RGB(100, 116, 139);
}

}  // namespace palette
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

constexpr int index_width = 4;
constexpr int category_width = 13;
constexpr int keyword_width = 10;
constexpr int lexeme_width = 24;
constexpr int position_width = 10;
constexpr int offset_width = 7;
constexpr int scrollbar_width = 1;
constexpr std::string_view ellipsis = "...";
// Rows used by the frame, input panel, legend and footer around the table.
constexpr int chrome_rows = 19;
constexpr int minimum_token_rows = 4;
constexpr int max_input_content_rows = 4;
// Border (2) + panel header (1) + separator (1).
constexpr int input_panel_chrome_rows = 4;

struct TokenView {
  sql::Token token;
  std::string lexeme;
  std::size_t line{0};
  std::size_t column{0};
};

struct Lexed {
  std::vector<TokenView> tokens;
  std::optional<sql::SqlError> error;
};

struct TokenWindow {
  std::size_t total;
  std::size_t first;
  std::size_t visible;
};

[[nodiscard]] auto kind_name(sql::TokenKind kind) -> std::string_view {
  switch (kind) {
    case sql::TokenKind::identifier:
      return "IDENTIFIER";
    case sql::TokenKind::keyword:
      return "KEYWORD";
    case sql::TokenKind::integer_literal:
      return "INTEGER";
    case sql::TokenKind::decimal_literal:
      return "DECIMAL";
    case sql::TokenKind::string_literal:
      return "STRING";
    case sql::TokenKind::comma:
      return "COMMA";
    case sql::TokenKind::left_paren:
      return "LPAREN";
    case sql::TokenKind::right_paren:
      return "RPAREN";
    case sql::TokenKind::star:
      return "STAR";
    case sql::TokenKind::semicolon:
      return "SEMICOLON";
    case sql::TokenKind::equal:
      return "EQUAL";
    case sql::TokenKind::not_equal:
      return "NOT_EQUAL";
    case sql::TokenKind::less:
      return "LESS";
    case sql::TokenKind::less_equal:
      return "LESS_EQUAL";
    case sql::TokenKind::greater:
      return "GREATER";
    case sql::TokenKind::greater_equal:
      return "GREATER_EQUAL";
    case sql::TokenKind::end_of_input:
      return "EOF";
  }
  return "UNKNOWN";
}

[[nodiscard]] auto category_color(sql::TokenKind kind) -> ftxui::Color {
  switch (kind) {
    case sql::TokenKind::identifier:
      return palette::identifier();
    case sql::TokenKind::keyword:
      return palette::keyword();
    case sql::TokenKind::integer_literal:
      return palette::integer();
    case sql::TokenKind::decimal_literal:
      return palette::decimal();
    case sql::TokenKind::string_literal:
      return palette::string();
    case sql::TokenKind::comma:
    case sql::TokenKind::left_paren:
    case sql::TokenKind::right_paren:
    case sql::TokenKind::star:
    case sql::TokenKind::semicolon:
      return palette::punctuation();
    case sql::TokenKind::equal:
    case sql::TokenKind::not_equal:
    case sql::TokenKind::less:
    case sql::TokenKind::less_equal:
    case sql::TokenKind::greater:
    case sql::TokenKind::greater_equal:
      return palette::operation();
    case sql::TokenKind::end_of_input:
      return palette::muted();
  }
  return palette::text();
}

[[nodiscard]] auto line_column(std::string_view source, std::size_t offset)
    -> std::pair<std::size_t, std::size_t> {
  if (offset == 0) {
    return {0, 0};
  }
  const std::size_t clamped = std::min(offset, source.size() + 1);
  const std::string_view prefix = source.substr(0, clamped - 1);
  const auto line = static_cast<std::size_t>(std::ranges::count(prefix, '\n')) + 1;
  const auto last_newline = prefix.rfind('\n');
  const std::size_t line_start = last_newline == std::string_view::npos ? 0 : last_newline + 1;
  return {line, clamped - line_start};
}

[[nodiscard]] auto source_line(std::string_view source, std::size_t line) -> std::string_view {
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
  if (current == line) {
    return source.substr(begin);
  }
  return {};
}

[[nodiscard]] auto raw_lexeme(std::string_view source, const std::vector<sql::Token>& tokens,
                              std::size_t index) -> std::string_view {
  const auto& token = tokens.at(index);
  if (token.kind == sql::TokenKind::end_of_input || token.position.offset == 0) {
    return {};
  }
  const std::size_t begin = token.position.offset - 1;
  if (begin >= source.size()) {
    return {};
  }
  std::size_t end = source.size();
  if (index + 1 < tokens.size()) {
    const std::size_t next = tokens.at(index + 1).position.offset;
    if (next == 0) {
      return {};
    }
    end = next - 1;
  }
  if (end > source.size() || end < begin) {
    return {};
  }
  std::string_view span = source.substr(begin, end - begin);
  while (!span.empty() && std::isspace(static_cast<unsigned char>(span.back())) != 0) {
    span.remove_suffix(1);
  }
  return span;
}

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

[[nodiscard]] auto clip(std::string_view value, int width) -> std::string {
  if (width <= 0) {
    return {};
  }
  const auto limit = static_cast<std::size_t>(width);
  if (value.size() <= limit) {
    return std::string{value};
  }
  if (limit <= ellipsis.size()) {
    return std::string{ellipsis.substr(0, limit)};
  }
  return std::string{value.substr(0, limit - ellipsis.size())} + std::string{ellipsis};
}

[[nodiscard]] auto lex(std::string_view source) -> Lexed {
  auto result = sql::tokenize(source);
  if (!result) {
    return Lexed{.tokens = {}, .error = std::move(result.error())};
  }
  const auto& tokens = *result;
  std::vector<TokenView> views;
  views.reserve(tokens.size());
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const auto& token = tokens.at(index);
    const auto [line, column] = line_column(source, token.position.offset);
    views.push_back(TokenView{
        .token = token,
        .lexeme = std::string{raw_lexeme(source, tokens, index)},
        .line = line,
        .column = column,
    });
  }
  return Lexed{.tokens = std::move(views), .error = std::nullopt};
}

[[nodiscard]] auto cell(std::string_view value, int width, ftxui::Color color) -> ftxui::Element {
  return ftxui::text(value) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width) | ftxui::color(color);
}

[[nodiscard]] auto header_cell(std::string_view value, int width) -> ftxui::Element {
  return ftxui::text(value) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width) | ftxui::bold;
}

[[nodiscard]] auto columns(ftxui::Elements cells) -> ftxui::Element {
  ftxui::Elements row;
  row.reserve(cells.size() * 2);
  for (std::size_t index = 0; index < cells.size(); ++index) {
    if (index != 0) {
      row.push_back(ftxui::text(" "));
    }
    row.push_back(std::move(cells.at(index)));
  }
  return ftxui::hbox(std::move(row));
}

[[nodiscard]] auto scrollbar_cell(bool thumb) -> ftxui::Element {
  return cell(thumb ? "█" : "│", scrollbar_width, thumb ? palette::identifier() : palette::muted());
}

[[nodiscard]] auto is_scrollbar_thumb(const TokenWindow& window, std::size_t row) -> bool {
  if (window.total <= window.visible) {
    return false;
  }
  const std::size_t thumb =
      std::max<std::size_t>(1, window.visible * window.visible / window.total);
  const std::size_t max_first = window.total - window.visible;
  const std::size_t start = (window.visible - thumb) * window.first / max_first;
  return row >= start && row < start + thumb;
}

[[nodiscard]] auto token_row(const TokenView& view, std::size_t index, bool show_scrollbar,
                             bool thumb) -> ftxui::Element {
  const auto& token = view.token;
  const bool is_eof = token.kind == sql::TokenKind::end_of_input;
  const std::string_view keyword =
      token.kind == sql::TokenKind::keyword ? std::string_view{token.text} : std::string_view{"-"};
  const std::string position = view.line == 0
                                   ? std::string{"-"}
                                   : std::to_string(view.line) + ":" + std::to_string(view.column);
  const std::string offset =
      token.position.offset == 0 ? std::string{"-"} : std::to_string(token.position.offset);
  const std::string lexeme = is_eof ? std::string{"\"\""} : escaped(view.lexeme);
  ftxui::Element keyword_cell =
      cell(clip(keyword, keyword_width), keyword_width, palette::keyword());
  if (token.kind == sql::TokenKind::keyword) {
    keyword_cell |= ftxui::bold;
  }
  ftxui::Elements cells{
      cell(std::to_string(index + 1), index_width, palette::muted()),
      cell(kind_name(token.kind), category_width, category_color(token.kind)),
      std::move(keyword_cell),
      cell(clip(lexeme, lexeme_width), lexeme_width, category_color(token.kind)),
      cell(position, position_width, palette::muted()),
      cell(offset, offset_width, palette::muted()),
  };
  if (show_scrollbar) {
    cells.push_back(scrollbar_cell(thumb));
  }
  return columns(std::move(cells));
}

[[nodiscard]] auto blank_row(bool show_scrollbar, bool thumb) -> ftxui::Element {
  ftxui::Elements cells{
      cell("", index_width, palette::muted()),    cell("", category_width, palette::muted()),
      cell("", keyword_width, palette::muted()),  cell("", lexeme_width, palette::muted()),
      cell("", position_width, palette::muted()), cell("", offset_width, palette::muted()),
  };
  if (show_scrollbar) {
    cells.push_back(scrollbar_cell(thumb));
  }
  return columns(std::move(cells));
}

[[nodiscard]] auto max_visible_input_rows(int terminal_height) -> int {
  const int available = terminal_height - chrome_rows - minimum_token_rows + 1;
  return std::clamp(available, 1, max_input_content_rows);
}

[[nodiscard]] auto input_content_rows(std::string_view source, int terminal_height) -> int {
  const auto lines = static_cast<int>(std::ranges::count(source, '\n')) + 1;
  return std::min(lines, max_visible_input_rows(terminal_height));
}

[[nodiscard]] auto input_panel_height(std::string_view source) -> int {
  return input_content_rows(source, ftxui::Terminal::Size().dimy) + input_panel_chrome_rows;
}

[[nodiscard]] auto visible_token_rows(std::string_view source) -> std::size_t {
  const int height = ftxui::Terminal::Size().dimy;
  const int content_rows = input_content_rows(source, height);
  const int rows = std::max(minimum_token_rows, height - chrome_rows - (content_rows - 1));
  return static_cast<std::size_t>(rows);
}

[[nodiscard]] auto max_first_token(std::size_t total, std::size_t visible) -> std::size_t {
  return total > visible ? total - visible : 0;
}

[[nodiscard]] auto token_table(const Lexed& lexed, std::size_t first, std::size_t visible,
                               bool interactive) -> ftxui::Element {
  ftxui::Elements header{
      header_cell("#", index_width),           header_cell("CATEGORY", category_width),
      header_cell("KEYWORD", keyword_width),   header_cell("LEXEME", lexeme_width),
      header_cell("LINE:COL", position_width), header_cell("OFFSET", offset_width),
  };
  if (interactive) {
    header.push_back(header_cell("", scrollbar_width));
  }
  ftxui::Elements rows;
  rows.push_back(columns(std::move(header)));
  rows.push_back(ftxui::separatorLight());
  if (lexed.tokens.empty()) {
    rows.push_back(ftxui::text("(no tokens)") | ftxui::color(palette::muted()));
  } else {
    const std::size_t total = lexed.tokens.size();
    const std::size_t begin = std::min(first, max_first_token(total, visible));
    const TokenWindow window{.total = total, .first = begin, .visible = visible};
    const std::size_t end = std::min(begin + visible, total);
    for (std::size_t index = begin; index < end; ++index) {
      const std::size_t row = index - begin;
      rows.push_back(
          token_row(lexed.tokens.at(index), index, interactive, is_scrollbar_thumb(window, row)));
    }
    if (interactive && total > visible) {
      for (std::size_t row = end - begin; row < visible; ++row) {
        rows.push_back(blank_row(true, is_scrollbar_thumb(window, row)));
      }
    }
  }
  return ftxui::vbox(std::move(rows)) | ftxui::borderStyled(palette::border());
}

[[nodiscard]] auto token_chips(const Lexed& lexed) -> ftxui::Element {
  if (lexed.error.has_value()) {
    return ftxui::text("(token stream unavailable after a lexical error)") |
           ftxui::color(palette::muted());
  }
  ftxui::Elements chips;
  for (const auto& view : lexed.tokens) {
    if (view.token.kind == sql::TokenKind::end_of_input) {
      chips.push_back(ftxui::text("<EOF>") | ftxui::color(palette::muted()));
      continue;
    }
    auto chip =
        ftxui::text(escaped(view.lexeme) + " ") | ftxui::color(category_color(view.token.kind));
    if (view.token.kind == sql::TokenKind::keyword) {
      chip |= ftxui::bold;
    }
    chips.push_back(std::move(chip));
  }
  if (chips.empty()) {
    return ftxui::text("(empty)") | ftxui::color(palette::muted());
  }
  return ftxui::hflow(std::move(chips));
}

[[nodiscard]] auto legend() -> ftxui::Element {
  const auto item = [](std::string_view label, ftxui::Color color) -> ftxui::Element {
    return ftxui::text(std::string{label} + "  ") | ftxui::color(color);
  };
  return ftxui::hflow({
      ftxui::text("colors: ") | ftxui::color(palette::muted()),
      item("keyword", palette::keyword()),
      item("identifier", palette::identifier()),
      item("integer", palette::integer()),
      item("decimal", palette::decimal()),
      item("string", palette::string()),
      item("operator", palette::operation()),
      item("punctuation", palette::punctuation()),
  });
}

[[nodiscard]] auto section_title(std::string_view label) -> ftxui::Element {
  return ftxui::text(label) | ftxui::bold | ftxui::color(palette::keyword());
}

[[nodiscard]] auto footer(const Lexed& lexed, bool interactive, std::size_t first,
                          std::size_t visible) -> ftxui::Element {
  const std::string count = std::to_string(lexed.tokens.size()) + " tokens";
  const bool failed = lexed.error.has_value();
  const std::string status = failed ? "lexical error" : "ok";
  const ftxui::Color status_color = failed ? palette::operation() : palette::string();
  ftxui::Elements parts{
      ftxui::text(count) | ftxui::color(palette::muted()),
      ftxui::text(" | ") | ftxui::color(palette::muted()),
      ftxui::text(status) | ftxui::color(status_color),
  };
  if (interactive) {
    if (lexed.tokens.size() > visible) {
      const std::size_t last = std::min(first + visible, lexed.tokens.size());
      parts.push_back(ftxui::text(" | " + std::to_string(first + 1) + "-" + std::to_string(last) +
                                  "/" + std::to_string(lexed.tokens.size())) |
                      ftxui::color(palette::muted()));
    }
    parts.push_back(ftxui::text(" | PgUp/PgDn | Ctrl+L clear | Esc/Ctrl+C") |
                    ftxui::color(palette::muted()));
  }
  return ftxui::hbox(std::move(parts));
}

[[nodiscard]] auto error_panel(std::string_view source, const sql::SqlError& error)
    -> ftxui::Element {
  const auto [line, column] = line_column(source, error.position.offset);
  ftxui::Elements details;
  details.push_back(ftxui::text("LEXICAL ERROR " + error.sqlstate) | ftxui::bold |
                    ftxui::color(palette::operation()));
  details.push_back(ftxui::paragraph(error.message) | ftxui::color(palette::operation()));
  if (line > 0) {
    const std::string_view text = source_line(source, line);
    details.push_back(ftxui::separator());
    details.push_back(ftxui::text(text) | ftxui::color(palette::text()));
    if (column > 0) {
      details.push_back(ftxui::text(std::string(column - 1, ' ') + "^") |
                        ftxui::color(palette::operation()));
    }
    details.push_back(
        ftxui::text("line " + std::to_string(line) + ", column " + std::to_string(column)) |
        ftxui::color(palette::muted()));
  }
  return ftxui::vbox(std::move(details)) | ftxui::borderStyled(palette::operation());
}

[[nodiscard]] auto build_document(std::string_view source, const Lexed& lexed, bool interactive,
                                  ftxui::Element input_row, std::size_t first, std::size_t visible)
    -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(ftxui::text("kravidb | SQL lexer") | ftxui::bold);
  rows.push_back(ftxui::separator());
  if (input_row != nullptr) {
    rows.push_back(std::move(input_row));
  } else {
    rows.push_back(section_title("Statement"));
    rows.push_back(source.empty() ? (ftxui::text("(empty)") | ftxui::color(palette::muted()))
                                  : (ftxui::paragraph(source) | ftxui::color(palette::text())));
  }
  if (!interactive) {
    rows.push_back(section_title("Token stream"));
    rows.push_back(token_chips(lexed));
  }
  rows.push_back(ftxui::separator());
  rows.push_back(token_table(lexed, first, visible, interactive));
  rows.push_back(legend());
  if (lexed.error.has_value()) {
    const auto& error = lexed.error.value();
    rows.push_back(error_panel(source, error));
  }
  rows.push_back(ftxui::separator());
  rows.push_back(footer(lexed, interactive, first, visible));
  auto document = ftxui::vbox(std::move(rows));
  if (interactive) {
    document = document | ftxui::flex;
  }
  return document | ftxui::borderStyled(palette::border());
}

[[nodiscard]] auto read_all(std::istream& input) -> std::string {
  return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

struct Options {
  std::string file;
  bool file_set{false};
  bool help{false};
  bool error{false};
};

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
    if (argument == "--file") {
      ++iterator;
      if (iterator == args.end()) {
        options.error = true;
        return options;
      }
      options.file = *iterator;
      options.file_set = true;
      continue;
    }
    options.error = true;
    return options;
  }
  return options;
}

auto print_usage(std::ostream& output) -> void {
  output << "Usage: kravidb_lexer [--file <path>]\n\n"
            "  (no arguments, terminal)  interactive token viewer\n"
            "  (no arguments, piped)     tokenize standard input once\n"
            "  --file <path>             tokenize a file once\n"
            "  -h, --help                show this help\n";
}

[[nodiscard]] auto stdin_is_terminal() -> bool {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(STDIN_FILENO) != 0;
#endif
}

auto scroll_tokens(std::size_t& first, std::size_t total, std::size_t visible, bool down) -> void {
  const std::size_t max_first = max_first_token(total, visible);
  if (down) {
    first = std::min(first + visible, max_first);
  } else {
    first = first > visible ? first - visible : 0;
  }
}

auto run_interactive() -> void {
  std::string input = "SELECT id, name FROM users WHERE id >= 10 AND name <> 'ada';";
  int cursor_position = static_cast<int>(input.size());
  std::size_t first_token = 0;

  ftxui::InputOption input_option;
  input_option.content = &input;
  input_option.placeholder = "SELECT ...";
  input_option.cursor_position = &cursor_position;
  // The default transform inverts the whole focused input, which hides the
  // cursor. Keep the text background neutral and signal focus with the border.
  input_option.transform = [](ftxui::InputState state) -> ftxui::Element {
    if (state.is_placeholder) {
      state.element |= ftxui::dim;
    }
    return state.element;
  };
  auto input_component = ftxui::Input(input_option);

  auto input_panel = [&] -> ftxui::Element {
    const int cursor = std::clamp(cursor_position, 0, static_cast<int>(input.size()));
    const auto [line, column] = line_column(input, static_cast<std::size_t>(cursor) + 1);
    return ftxui::vbox({
               ftxui::hbox({
                   ftxui::text("SQL") | ftxui::bold | ftxui::color(palette::keyword()),
                   ftxui::filler(),
                   ftxui::text("cursor " + std::to_string(line) + ":" + std::to_string(column)) |
                       ftxui::color(palette::identifier()),
               }),
               ftxui::separatorLight(),
               input_component->Render() | ftxui::vscroll_indicator | ftxui::yframe | ftxui::flex,
           }) |
           ftxui::borderStyled(palette::keyword()) |
           ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, input_panel_height(input));
  };

  auto screen = ftxui::App::Fullscreen();
  // Leave native mouse selection to the terminal; the viewer has no mouse actions.
  screen.TrackMouse(false);
  auto root = ftxui::Renderer(input_component, [&] -> ftxui::Element {
    const Lexed lexed = lex(input);
    const std::size_t visible = visible_token_rows(input);
    first_token = std::min(first_token, max_first_token(lexed.tokens.size(), visible));
    return build_document(input, lexed, true, input_panel(), first_token, visible);
  });
  root = ftxui::CatchEvent(root, [&](const ftxui::Event& event) -> bool {
    if (event == ftxui::Event::Escape) {
      screen.Exit();
      return true;
    }
    if (event == ftxui::Event::CtrlL) {
      input.clear();
      cursor_position = 0;
      first_token = 0;
      return true;
    }
    if (event == ftxui::Event::PageDown || event == ftxui::Event::PageUp) {
      const std::size_t total = lex(input).tokens.size();
      scroll_tokens(first_token, total, visible_token_rows(input), event == ftxui::Event::PageDown);
      return true;
    }
    return false;
  });
  screen.Loop(root);
}

auto run_static(std::string_view source) -> int {
  const Lexed lexed = lex(source);
  auto document = build_document(source, lexed, false, nullptr, 0, lexed.tokens.size());
  auto screen =
      ftxui::Screen::Create(ftxui::Dimension::Full(), ftxui::Dimension::Fit(document, true));
  ftxui::Render(screen, document);
  screen.Print();
  return lexed.error.has_value() ? 1 : 0;
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

  std::string source;
  if (options.file_set) {
    std::ifstream file{options.file, std::ios::binary};
    if (!file) {
      std::cerr << "kravidb_lexer: cannot open " << options.file << '\n';
      return 2;
    }
    source = read_all(file);
  }

  const bool interactive = !options.file_set && stdin_is_terminal();
  if (interactive) {
    run_interactive();
    return 0;
  }

  if (!options.file_set) {
    source = read_all(std::cin);
  }
  return run_static(source);
}
