/// \file
/// \brief Table-driven, non-recursive recognition of the supported SELECT grammar.
export module sql.select_ll1;

import std;
export import sql.lexer;

export namespace kravidb::sql {

namespace ll1 {

/// \brief Grammar variables. Tails remove left recursion and encode precedence.
enum class Nonterminal : std::uint8_t {
  statement,
  targets,
  target,
  targets_tail,
  table,
  where_opt,
  expr,
  or_tail,
  and_expr,
  and_tail,
  predicate,
  predicate_tail,
  primary,
  comparison,
  not_opt,
  literal,
  semicolon_opt,
  count,
};

/// \brief Keywords need their own columns: TokenKind::keyword alone is ambiguous.
struct Terminal {
  TokenKind kind;
  Keyword keyword{Keyword::none};
  [[nodiscard]] auto operator<=>(const Terminal&) const = default;
};

using Symbol = std::variant<Nonterminal, Terminal>;

/// \brief An empty right-hand side represents epsilon.
struct Production {
  Nonterminal left;
  std::vector<Symbol> right;
};

using Cell = std::pair<Nonterminal, Terminal>;
using ParseTable = std::map<Cell, std::size_t>;

struct Conflict {
  Cell cell;
  std::size_t existing_production;
  std::size_t incoming_production;
};

/// \brief Stable production order, shared by the engine, diagnostics and tests.
[[nodiscard]] auto select_grammar() -> std::span<const Production>;

/// \brief Computes nullable, FIRST and FOLLOW by iteration; never overwrites conflicts.
[[nodiscard]] auto build_table(std::span<const Production> grammar)
    -> std::expected<ParseTable, Conflict>;

enum class TraceAction : std::uint8_t { expand, match, accept, error };

/// \brief Snapshot before an action. Production is present only for expansion.
struct TraceStep {
  TraceAction action{};
  std::size_t token_index{0};
  std::size_t stack_size{0};
  std::optional<std::size_t> production;
};

using TraceCallback = std::function<void(const TraceStep&)>;

}  // namespace ll1

/// \brief Recognizes exactly one SELECT, including optional ';' and final EOF.
/// \details Does not construct an AST or resolve names/types. Preserves the
/// front-end's 256-parenthesis limit and SQLSTATE/position diagnostics. Callback
/// exceptions propagate; no output is emitted when no callback is supplied.
[[nodiscard]] auto validate_select(std::span<const Token> tokens,
                                   const ll1::TraceCallback& trace = {}) -> SqlResult<void>;

}  // namespace kravidb::sql
