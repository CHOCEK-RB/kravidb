/// \file
/// \brief SQL token model and lexer entry point.
///
/// \details `tokenize` converts a complete SQL text into tokens. It never
/// returns a partial token sequence: the first lexical error aborts the scan.

export module sql.lexer;

import std;
export import sql.error;

export namespace kravidb::sql {

/// \brief Lexical category of a token.
enum class TokenKind : std::uint8_t {
  identifier,
  keyword,
  integer_literal,
  decimal_literal,
  string_literal,
  comma,
  left_paren,
  right_paren,
  star,
  semicolon,
  equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
  end_of_input,
};

/// \brief Reserved SQL word carried by a keyword token.
enum class Keyword : std::uint8_t {
  none,
  add,
  alter,
  and_,
  bigint,
  column,
  create,
  decimal,
  delete_,
  drop,
  exists,
  from,
  if_,
  index,
  insert,
  int8,
  into,
  is,
  key,
  not_,
  null,
  numeric,
  or_,
  primary,
  select,
  set,
  table,
  text,
  unique,
  update,
  values,
  varchar,
  where,
};

/// \brief One token with owned text and a 1-based byte position.
struct Token {
  TokenKind kind;
  Keyword keyword{Keyword::none};
  std::string text;
  SourcePosition position;

  Token(TokenKind kind, std::string text, SourcePosition position, Keyword keyword = Keyword::none)
      : kind{kind}, keyword{keyword}, text{std::move(text)}, position{position} {}

  /// \brief Two tokens are equal when every field is equal.
  [[nodiscard]] auto operator==(const Token&) const -> bool = default;
};

/// \brief Converts a complete SQL text into tokens.
///
/// \details The returned vector always ends with `TokenKind::end_of_input`.
/// On error it returns `SqlError` with SQLSTATE `42601`.
[[nodiscard]] auto tokenize(std::string_view text) -> SqlResult<std::vector<Token>>;

}  // namespace kravidb::sql
