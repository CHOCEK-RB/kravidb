module sql.select_ll1;

import std;

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

auto syntax_error(const Token& token) -> SqlError {
  return SqlError{
      .sqlstate = std::string{sqlstate::syntax_error},
      .message = token.kind == TokenKind::end_of_input
                     ? "syntax error at end of input"
                     : std::format("syntax error at or near \"{}\"", token.text),
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
          token.kind == TokenKind::string_literal || terminal(token) == keyword(Keyword::null)) {
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
  // Right-recursive tails recognize the same language as the existing parser;
  // AST construction folds AND/OR left, independently of this recognition order.
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

auto validate_select(std::span<const Token> tokens, const ll1::TraceCallback& trace)
    -> SqlResult<void> {
  std::size_t cursor = 0;
  std::size_t depth = 0;
  std::vector<ll1::Symbol> stack{N::statement};
  const auto emit = [&](ll1::TraceAction action,
                        std::optional<std::size_t> production = {}) -> void {
    if (trace) {
      trace(ll1::TraceStep{.action = action,
                           .token_index = cursor,
                           .stack_size = stack.size(),
                           .production = production});
    }
  };
  const auto fail = [&](SqlError error) -> SqlResult<void> {
    emit(ll1::TraceAction::error);
    return std::unexpected{std::move(error)};
  };
  if (tokens.empty() || tokens.back().kind != TokenKind::end_of_input) {
    return fail(SqlError{.sqlstate = std::string{sqlstate::syntax_error},
                         .message = "token sequence must end with end_of_input",
                         .position = SourcePosition{}});
  }
  static const auto table = ll1::build_table(ll1::select_grammar());
  if (!table.has_value()) {
    return fail(SqlError{.sqlstate = std::string{sqlstate::internal_error},
                         .message = "SELECT grammar has an LL(1) conflict",
                         .position = SourcePosition{}});
  }
  const auto grammar = ll1::select_grammar();
  while (!stack.empty()) {
    if (cursor >= tokens.size()) {
      return fail(syntax_error(tokens.back()));
    }
    const Token& token = checked_at(tokens, cursor);
    const Token& next = checked_at(tokens, std::min(cursor + 1, tokens.size() - 1));
    const auto symbol = stack.back();
    if (auto error = diagnostic(symbol, token, next,
                                cursor == 0 ? nullptr : &checked_at(tokens, cursor - 1), depth)) {
      return fail(std::move(*error));
    }
    if (const auto* expected = std::get_if<T>(&symbol)) {
      if (*expected != terminal(token)) {
        return fail(syntax_error(token));
      }
      emit(ll1::TraceAction::match);
      stack.pop_back();
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
      return fail(syntax_error(token));
    }
    emit(ll1::TraceAction::expand, entry->second);
    stack.pop_back();
    const auto& right = checked_at(grammar, entry->second).right;
    stack.insert(stack.end(), right.rbegin(), right.rend());
  }
  // Reject embedded EOF sentinels, not just text following a semicolon.
  if (cursor != tokens.size()) {
    return fail(syntax_error(checked_at(tokens, cursor)));
  }
  emit(ll1::TraceAction::accept);
  return {};
}

}  // namespace kravidb::sql
