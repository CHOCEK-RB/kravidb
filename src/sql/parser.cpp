module sql.parser;

import std;
import sql.ast;
import sql.error;
import sql.lexer;

namespace kravidb::sql {
namespace {

/// \brief Deepest parenthesis nesting accepted before giving up.
constexpr std::size_t max_expression_depth = 256;

/// \brief Unsupported clause or statement word and the feature it names.
struct UnsupportedWord {
  std::string_view word;
  std::string_view feature;
};

/// \brief Words that start statements outside the supported subset.
constexpr std::array<UnsupportedWord, 8> unsupported_statement_words{{
    {.word = "begin", .feature = "transaction control statements"},
    {.word = "start", .feature = "transaction control statements"},
    {.word = "commit", .feature = "transaction control statements"},
    {.word = "end", .feature = "transaction control statements"},
    {.word = "rollback", .feature = "transaction control statements"},
    {.word = "abort", .feature = "transaction control statements"},
    {.word = "savepoint", .feature = "transaction control statements"},
    {.word = "release", .feature = "transaction control statements"},
}};

/// \brief Words that start clauses outside the supported SELECT subset.
constexpr std::array<UnsupportedWord, 16> unsupported_clause_words{{
    {.word = "join", .feature = "JOIN"},
    {.word = "inner", .feature = "JOIN"},
    {.word = "left", .feature = "JOIN"},
    {.word = "right", .feature = "JOIN"},
    {.word = "full", .feature = "JOIN"},
    {.word = "cross", .feature = "JOIN"},
    {.word = "natural", .feature = "JOIN"},
    {.word = "order", .feature = "ORDER BY"},
    {.word = "group", .feature = "GROUP BY"},
    {.word = "having", .feature = "HAVING"},
    {.word = "limit", .feature = "LIMIT"},
    {.word = "offset", .feature = "OFFSET"},
    {.word = "fetch", .feature = "FETCH"},
    {.word = "union", .feature = "UNION"},
    {.word = "intersect", .feature = "INTERSECT"},
    {.word = "except", .feature = "EXCEPT"},
}};

/// \brief Words that start predicates outside the supported subset.
constexpr std::array<UnsupportedWord, 5> unsupported_predicate_words{{
    {.word = "in", .feature = "IN"},
    {.word = "like", .feature = "LIKE"},
    {.word = "ilike", .feature = "ILIKE"},
    {.word = "between", .feature = "BETWEEN"},
    {.word = "similar", .feature = "SIMILAR TO"},
}};

template <std::size_t Size>
[[nodiscard]] auto find_unsupported(const std::array<UnsupportedWord, Size>& table,
                                    std::string_view word) -> std::optional<std::string_view> {
  const auto* const match = std::ranges::find_if(
      table, [word](const UnsupportedWord& entry) -> bool { return entry.word == word; });
  if (match == table.end()) {
    return std::nullopt;
  }
  return match->feature;
}

[[nodiscard]] auto comparison_operator(TokenKind kind) -> std::optional<BinaryOp> {
  switch (kind) {
    case TokenKind::equal:
      return BinaryOp::eq;
    case TokenKind::not_equal:
      return BinaryOp::ne;
    case TokenKind::less:
      return BinaryOp::lt;
    case TokenKind::less_equal:
      return BinaryOp::le;
    case TokenKind::greater:
      return BinaryOp::gt;
    case TokenKind::greater_equal:
      return BinaryOp::ge;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] auto make_binary(BinaryOp operation, SourcePosition position,
                               std::unique_ptr<Expr> left, std::unique_ptr<Expr> right)
    -> std::unique_ptr<Expr> {
  return std::make_unique<Expr>(
      Expr{.node = BinaryExpr{operation, position, std::move(left), std::move(right)}});
}

class Parser {
  public:
  explicit Parser(std::span<const Token> tokens) : tokens_{tokens} {}

  [[nodiscard]] auto run() -> SqlResult<Stmt>;

  private:
  [[nodiscard]] auto token_at(std::size_t offset) const -> const Token& {
    const std::size_t target = std::min(offset, tokens_.size() - 1);
    return *std::next(tokens_.begin(), static_cast<std::ptrdiff_t>(target));
  }

  [[nodiscard]] auto current() const -> const Token& { return token_at(index_); }

  [[nodiscard]] auto peek_kind(std::size_t ahead) const -> TokenKind {
    return token_at(index_ + ahead).kind;
  }

  [[nodiscard]] auto at(TokenKind kind) const -> bool { return current().kind == kind; }

  [[nodiscard]] auto at_keyword(Keyword keyword) const -> bool {
    return current().kind == TokenKind::keyword && current().keyword == keyword;
  }

  auto advance() -> const Token& {
    const Token& token = current();
    if (token.kind != TokenKind::end_of_input) {
      ++index_;
    }
    return token;
  }

  [[nodiscard]] static auto syntax_error(const Token& token) -> SqlError {
    std::string message = token.kind == TokenKind::end_of_input
                              ? std::string{"syntax error at end of input"}
                              : std::format("syntax error at or near \"{}\"", token.text);
    return SqlError{
        .sqlstate = std::string{sqlstate::syntax_error},
        .message = std::move(message),
        .position = token.position,
    };
  }

  [[nodiscard]] static auto unsupported(std::string_view feature, const Token& token) -> SqlError {
    return SqlError{
        .sqlstate = std::string{sqlstate::feature_not_supported},
        .message = std::format("{} is not supported", feature),
        .position = token.position,
    };
  }

  [[nodiscard]] auto expect_keyword(Keyword keyword) -> SqlResult<const Token*> {
    if (!at_keyword(keyword)) {
      return std::unexpected{syntax_error(current())};
    }
    return &advance();
  }

  [[nodiscard]] auto expect_identifier() -> SqlResult<Identifier>;
  [[nodiscard]] auto parse_statement() -> SqlResult<Stmt>;
  [[nodiscard]] auto parse_select() -> SqlResult<SelectStmt>;
  [[nodiscard]] auto parse_insert() -> SqlResult<InsertStmt>;
  [[nodiscard]] auto parse_update() -> SqlResult<UpdateStmt>;
  [[nodiscard]] auto parse_delete() -> SqlResult<DeleteStmt>;
  [[nodiscard]] auto parse_identifier_list() -> SqlResult<std::vector<Identifier>>;
  [[nodiscard]] auto parse_literal_list() -> SqlResult<std::vector<Literal>>;
  [[nodiscard]] auto parse_assignments() -> SqlResult<std::vector<Assignment>>;
  [[nodiscard]] auto parse_target_list() -> SqlResult<std::vector<Identifier>>;
  [[nodiscard]] auto parse_target() -> SqlResult<Identifier>;
  [[nodiscard]] auto parse_expr() -> SqlResult<std::unique_ptr<Expr>>;
  [[nodiscard]] auto parse_and_expr() -> SqlResult<std::unique_ptr<Expr>>;
  [[nodiscard]] auto parse_predicate() -> SqlResult<std::unique_ptr<Expr>>;
  [[nodiscard]] auto parse_primary() -> SqlResult<std::unique_ptr<Expr>>;
  [[nodiscard]] auto parse_literal() -> SqlResult<Literal>;

  /// \brief Error for the token that follows a complete clause.
  [[nodiscard]] auto trailing_clause_error() const -> SqlError;

  std::span<const Token> tokens_;
  std::size_t index_{0};
  std::size_t depth_{0};
};

auto Parser::run() -> SqlResult<Stmt> {
  if (tokens_.empty() || tokens_.back().kind != TokenKind::end_of_input) {
    return std::unexpected{SqlError{
        .sqlstate = std::string{sqlstate::syntax_error},
        .message = "token sequence must end with end_of_input",
        .position = SourcePosition{},
    }};
  }

  if (at(TokenKind::end_of_input) || at(TokenKind::semicolon)) {
    return std::unexpected{SqlError{
        .sqlstate = std::string{sqlstate::syntax_error},
        .message = "empty statement",
        .position = current().position,
    }};
  }

  auto statement = parse_statement();
  if (!statement.has_value()) {
    return statement;
  }

  if (at(TokenKind::semicolon)) {
    advance();
    if (!at(TokenKind::end_of_input)) {
      return std::unexpected{SqlError{
          .sqlstate = std::string{sqlstate::syntax_error},
          .message = "only one statement is allowed per query",
          .position = current().position,
      }};
    }
  }

  if (!at(TokenKind::end_of_input)) {
    return std::unexpected{trailing_clause_error()};
  }
  return statement;
}

auto Parser::trailing_clause_error() const -> SqlError {
  const Token& token = current();
  if (token.kind == TokenKind::identifier) {
    if (const auto feature = find_unsupported(unsupported_clause_words, token.text)) {
      return unsupported(*feature, token);
    }
  }
  return syntax_error(token);
}

auto Parser::expect_identifier() -> SqlResult<Identifier> {
  if (!at(TokenKind::identifier)) {
    return std::unexpected{syntax_error(current())};
  }
  const Token& token = advance();
  return Identifier{.name = token.text, .position = token.position};
}

auto Parser::parse_statement() -> SqlResult<Stmt> {
  const Token& first = current();

  if (first.kind == TokenKind::keyword) {
    switch (first.keyword) {
      case Keyword::select: {
        auto select = parse_select();
        if (!select.has_value()) {
          return std::unexpected{std::move(select.error())};
        }
        return Stmt{std::move(select.value())};
      }
      case Keyword::insert:
        if (auto result = parse_insert(); result.has_value()) {
          return Stmt{std::move(result.value())};
        } else {
          return std::unexpected{std::move(result.error())};
        }
      case Keyword::update:
        if (auto result = parse_update(); result.has_value()) {
          return Stmt{std::move(result.value())};
        } else {
          return std::unexpected{std::move(result.error())};
        }
      case Keyword::delete_:
        if (auto result = parse_delete(); result.has_value()) {
          return Stmt{std::move(result.value())};
        } else {
          return std::unexpected{std::move(result.error())};
        }
      case Keyword::create:
        return std::unexpected{unsupported("CREATE", first)};
      case Keyword::drop:
        return std::unexpected{unsupported("DROP", first)};
      case Keyword::alter:
        return std::unexpected{unsupported("ALTER", first)};
      default:
        return std::unexpected{syntax_error(first)};
    }
  }

  if (first.kind == TokenKind::identifier) {
    if (const auto feature = find_unsupported(unsupported_statement_words, first.text)) {
      return std::unexpected{unsupported(*feature, first)};
    }
  }
  return std::unexpected{syntax_error(first)};
}

auto Parser::parse_identifier_list() -> SqlResult<std::vector<Identifier>> {
  std::vector<Identifier> identifiers;
  while (true) {
    auto identifier = expect_identifier();
    if (!identifier.has_value()) {
      return std::unexpected{std::move(identifier.error())};
    }
    identifiers.push_back(std::move(identifier.value()));
    if (!at(TokenKind::comma)) {
      return identifiers;
    }
    advance();
  }
}

auto Parser::parse_literal_list() -> SqlResult<std::vector<Literal>> {
  std::vector<Literal> literals;
  while (true) {
    auto literal = parse_literal();
    if (!literal.has_value()) {
      return std::unexpected{std::move(literal.error())};
    }
    literals.push_back(std::move(literal.value()));
    if (!at(TokenKind::comma)) {
      return literals;
    }
    advance();
  }
}

auto Parser::parse_insert() -> SqlResult<InsertStmt> {
  advance();
  if (auto into = expect_keyword(Keyword::into); !into.has_value()) {
    return std::unexpected{std::move(into.error())};
  }
  auto table = expect_identifier();
  if (!table.has_value()) {
    return std::unexpected{std::move(table.error())};
  }
  std::vector<Identifier> columns;
  if (at(TokenKind::left_paren)) {
    advance();
    auto list = parse_identifier_list();
    if (!list.has_value()) {
      return std::unexpected{std::move(list.error())};
    }
    columns = std::move(list.value());
    if (!at(TokenKind::right_paren)) {
      return std::unexpected{syntax_error(current())};
    }
    advance();
  }
  if (auto values = expect_keyword(Keyword::values); !values.has_value()) {
    return std::unexpected{std::move(values.error())};
  }
  if (!at(TokenKind::left_paren)) {
    return std::unexpected{syntax_error(current())};
  }
  advance();
  auto literals = parse_literal_list();
  if (!literals.has_value()) {
    return std::unexpected{std::move(literals.error())};
  }
  if (!at(TokenKind::right_paren)) {
    return std::unexpected{syntax_error(current())};
  }
  advance();
  return InsertStmt{.table = std::move(table.value()),
                    .columns = std::move(columns),
                    .values = std::move(literals.value())};
}

auto Parser::parse_assignments() -> SqlResult<std::vector<Assignment>> {
  std::vector<Assignment> assignments;
  while (true) {
    auto column = expect_identifier();
    if (!column.has_value()) {
      return std::unexpected{std::move(column.error())};
    }
    if (!at(TokenKind::equal)) {
      return std::unexpected{syntax_error(current())};
    }
    advance();
    auto value = parse_literal();
    if (!value.has_value()) {
      return std::unexpected{std::move(value.error())};
    }
    assignments.push_back(
        Assignment{.column = std::move(column.value()), .value = std::move(value.value())});
    if (!at(TokenKind::comma)) {
      return assignments;
    }
    advance();
  }
}

auto Parser::parse_update() -> SqlResult<UpdateStmt> {
  advance();
  auto table = expect_identifier();
  if (!table.has_value()) {
    return std::unexpected{std::move(table.error())};
  }
  if (auto set = expect_keyword(Keyword::set); !set.has_value()) {
    return std::unexpected{std::move(set.error())};
  }
  auto assignments = parse_assignments();
  if (!assignments.has_value()) {
    return std::unexpected{std::move(assignments.error())};
  }
  std::unique_ptr<Expr> where;
  if (at_keyword(Keyword::where)) {
    advance();
    auto expression = parse_expr();
    if (!expression.has_value()) {
      return std::unexpected{std::move(expression.error())};
    }
    where = std::move(expression.value());
  }
  return UpdateStmt{.table = std::move(table.value()),
                    .assignments = std::move(assignments.value()),
                    .where = std::move(where)};
}

auto Parser::parse_delete() -> SqlResult<DeleteStmt> {
  advance();
  if (auto from = expect_keyword(Keyword::from); !from.has_value()) {
    return std::unexpected{std::move(from.error())};
  }
  auto table = expect_identifier();
  if (!table.has_value()) {
    return std::unexpected{std::move(table.error())};
  }
  std::unique_ptr<Expr> where;
  if (at_keyword(Keyword::where)) {
    advance();
    auto expression = parse_expr();
    if (!expression.has_value()) {
      return std::unexpected{std::move(expression.error())};
    }
    where = std::move(expression.value());
  }
  return DeleteStmt{.table = std::move(table.value()), .where = std::move(where)};
}

auto Parser::parse_select() -> SqlResult<SelectStmt> {
  if (auto select = expect_keyword(Keyword::select); !select.has_value()) {
    return std::unexpected{std::move(select.error())};
  }

  auto targets = parse_target_list();
  if (!targets.has_value()) {
    return std::unexpected{std::move(targets.error())};
  }

  if (!at_keyword(Keyword::from)) {
    if (at(TokenKind::identifier) && current().text == "as") {
      return std::unexpected{unsupported("column aliases", current())};
    }
    return std::unexpected{syntax_error(current())};
  }
  advance();

  auto table = expect_identifier();
  if (!table.has_value()) {
    if (at(TokenKind::left_paren)) {
      return std::unexpected{unsupported("subqueries in FROM", current())};
    }
    return std::unexpected{std::move(table.error())};
  }

  if (at(TokenKind::comma)) {
    return std::unexpected{unsupported("multiple tables in FROM", current())};
  }
  if (at(TokenKind::identifier) && current().text == "as") {
    return std::unexpected{unsupported("table aliases", current())};
  }

  SelectStmt statement{
      .targets = std::move(targets.value()),
      .table = std::move(table.value()),
      .where = nullptr,
  };

  if (at_keyword(Keyword::where)) {
    advance();
    auto where = parse_expr();
    if (!where.has_value()) {
      return std::unexpected{std::move(where.error())};
    }
    statement.where = std::move(where.value());
  }

  return statement;
}

auto Parser::parse_target_list() -> SqlResult<std::vector<Identifier>> {
  std::vector<Identifier> targets;

  if (at(TokenKind::star)) {
    advance();
    return targets;
  }

  // `distinct` and `all` stay usable as column names: only a following
  // column or '*' turns them into the unsupported set quantifier.
  const bool quantifier_follows =
      peek_kind(1) == TokenKind::identifier || peek_kind(1) == TokenKind::star;
  if (at(TokenKind::identifier) && (current().text == "distinct" || current().text == "all") &&
      quantifier_follows) {
    return std::unexpected{unsupported("SELECT DISTINCT", current())};
  }

  while (true) {
    auto target = parse_target();
    if (!target.has_value()) {
      return std::unexpected{std::move(target.error())};
    }
    targets.push_back(std::move(target.value()));

    if (!at(TokenKind::comma)) {
      return targets;
    }
    advance();
  }
}

auto Parser::parse_target() -> SqlResult<Identifier> {
  const Token& token = current();
  switch (token.kind) {
    case TokenKind::identifier:
      if (peek_kind(1) == TokenKind::left_paren) {
        return std::unexpected{unsupported("function calls and aggregates", token)};
      }
      return expect_identifier();
    case TokenKind::star:
      return std::unexpected{unsupported("'*' mixed with columns", token)};
    case TokenKind::integer_literal:
    case TokenKind::decimal_literal:
    case TokenKind::string_literal:
      return std::unexpected{unsupported("expressions in the select list", token)};
    case TokenKind::keyword:
      if (token.keyword == Keyword::null) {
        return std::unexpected{unsupported("expressions in the select list", token)};
      }
      return std::unexpected{syntax_error(token)};
    default:
      return std::unexpected{syntax_error(token)};
  }
}

auto Parser::parse_expr() -> SqlResult<std::unique_ptr<Expr>> {
  auto left = parse_and_expr();
  if (!left.has_value()) {
    return left;
  }

  while (at_keyword(Keyword::or_)) {
    const SourcePosition position = advance().position;
    auto right = parse_and_expr();
    if (!right.has_value()) {
      return right;
    }
    left = make_binary(BinaryOp::or_, position, std::move(left.value()), std::move(right.value()));
  }
  return left;
}

auto Parser::parse_and_expr() -> SqlResult<std::unique_ptr<Expr>> {
  auto left = parse_predicate();
  if (!left.has_value()) {
    return left;
  }

  while (at_keyword(Keyword::and_)) {
    const SourcePosition position = advance().position;
    auto right = parse_predicate();
    if (!right.has_value()) {
      return right;
    }
    left = make_binary(BinaryOp::and_, position, std::move(left.value()), std::move(right.value()));
  }
  return left;
}

auto Parser::parse_predicate() -> SqlResult<std::unique_ptr<Expr>> {
  if (at_keyword(Keyword::not_)) {
    return std::unexpected{unsupported("NOT outside IS NOT NULL", current())};
  }

  auto left = parse_primary();
  if (!left.has_value()) {
    return left;
  }

  if (const auto operation = comparison_operator(current().kind)) {
    const SourcePosition position = advance().position;
    auto right = parse_primary();
    if (!right.has_value()) {
      return right;
    }
    return make_binary(*operation, position, std::move(left.value()), std::move(right.value()));
  }

  if (at_keyword(Keyword::is)) {
    const SourcePosition position = advance().position;
    BinaryOp operation = BinaryOp::is_null;
    if (at_keyword(Keyword::not_)) {
      advance();
      operation = BinaryOp::is_not_null;
    }
    if (auto null = expect_keyword(Keyword::null); !null.has_value()) {
      return std::unexpected{std::move(null.error())};
    }
    return make_binary(operation, position, std::move(left.value()), nullptr);
  }

  if (at_keyword(Keyword::not_)) {
    return std::unexpected{unsupported("NOT outside IS NOT NULL", current())};
  }
  if (at(TokenKind::identifier)) {
    if (const auto feature = find_unsupported(unsupported_predicate_words, current().text)) {
      return std::unexpected{unsupported(*feature, current())};
    }
  }
  return left;
}

auto Parser::parse_primary() -> SqlResult<std::unique_ptr<Expr>> {
  const Token& token = current();

  switch (token.kind) {
    case TokenKind::left_paren: {
      if (depth_ >= max_expression_depth) {
        return std::unexpected{SqlError{
            .sqlstate = std::string{sqlstate::statement_too_complex},
            .message = "expression nesting is too deep",
            .position = token.position,
        }};
      }
      advance();
      if (at_keyword(Keyword::select)) {
        return std::unexpected{unsupported("subqueries", current())};
      }
      ++depth_;
      auto inner = parse_expr();
      --depth_;
      if (!inner.has_value()) {
        return inner;
      }
      if (!at(TokenKind::right_paren)) {
        return std::unexpected{syntax_error(current())};
      }
      advance();
      return inner;
    }
    case TokenKind::identifier: {
      if (peek_kind(1) == TokenKind::left_paren) {
        return std::unexpected{unsupported("function calls and aggregates", token)};
      }
      auto name = expect_identifier();
      if (!name.has_value()) {
        return std::unexpected{std::move(name.error())};
      }
      return std::make_unique<Expr>(Expr{.node = ColumnRef{.name = std::move(name.value())}});
    }
    case TokenKind::integer_literal:
    case TokenKind::decimal_literal:
    case TokenKind::string_literal:
    case TokenKind::keyword: {
      auto literal = parse_literal();
      if (!literal.has_value()) {
        return std::unexpected{std::move(literal.error())};
      }
      return std::make_unique<Expr>(Expr{.node = std::move(literal.value())});
    }
    default:
      return std::unexpected{syntax_error(token)};
  }
}

auto Parser::parse_literal() -> SqlResult<Literal> {
  const Token& token = current();

  switch (token.kind) {
    case TokenKind::integer_literal: {
      advance();
      std::int64_t value{0};
      const char* const first = token.text.data();
      const char* const last = std::next(first, static_cast<std::ptrdiff_t>(token.text.size()));
      const auto [end, error] = std::from_chars(first, last, value);
      // 0 = no error. std::errc{} is avoided because clang-tidy flags it under libc++.
      if (static_cast<int>(error) == 0 && end == last) {
        return Literal{.value = value, .position = token.position};
      }
      // Too large for BIGINT: keep it exact and let the analyzer decide.
      return Literal{.value = DecimalLiteral{.text = token.text}, .position = token.position};
    }
    case TokenKind::decimal_literal:
      advance();
      return Literal{.value = DecimalLiteral{.text = token.text}, .position = token.position};
    case TokenKind::string_literal:
      advance();
      return Literal{.value = token.text, .position = token.position};
    case TokenKind::keyword:
      if (token.keyword == Keyword::null) {
        advance();
        return Literal{.value = NullLiteral{}, .position = token.position};
      }
      return std::unexpected{syntax_error(token)};
    default:
      return std::unexpected{syntax_error(token)};
  }
}

}  // namespace
}  // namespace kravidb::sql

namespace kravidb::sql {

auto parse_tokens(std::span<const Token> tokens) -> SqlResult<Stmt> {
  return Parser{tokens}.run();
}

auto parse(std::string_view text) -> SqlResult<Stmt> {
  auto tokens = tokenize(text);
  if (!tokens.has_value()) {
    return std::unexpected{std::move(tokens.error())};
  }
  return parse_tokens(tokens.value());
}

}  // namespace kravidb::sql
