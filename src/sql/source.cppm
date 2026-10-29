/// \file
/// \brief Source positions used by SQL diagnostics.

export module sql.source;

import std;

export namespace kravidb::sql {

/// \brief 1-based byte offset of a token or AST node in the SQL text.
///
/// A value of 0 means that no position is available.
struct SourcePosition {
  std::size_t offset{0};

  /// \brief Two positions are equal when they point at the same byte.
  [[nodiscard]] auto operator==(const SourcePosition&) const -> bool = default;
};

}  // namespace kravidb::sql
