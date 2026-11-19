/// \file
/// \brief Text tree for an analyzed SELECT query.
///
/// \details The tree exposes the decorated AST without depending on a terminal
/// library: `format_query` renders it for the console and the UI renders the
/// same nodes with colors.

export module sql.explain;

import std;
export import sql.query;

export namespace kravidb::sql {

/// \brief Visual role of an explained node.
enum class ExplainKind : std::uint8_t {
  statement,
  targets,
  target,
  where,
  operator_,
  column,
  constant,
};

/// \brief One line of the decorated AST and its children.
struct ExplainNode {
  std::string label;
  ExplainKind kind{ExplainKind::statement};
  std::vector<ExplainNode> children;
};

/// \brief Builds the decorated AST tree for `query`.
[[nodiscard]] auto explain_query(const Query& query) -> ExplainNode;

/// \brief Renders `query` as an indented text tree.
///
/// The result has no trailing newline.
[[nodiscard]] auto format_query(const Query& query) -> std::string;

}  // namespace kravidb::sql
