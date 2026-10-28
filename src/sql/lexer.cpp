module sql.lexer;

import std;
import sql.error;

namespace kravidb::sql {
namespace {

constexpr std::size_t max_identifier_bytes = 63;

constexpr std::array<std::pair<std::string_view, Keyword>, 32> keyword_table{
    {
        {"add", Keyword::add},         {"alter", Keyword::alter},     {"and", Keyword::and_},
        {"bigint", Keyword::bigint},   {"column", Keyword::column},   {"create", Keyword::create},
        {"decimal", Keyword::decimal}, {"delete", Keyword::delete_},  {"drop", Keyword::drop},
        {"exists", Keyword::exists},   {"from", Keyword::from},       {"if", Keyword::if_},
        {"index", Keyword::index},     {"insert", Keyword::insert},   {"int8", Keyword::int8},
        {"into", Keyword::into},       {"is", Keyword::is},           {"key", Keyword::key},
        {"not", Keyword::not_},        {"null", Keyword::null},       {"numeric", Keyword::numeric},
        {"or", Keyword::or_},          {"primary", Keyword::primary}, {"select", Keyword::select},
        {"set", Keyword::set},         {"table", Keyword::table},     {"text", Keyword::text},
        {"unique", Keyword::unique},   {"update", Keyword::update},   {"values", Keyword::values},
        {"varchar", Keyword::varchar}, {"where", Keyword::where},
    },
};

[[nodiscard]] constexpr auto is_digit(char value) noexcept -> bool {
  return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr auto is_ascii_letter(char value) noexcept -> bool {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] constexpr auto is_identifier_start(char value) noexcept -> bool {
  return is_ascii_letter(value) || value == '_';
}

[[nodiscard]] constexpr auto is_identifier_continue(char value) noexcept -> bool {
  return is_identifier_start(value) || is_digit(value);
}

[[nodiscard]] constexpr auto is_space(char value) noexcept -> bool {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' ||
         value == '\v';
}

[[nodiscard]] constexpr auto lower_ascii(char value) noexcept -> char {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value - 'A' + 'a');
  }
  return value;
}

[[nodiscard]] auto keyword_from_text(std::string_view text) -> Keyword {
  const auto* const match = std::ranges::find_if(
      keyword_table, [text](const auto& entry) -> bool { return entry.first == text; });
  return match == keyword_table.end() ? Keyword::none : match->second;
}

class Lexer {
  public:
  explicit Lexer(std::string_view text) : text_{text} {}

  [[nodiscard]] auto run() -> SqlResult<std::vector<Token>>;

  private:
  [[nodiscard]] auto at_end() const noexcept -> bool { return index_ >= text_.size(); }

  [[nodiscard]] static auto position(std::size_t offset) noexcept -> SourcePosition {
    return SourcePosition{offset + 1};
  }

  auto fail(std::string message, std::size_t offset) -> void {
    if (!error_.has_value()) {
      error_ = SqlError{
          .sqlstate = "42601",
          .message = std::move(message),
          .position = position(offset),
      };
    }
  }

  auto skip_whitespace() noexcept -> void;
  auto scan_identifier_or_keyword() -> void;
  auto scan_number() -> void;
  auto scan_string() -> void;
  auto scan_operator_or_punctuation() -> void;

  std::string_view text_;
  std::size_t index_{0};
  std::vector<Token> tokens_;
  std::optional<SqlError> error_;
};

auto Lexer::run() -> SqlResult<std::vector<Token>> {
  while (!at_end() && !error_.has_value()) {
    skip_whitespace();
    if (at_end()) {
      break;
    }

    const char current = text_.at(index_);
    if (is_identifier_start(current)) {
      scan_identifier_or_keyword();
    } else if (is_digit(current) ||
               (current == '-' && index_ + 1 < text_.size() && is_digit(text_.at(index_ + 1)))) {
      scan_number();
    } else if (current == '\'') {
      scan_string();
    } else {
      scan_operator_or_punctuation();
    }
  }

  if (error_.has_value()) {
    return std::unexpected{std::move(error_.value())};
  }

  tokens_.emplace_back(TokenKind::end_of_input, "", position(text_.size()));
  return tokens_;
}

auto Lexer::skip_whitespace() noexcept -> void {
  while (!at_end() && is_space(text_.at(index_))) {
    ++index_;
  }
}

auto Lexer::scan_identifier_or_keyword() -> void {
  const std::size_t start = index_;
  while (!at_end() && is_identifier_continue(text_.at(index_))) {
    ++index_;
  }

  if (index_ - start > max_identifier_bytes) {
    fail("identifier exceeds 63 bytes", start);
    return;
  }

  std::string word{text_.substr(start, index_ - start)};
  for (char& value : word) {
    value = lower_ascii(value);
  }

  const Keyword keyword = keyword_from_text(word);
  if (keyword == Keyword::none) {
    tokens_.emplace_back(TokenKind::identifier, std::move(word), position(start));
  } else {
    tokens_.emplace_back(TokenKind::keyword, std::move(word), position(start), keyword);
  }
}

auto Lexer::scan_number() -> void {
  const std::size_t start = index_;
  if (text_.at(index_) == '-') {
    ++index_;
  }

  while (!at_end() && is_digit(text_.at(index_))) {
    ++index_;
  }

  bool is_decimal = false;
  if (!at_end() && text_.at(index_) == '.') {
    const std::size_t dot = index_;
    ++index_;
    if (at_end() || !is_digit(text_.at(index_))) {
      fail("expected digit after decimal point", dot);
      return;
    }
    is_decimal = true;
    while (!at_end() && is_digit(text_.at(index_))) {
      ++index_;
    }
  }

  if (!at_end() && (is_identifier_start(text_.at(index_)) || text_.at(index_) == '.')) {
    fail("trailing junk after numeric literal", index_);
    return;
  }

  const TokenKind kind = is_decimal ? TokenKind::decimal_literal : TokenKind::integer_literal;
  tokens_.emplace_back(kind, std::string{text_.substr(start, index_ - start)}, position(start));
}

auto Lexer::scan_string() -> void {
  const std::size_t start = index_;
  ++index_;

  std::string value;
  while (!at_end()) {
    const char current = text_.at(index_);
    if (current == '\0') {
      fail("NUL byte in string literal", index_);
      return;
    }
    if (current == '\'') {
      if (index_ + 1 < text_.size() && text_.at(index_ + 1) == '\'') {
        value.push_back('\'');
        index_ += 2;
        continue;
      }
      ++index_;
      tokens_.emplace_back(TokenKind::string_literal, std::move(value), position(start));
      return;
    }
    value.push_back(current);
    ++index_;
  }

  fail("unterminated string literal", start);
}

auto Lexer::scan_operator_or_punctuation() -> void {
  const std::size_t start = index_;
  const char current = text_.at(index_);

  const auto push = [this, start](TokenKind kind, std::size_t length) -> void {
    tokens_.emplace_back(kind, std::string{text_.substr(start, length)}, position(start));
    index_ += length;
  };

  switch (current) {
    case ',':
      push(TokenKind::comma, 1);
      return;
    case '(':
      push(TokenKind::left_paren, 1);
      return;
    case ')':
      push(TokenKind::right_paren, 1);
      return;
    case '*':
      push(TokenKind::star, 1);
      return;
    case ';':
      push(TokenKind::semicolon, 1);
      return;
    case '=':
      push(TokenKind::equal, 1);
      return;
    case '!':
      if (index_ + 1 < text_.size() && text_.at(index_ + 1) == '=') {
        push(TokenKind::not_equal, 2);
        return;
      }
      fail("unexpected character", start);
      return;
    case '<':
      if (index_ + 1 < text_.size() && text_.at(index_ + 1) == '>') {
        push(TokenKind::not_equal, 2);
        return;
      }
      if (index_ + 1 < text_.size() && text_.at(index_ + 1) == '=') {
        push(TokenKind::less_equal, 2);
        return;
      }
      push(TokenKind::less, 1);
      return;
    case '>':
      if (index_ + 1 < text_.size() && text_.at(index_ + 1) == '=') {
        push(TokenKind::greater_equal, 2);
        return;
      }
      push(TokenKind::greater, 1);
      return;
    default:
      fail("unexpected character", start);
      return;
  }
}

}  // namespace
}  // namespace kravidb::sql

namespace kravidb::sql {

auto tokenize(std::string_view text) -> SqlResult<std::vector<Token>> {
  return Lexer{text}.run();
}

}  // namespace kravidb::sql
