module sql.select_ll1;

import std;
import sql.ast;

namespace kravidb::sql {
namespace {

using N = ll1::Nonterminal;
using T = ll1::Terminal;
using Set = std::set<T>;
constexpr std::size_t nonterminal_count = static_cast<std::size_t>(N::count);
constexpr std::size_t max_expression_depth = 256;

auto index_of(N symbol) -> std::size_t {
  return static_cast<std::size_t>(symbol);
}
auto keyword(Keyword word) -> T {
  return T{.kind = TokenKind::keyword, .keyword = word};
}
auto terminal(const Token& token) -> T {
  return T{.kind = token.kind,
           .keyword = token.kind == TokenKind::keyword ? token.keyword : Keyword::none};
}

/// \brief Renders a terminal for a diagnostic's expected-token list.
auto terminal_name(const T& term) -> std::string_view {
  switch (term.kind) {
    case TokenKind::identifier:
      return "identifier";
    case TokenKind::integer_literal:
      return "integer";
    case TokenKind::decimal_literal:
      return "decimal";
    case TokenKind::string_literal:
      return "string";
    case TokenKind::comma:
      return ",";
    case TokenKind::left_paren:
      return "(";
    case TokenKind::right_paren:
      return ")";
    case TokenKind::star:
      return "*";
    case TokenKind::semicolon:
      return ";";
    case TokenKind::equal:
      return "=";
    case TokenKind::not_equal:
      return "<>";
    case TokenKind::less:
      return "<";
    case TokenKind::less_equal:
      return "<=";
    case TokenKind::greater:
      return ">";
    case TokenKind::greater_equal:
      return ">=";
    case TokenKind::end_of_input:
      return "end of input";
    case TokenKind::keyword:
      switch (term.keyword) {
        case Keyword::select:
          return "SELECT";
        case Keyword::from:
          return "FROM";
        case Keyword::where:
          return "WHERE";
        case Keyword::or_:
          return "OR";
        case Keyword::and_:
          return "AND";
        case Keyword::is:
          return "IS";
        case Keyword::not_:
          return "NOT";
        case Keyword::null:
          return "NULL";
        case Keyword::true_:
          return "TRUE";
        case Keyword::false_:
          return "FALSE";
        default:
          return "keyword";
      }
  }
  return "token";
}

/// \brief Lists the table columns that can follow a variable, sorted and unique.
auto expected_terminals(const ll1::ParseTable& table, N variable) -> std::string {
  std::vector<std::string> names;
  for (const auto& entry : table) {
    if (entry.first.first == variable) {
      names.emplace_back(terminal_name(entry.first.second));
    }
  }
  std::ranges::sort(names);
  const auto [first, last] = std::ranges::unique(names);
  names.erase(first, last);
  std::string result;
  for (const auto& name : names) {
    if (!result.empty()) {
      result += ", ";
    }
    result += name;
  }
  return result;
}

/// \brief std::span has no at() in C++23; keep checked access at one boundary.
template <class Value>
auto checked_at(std::span<const Value> values, std::size_t offset) -> const Value& {
  if (offset >= values.size()) {
    throw std::out_of_range{"LL(1) span index out of range"};
  }
  return *std::next(values.begin(), static_cast<std::ptrdiff_t>(offset));
}

auto merge(Set& destination, const Set& source) -> bool {
  const auto size = destination.size();
  destination.insert(source.begin(), source.end());
  return destination.size() != size;
}

struct First {
  Set terminals;
  bool nullable{true};
};

auto first_of(std::span<const ll1::Symbol> symbols, const std::array<Set, nonterminal_count>& first,
              const std::array<bool, nonterminal_count>& nullable) -> First {
  First result;
  for (const auto& symbol : symbols) {
    if (const auto* token = std::get_if<T>(&symbol)) {
      result.terminals.insert(*token);
      result.nullable = false;
      break;
    }
    const auto variable = index_of(std::get<N>(symbol));
    merge(result.terminals, first.at(variable));
    if (!nullable.at(variable)) {
      result.nullable = false;
      break;
    }
  }
  return result;
}

auto syntax_error(const Token& token, std::string_view expected = {}) -> SqlError {
  std::string message = "syntax error";
  if (token.position.line != 0) {
    message += std::format(" at line {}, column {}", token.position.line, token.position.column);
  }
  message += token.kind == TokenKind::end_of_input ? " at end of input"
                                                   : std::format(" at or near \"{}\"", token.text);
  if (!expected.empty()) {
    message += std::format("; expected: {}", expected);
  }
  return SqlError{
      .sqlstate = std::string{sqlstate::syntax_error},
      .message = std::move(message),
      .position = token.position,
  };
}

auto unsupported(std::string_view feature, const Token& token) -> SqlError {
  return SqlError{.sqlstate = std::string{sqlstate::feature_not_supported},
                  .message = std::format("{} is not supported", feature),
                  .position = token.position};
}

auto clause_feature(std::string_view word) -> std::optional<std::string_view> {
  if (word == "join" || word == "inner" || word == "left" || word == "right" || word == "full" ||
      word == "cross" || word == "natural") {
    return "JOIN";
  }
  const std::array words{
      std::pair{"order", "ORDER BY"}, std::pair{"group", "GROUP BY"},
      std::pair{"having", "HAVING"},  std::pair{"limit", "LIMIT"},
      std::pair{"offset", "OFFSET"},  std::pair{"fetch", "FETCH"},
      std::pair{"union", "UNION"},    std::pair{"intersect", "INTERSECT"},
      std::pair{"except", "EXCEPT"},
  };
  for (const auto& [name, feature] : words) {
    if (word == name) {
      return feature;
    }
  }
  return std::nullopt;
}

/// \brief Context-sensitive diagnostics do not participate in rule selection.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto diagnostic(const ll1::Symbol& symbol, const Token& token, const Token& next,
                const Token* previous, std::size_t depth) -> std::optional<SqlError> {
  const auto* variable = std::get_if<N>(&symbol);
  const bool identifier = token.kind == TokenKind::identifier;
  if (variable != nullptr) {
    if (*variable == N::targets && identifier &&
        (token.text == "distinct" || token.text == "all") &&
        (next.kind == TokenKind::identifier || next.kind == TokenKind::star)) {
      return unsupported("SELECT DISTINCT", token);
    }
    if ((*variable == N::target || *variable == N::primary) && identifier &&
        next.kind == TokenKind::left_paren) {
      return unsupported("function calls and aggregates", token);
    }
    if (*variable == N::target || *variable == N::targets) {
      if (*variable == N::target && token.kind == TokenKind::star) {
        return unsupported("'*' mixed with columns", token);
      }
      if (token.kind == TokenKind::integer_literal || token.kind == TokenKind::decimal_literal ||
          token.kind == TokenKind::string_literal || terminal(token) == keyword(Keyword::null) ||
          terminal(token) == keyword(Keyword::true_) ||
          terminal(token) == keyword(Keyword::false_)) {
        return unsupported("expressions in the select list", token);
      }
    }
    if (*variable == N::table && token.kind == TokenKind::left_paren) {
      return unsupported("subqueries in FROM", token);
    }
    if (*variable == N::where_opt) {
      if (token.kind == TokenKind::comma) {
        return unsupported("multiple tables in FROM", token);
      }
      if (identifier && token.text == "as") {
        return unsupported("table aliases", token);
      }
    }
    if (*variable == N::targets_tail && identifier && token.text == "as") {
      return unsupported("column aliases", token);
    }
    if ((*variable == N::expr || *variable == N::and_expr || *variable == N::predicate ||
         *variable == N::predicate_tail) &&
        terminal(token) == keyword(Keyword::not_)) {
      return unsupported("NOT outside IS NOT NULL", token);
    }
    if (*variable == N::predicate_tail && identifier) {
      const std::array words{std::pair{"in", "IN"}, std::pair{"like", "LIKE"},
                             std::pair{"ilike", "ILIKE"}, std::pair{"between", "BETWEEN"},
                             std::pair{"similar", "SIMILAR TO"}};
      for (const auto& [name, feature] : words) {
        if (token.text == name) {
          return unsupported(feature, token);
        }
      }
    }
    if (*variable == N::primary && token.kind == TokenKind::left_paren) {
      if (depth >= max_expression_depth) {
        return SqlError{.sqlstate = std::string{sqlstate::statement_too_complex},
                        .message = "expression nesting is too deep",
                        .position = token.position};
      }
      if (terminal(next) == keyword(Keyword::select)) {
        return unsupported("subqueries", next);
      }
    }
  }
  if (const auto* expected = std::get_if<T>(&symbol); expected != nullptr &&
                                                      *expected == keyword(Keyword::from) &&
                                                      identifier && token.text == "as") {
    return unsupported("column aliases", token);
  }
  if (previous != nullptr && previous->kind == TokenKind::semicolon &&
      token.kind != TokenKind::end_of_input) {
    auto error = syntax_error(token);
    error.message = "only one statement is allowed per query";
    return error;
  }
  return std::nullopt;
}

}  // namespace

namespace ll1 {

auto select_grammar() -> std::span<const Production> {
  // Right-recursive tails recognize the same language as the recursive parser;
  // the SDT actions in parse_select_ll1 fold AND/OR left over these tails.
  // The order is significant: production numbers select the semantic action (P).
  static const std::vector<Production> grammar{
      {.left = N::statement,
       .right = {keyword(Keyword::select), N::targets, keyword(Keyword::from), N::table,
                 N::where_opt, N::semicolon_opt, T{.kind = TokenKind::end_of_input}}},
      {.left = N::targets, .right = {T{.kind = TokenKind::star}}},
      {.left = N::targets, .right = {N::target, N::targets_tail}},
      {.left = N::target, .right = {T{.kind = TokenKind::identifier}}},
      {.left = N::targets_tail, .right = {T{.kind = TokenKind::comma}, N::target, N::targets_tail}},
      {.left = N::targets_tail, .right = {}},
      {.left = N::table, .right = {T{.kind = TokenKind::identifier}}},
      {.left = N::where_opt, .right = {keyword(Keyword::where), N::expr}},
      {.left = N::where_opt, .right = {}},
      {.left = N::expr, .right = {N::and_expr, N::or_tail}},
      {.left = N::or_tail, .right = {keyword(Keyword::or_), N::and_expr, N::or_tail}},
      {.left = N::or_tail, .right = {}},
      {.left = N::and_expr, .right = {N::predicate, N::and_tail}},
      {.left = N::and_tail, .right = {keyword(Keyword::and_), N::predicate, N::and_tail}},
      {.left = N::and_tail, .right = {}},
      {.left = N::predicate, .right = {N::primary, N::predicate_tail}},
      {.left = N::predicate_tail, .right = {N::comparison, N::primary}},
      {.left = N::predicate_tail,
       .right = {keyword(Keyword::is), N::not_opt, keyword(Keyword::null)}},
      {.left = N::predicate_tail, .right = {}},
      {.left = N::not_opt, .right = {keyword(Keyword::not_)}},
      {.left = N::not_opt, .right = {}},
      {.left = N::primary,
       .right = {T{.kind = TokenKind::left_paren}, N::expr, T{.kind = TokenKind::right_paren}}},
      {.left = N::primary, .right = {T{.kind = TokenKind::identifier}}},
      {.left = N::primary, .right = {N::literal}},
      {.left = N::literal, .right = {T{.kind = TokenKind::integer_literal}}},
      {.left = N::literal, .right = {T{.kind = TokenKind::decimal_literal}}},
      {.left = N::literal, .right = {T{.kind = TokenKind::string_literal}}},
      {.left = N::literal, .right = {keyword(Keyword::null)}},
      {.left = N::literal, .right = {keyword(Keyword::true_)}},
      {.left = N::literal, .right = {keyword(Keyword::false_)}},
      {.left = N::comparison, .right = {T{.kind = TokenKind::equal}}},
      {.left = N::comparison, .right = {T{.kind = TokenKind::not_equal}}},
      {.left = N::comparison, .right = {T{.kind = TokenKind::less}}},
      {.left = N::comparison, .right = {T{.kind = TokenKind::less_equal}}},
      {.left = N::comparison, .right = {T{.kind = TokenKind::greater}}},
      {.left = N::comparison, .right = {T{.kind = TokenKind::greater_equal}}},
      {.left = N::semicolon_opt, .right = {T{.kind = TokenKind::semicolon}}},
      {.left = N::semicolon_opt, .right = {}},
  };
  return grammar;
}

auto build_table(std::span<const Production> grammar) -> std::expected<ParseTable, Conflict> {
  std::array<Set, nonterminal_count> first;
  std::array<Set, nonterminal_count> follow;
  std::array<bool, nonterminal_count> nullable{};
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& production : grammar) {
      const auto left = index_of(production.left);
      const auto result = first_of(production.right, first, nullable);
      changed = merge(first.at(left), result.terminals) || changed;
      if (result.nullable && !nullable.at(left)) {
        nullable.at(left) = true;
        changed = true;
      }
    }
  }
  follow.at(index_of(N::statement)).insert(T{.kind = TokenKind::end_of_input});
  changed = true;
  while (changed) {
    changed = false;
    for (const auto& production : grammar) {
      const std::span<const Symbol> right{production.right};
      for (std::size_t offset = 0; offset < right.size(); ++offset) {
        const auto* variable = std::get_if<N>(&checked_at(right, offset));
        if (variable == nullptr) {
          continue;
        }
        auto& destination = follow.at(index_of(*variable));
        const auto suffix = first_of(right.subspan(offset + 1), first, nullable);
        changed = merge(destination, suffix.terminals) || changed;
        if (suffix.nullable) {
          changed = merge(destination, follow.at(index_of(production.left))) || changed;
        }
      }
    }
  }
  ParseTable table;
  for (std::size_t number = 0; number < grammar.size(); ++number) {
    const auto& production = checked_at(grammar, number);
    auto lookaheads = first_of(production.right, first, nullable);
    if (lookaheads.nullable) {
      merge(lookaheads.terminals, follow.at(index_of(production.left)));
    }
    for (T lookahead : lookaheads.terminals) {
      const Cell cell{production.left, lookahead};
      const auto [entry, inserted] = table.emplace(cell, number);
      if (!inserted) {
        return std::unexpected{Conflict{
            .cell = cell, .existing_production = entry->second, .incoming_production = number}};
      }
    }
  }
  return table;
}

}  // namespace ll1

namespace {

/// \brief Production numbers, in the order of `ll1::select_grammar()`.
/// \details The semantic action of each production is selected by this number;
/// `parse_select_ll1` rejects a grammar whose size does not match.
enum class P : std::uint8_t {
  statement,
  targets_star,
  targets_list,
  target,
  targets_tail_more,
  targets_tail_end,
  table,
  where_opt_where,
  where_opt_empty,
  expr,
  or_tail_more,
  or_tail_end,
  and_expr,
  and_tail_more,
  and_tail_end,
  predicate,
  predicate_tail_comparison,
  predicate_tail_is_null,
  predicate_tail_empty,
  not_opt_not,
  not_opt_empty,
  primary_paren,
  primary_column,
  primary_literal,
  literal_integer,
  literal_decimal,
  literal_string,
  literal_null,
  literal_true,
  literal_false,
  comparison_eq,
  comparison_ne,
  comparison_lt,
  comparison_le,
  comparison_gt,
  comparison_ge,
  semicolon_opt_semicolon,
  semicolon_opt_empty,
  count,
};

/// \brief Reduction marker: pushed below a production's right-hand side.
struct Reduce {
  std::size_t production;
};

/// \brief Parse-stack entry: a grammar symbol or a semantic marker.
using Item = std::variant<N, T, Reduce>;

/// \brief Binary operator and the position of its token.
struct Operator {
  BinaryOp op;
  SourcePosition position;
};

/// \brief Right operand of one OR/AND in a tail, with its operator position.
struct Operand {
  SourcePosition position;
  std::unique_ptr<Expr> expr;
};

/// \brief Synthesized OR/AND tail. Operands are stored innermost-first: the
/// right-recursive tail reduces from the end, so appending is O(1) and the left
/// fold walks the vector backwards.
struct BooleanTail {
  std::vector<Operand> operands;
};

/// \brief Synthesized select-list tail, stored last-first for the same reason.
struct ReversedTargets {
  std::vector<Identifier> names;
};

/// \brief Synthesized `predicate_tail`: nothing, `op primary` or `IS [NOT] NULL`.
struct PredicateTail {
  std::optional<Operator> op;
  std::unique_ptr<Expr> right;
};

/// \brief Synthesized attribute of one right-hand-side symbol.
/// \details Terminals contribute the matched `const Token*` (tokens outlive the
/// parse); every nonterminal contributes exactly one value of its own type.
using Value = std::variant<std::monostate, const Token*, Identifier, std::vector<Identifier>,
                           ReversedTargets, std::unique_ptr<Expr>, BooleanTail, PredicateTail,
                           Operator, bool, Literal, SelectStmt>;

auto make_expr(Expr::Node node) -> std::unique_ptr<Expr> {
  return std::make_unique<Expr>(Expr{.node = std::move(node)});
}

auto make_binary(BinaryOp operation, SourcePosition position, std::unique_ptr<Expr> left,
                 std::unique_ptr<Expr> right) -> std::unique_ptr<Expr> {
  return make_expr(BinaryExpr{operation, position, std::move(left), std::move(right)});
}

/// \brief Moves the attribute of right-hand-side symbol `offset` out of `values`.
template <class Attribute>
auto take(std::span<Value> values, std::size_t offset) -> Attribute {
  if (offset >= values.size()) {
    throw std::out_of_range{"SDT value index out of range"};
  }
  return std::get<Attribute>(
      std::move(*std::next(values.begin(), static_cast<std::ptrdiff_t>(offset))));
}

auto token_of(std::span<Value> values, std::size_t offset) -> const Token& {
  return *take<const Token*>(values, offset);
}

auto identifier_of(const Token& token) -> Identifier {
  return Identifier{.name = token.text, .position = token.position};
}

/// \brief Integer literals beyond BIGINT stay exact as decimal text for the analyzer.
auto integer_literal(const Token& token) -> Literal {
  std::int64_t value{0};
  const char* const first = token.text.data();
  const char* const last = std::next(first, static_cast<std::ptrdiff_t>(token.text.size()));
  const auto [end, error] = std::from_chars(first, last, value);
  // 0 = no error. std::errc{} is avoided because clang-tidy flags it under libc++.
  if (static_cast<int>(error) == 0 && end == last) {
    return Literal{.value = value, .position = token.position};
  }
  return Literal{.value = DecimalLiteral{.text = token.text}, .position = token.position};
}

/// \brief Left fold of `first (op operand)*`, e.g. `a OR b OR c` -> `(a OR b) OR c`.
auto fold_left(BinaryOp operation, std::unique_ptr<Expr> first, BooleanTail tail)
    -> std::unique_ptr<Expr> {
  auto result = std::move(first);
  for (auto& operand : std::ranges::reverse_view(tail.operands)) {
    result = make_binary(operation, operand.position, std::move(result), std::move(operand.expr));
  }
  return result;
}

auto comparison(std::span<Value> values, BinaryOp operation) -> Value {
  return Operator{.op = operation, .position = token_of(values, 0).position};
}

/// \brief Semantic action of `production` over the attributes of its right-hand side.
// NOLINTNEXTLINE(readability-function-size)
auto reduce(P production, std::span<Value> values) -> Value {
  switch (production) {
    case P::statement:  // SELECT targets FROM table where_opt semicolon_opt EOF
      return SelectStmt{.targets = take<std::vector<Identifier>>(values, 1),
                        .table = take<Identifier>(values, 3),
                        .where = take<std::unique_ptr<Expr>>(values, 4)};
    case P::targets_star:  // '*': an empty list means every column
      return std::vector<Identifier>{};
    case P::targets_list: {  // target targets_tail
      auto names = take<ReversedTargets>(values, 1).names;
      names.push_back(take<Identifier>(values, 0));
      std::ranges::reverse(names);
      return names;
    }
    case P::target:
    case P::table:
      return identifier_of(token_of(values, 0));
    case P::targets_tail_more: {  // ',' target targets_tail
      auto tail = take<ReversedTargets>(values, 2);
      tail.names.push_back(take<Identifier>(values, 1));
      return tail;
    }
    case P::targets_tail_end:
      return ReversedTargets{};
    case P::where_opt_where:  // WHERE expr
      return take<std::unique_ptr<Expr>>(values, 1);
    case P::where_opt_empty:
      return std::unique_ptr<Expr>{};
    case P::expr:  // and_expr or_tail
      return fold_left(BinaryOp::or_, take<std::unique_ptr<Expr>>(values, 0),
                       take<BooleanTail>(values, 1));
    case P::and_expr:  // predicate and_tail
      return fold_left(BinaryOp::and_, take<std::unique_ptr<Expr>>(values, 0),
                       take<BooleanTail>(values, 1));
    case P::or_tail_more:     // OR and_expr or_tail
    case P::and_tail_more: {  // AND predicate and_tail
      auto tail = take<BooleanTail>(values, 2);
      tail.operands.push_back(Operand{.position = token_of(values, 0).position,
                                      .expr = take<std::unique_ptr<Expr>>(values, 1)});
      return tail;
    }
    case P::or_tail_end:
    case P::and_tail_end:
      return BooleanTail{};
    case P::predicate: {  // primary predicate_tail
      auto left = take<std::unique_ptr<Expr>>(values, 0);
      auto tail = take<PredicateTail>(values, 1);
      if (!tail.op.has_value()) {
        return left;
      }
      return make_binary(tail.op->op, tail.op->position, std::move(left), std::move(tail.right));
    }
    case P::predicate_tail_comparison: {  // comparison primary
      return PredicateTail{.op = take<Operator>(values, 0),
                           .right = take<std::unique_ptr<Expr>>(values, 1)};
    }
    case P::predicate_tail_is_null:  // IS not_opt NULL; the node is positioned at IS
      return PredicateTail{
          .op = Operator{.op = take<bool>(values, 1) ? BinaryOp::is_not_null : BinaryOp::is_null,
                         .position = token_of(values, 0).position},
          .right = nullptr};
    case P::predicate_tail_empty:
      return PredicateTail{};
    case P::not_opt_not:
      return true;
    case P::not_opt_empty:
      return false;
    case P::primary_paren:  // '(' expr ')': parentheses only group, no node
      return take<std::unique_ptr<Expr>>(values, 1);
    case P::primary_column:
      return make_expr(ColumnRef{.name = identifier_of(token_of(values, 0))});
    case P::primary_literal:
      return make_expr(take<Literal>(values, 0));
    case P::literal_integer:
      return integer_literal(token_of(values, 0));
    case P::literal_decimal: {
      const Token& token = token_of(values, 0);
      return Literal{.value = DecimalLiteral{.text = token.text}, .position = token.position};
    }
    case P::literal_string: {
      const Token& token = token_of(values, 0);
      return Literal{.value = token.text, .position = token.position};
    }
    case P::literal_null:
      return Literal{.value = NullLiteral{}, .position = token_of(values, 0).position};
    case P::literal_true:
    case P::literal_false:
      return Literal{.value = production == P::literal_true,
                     .position = token_of(values, 0).position};
    case P::comparison_eq:
      return comparison(values, BinaryOp::eq);
    case P::comparison_ne:  // both '<>' and '!=' lex as not_equal
      return comparison(values, BinaryOp::ne);
    case P::comparison_lt:
      return comparison(values, BinaryOp::lt);
    case P::comparison_le:
      return comparison(values, BinaryOp::le);
    case P::comparison_gt:
      return comparison(values, BinaryOp::gt);
    case P::comparison_ge:
      return comparison(values, BinaryOp::ge);
    case P::semicolon_opt_semicolon:
    case P::semicolon_opt_empty:
    case P::count:
      break;
  }
  return std::monostate{};
}

/// \brief Grammar symbols of the parse stack, without SDT markers, for traces.
auto visible_stack(const std::vector<Item>& stack) -> std::vector<ll1::Symbol> {
  std::vector<ll1::Symbol> symbols;
  symbols.reserve(stack.size());
  for (const auto& item : stack) {
    if (const auto* variable = std::get_if<N>(&item)) {
      symbols.emplace_back(*variable);
    } else if (const auto* token = std::get_if<T>(&item)) {
      symbols.emplace_back(*token);
    }
  }
  return symbols;
}

}  // namespace

auto parse_select_ll1(std::span<const Token> tokens, const ll1::TraceCallback& trace)
    -> SqlResult<SelectStmt> {
  std::size_t cursor = 0;
  std::size_t depth = 0;
  std::vector<Item> stack{N::statement};
  std::vector<Value> values;
  const auto emit = [&](ll1::TraceAction action, std::optional<std::size_t> production = {},
                        std::optional<ll1::Cell> cell = {}) -> void {
    if (trace) {
      auto symbols = visible_stack(stack);
      const auto size = symbols.size();
      trace(ll1::TraceStep{.action = action,
                           .token_index = cursor,
                           .stack_size = size,
                           .production = production,
                           .stack = std::move(symbols),
                           .cell = std::move(cell)});
    }
  };
  const auto fail = [&](SqlError error) -> SqlResult<SelectStmt> {
    emit(ll1::TraceAction::error);
    return std::unexpected{std::move(error)};
  };
  if (tokens.empty() || tokens.back().kind != TokenKind::end_of_input) {
    return fail(SqlError{.sqlstate = std::string{sqlstate::syntax_error},
                         .message = "token sequence must end with end_of_input",
                         .position = SourcePosition{}});
  }
  static const auto table = ll1::build_table(ll1::select_grammar());
  const auto grammar = ll1::select_grammar();
  if (!table.has_value() || grammar.size() != static_cast<std::size_t>(P::count)) {
    return fail(SqlError{.sqlstate = std::string{sqlstate::internal_error},
                         .message = table.has_value() ? "SELECT grammar and SDT actions differ"
                                                      : "SELECT grammar has an LL(1) conflict",
                         .position = SourcePosition{}});
  }
  while (!stack.empty()) {
    if (const auto* marker = std::get_if<Reduce>(&stack.back())) {
      // Every right-hand-side symbol left exactly one value on top of the stack.
      const auto arity = checked_at(grammar, marker->production).right.size();
      const auto first = std::prev(values.end(), static_cast<std::ptrdiff_t>(arity));
      auto result = reduce(static_cast<P>(marker->production), std::span<Value>{first, arity});
      values.erase(first, values.end());
      values.push_back(std::move(result));
      stack.pop_back();
      continue;
    }
    if (cursor >= tokens.size()) {
      return fail(syntax_error(tokens.back()));
    }
    const Token& token = checked_at(tokens, cursor);
    const Token& next = checked_at(tokens, std::min(cursor + 1, tokens.size() - 1));
    const ll1::Symbol symbol = std::holds_alternative<N>(stack.back())
                                   ? ll1::Symbol{std::get<N>(stack.back())}
                                   : ll1::Symbol{std::get<T>(stack.back())};
    if (auto error = diagnostic(symbol, token, next,
                                cursor == 0 ? nullptr : &checked_at(tokens, cursor - 1), depth)) {
      return fail(std::move(*error));
    }
    if (const auto* expected = std::get_if<T>(&symbol)) {
      if (*expected != terminal(token)) {
        return fail(syntax_error(token, terminal_name(*expected)));
      }
      emit(ll1::TraceAction::match);
      stack.pop_back();
      values.emplace_back(&token);
      if (token.kind == TokenKind::left_paren) {
        ++depth;
      } else if (token.kind == TokenKind::right_paren) {
        --depth;
      }
      ++cursor;
      continue;
    }
    const N variable = std::get<N>(symbol);
    const auto entry = table->find({variable, terminal(token)});
    if (entry == table->end()) {
      // The old parser only recognizes unsupported trailing clauses at top level.
      if (depth == 0 && token.kind == TokenKind::identifier &&
          (variable == N::where_opt || variable == N::predicate_tail || variable == N::and_tail ||
           variable == N::or_tail || variable == N::semicolon_opt)) {
        if (const auto feature = clause_feature(token.text)) {
          return fail(unsupported(*feature, token));
        }
      }
      return fail(syntax_error(token, expected_terminals(*table, variable)));
    }
    emit(ll1::TraceAction::expand, entry->second, ll1::Cell{variable, terminal(token)});
    stack.back() = Reduce{.production = entry->second};
    const auto& right = checked_at(grammar, entry->second).right;
    for (const auto& item : std::ranges::reverse_view(right)) {
      std::visit([&](const auto& grammar_symbol) -> void { stack.emplace_back(grammar_symbol); },
                 item);
    }
  }
  // Reject embedded EOF sentinels, not just text following a semicolon.
  if (cursor != tokens.size()) {
    return fail(syntax_error(checked_at(tokens, cursor)));
  }
  if (values.size() != 1 || !std::holds_alternative<SelectStmt>(values.back())) {
    return fail(SqlError{.sqlstate = std::string{sqlstate::internal_error},
                         .message = "SELECT translation left an unbalanced value stack",
                         .position = SourcePosition{}});
  }
  emit(ll1::TraceAction::accept);
  return std::get<SelectStmt>(std::move(values.back()));
}

auto validate_select(std::span<const Token> tokens, const ll1::TraceCallback& trace)
    -> SqlResult<void> {
  if (auto select = parse_select_ll1(tokens, trace); !select.has_value()) {
    return std::unexpected{std::move(select.error())};
  }
  return {};
}

}  // namespace kravidb::sql
