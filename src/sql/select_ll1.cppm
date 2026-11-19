/// \file
/// \brief Table-driven, non-recursive LL(1) parsing of the supported SELECT grammar.
/// \details The engine runs an S-attributed syntax-directed translation (SDT):
/// the `SelectStmt` is synthesized while the table drives recognition, in a
/// single pass and without a second parser.
export module sql.select_ll1;

import std;
export import sql.ast;
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
/// \details Internal reduction markers of the SDT are never part of a trace:
/// `stack` holds grammar symbols only and `stack_size == stack.size()`.
struct TraceStep {
  TraceAction action{};
  std::size_t token_index{0};
  std::size_t stack_size{0};
  std::optional<std::size_t> production;
  /// \brief Grammar symbols on the stack before the action; the top is the last element.
  std::vector<Symbol> stack;
  /// \brief Table cell used by an expansion, when applicable.
  std::optional<Cell> cell;
};

using TraceCallback = std::function<void(const TraceStep&)>;

}  // namespace ll1

/// \brief Parses exactly one SELECT, including optional ';' and final EOF, and
/// builds its AST in the same LL(1) pass.
/// \details Expanding `A -> X1..Xn` pushes a reduction marker below the
/// right-hand side; matched terminals push their `const Token*` onto a value
/// stack, and popping the marker runs the production's semantic action over
/// the n values of its right-hand side. `OR`/`AND` tails are folded left.
/// Names and types are not resolved (that is the analyzer's job). Preserves the
/// front-end's 256-parenthesis limit and SQLSTATE/message/position
/// diagnostics. Callback exceptions propagate; no output is emitted when no
/// callback is supplied.
[[nodiscard]] auto parse_select_ll1(std::span<const Token> tokens,
                                    const ll1::TraceCallback& trace = {}) -> SqlResult<SelectStmt>;

/// \brief Recognizes exactly one SELECT; compatibility wrapper over `parse_select_ll1`.
/// \details Produces the same trace and errors and discards the AST. Kept for
/// the trace viewer and for callers that only need acceptance.
[[nodiscard]] auto validate_select(std::span<const Token> tokens,
                                   const ll1::TraceCallback& trace = {}) -> SqlResult<void>;

}  // namespace kravidb::sql
