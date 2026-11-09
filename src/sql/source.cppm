/// \file
/// \brief Source positions used by SQL diagnostics.

export module sql.source;

import std;

export namespace kravidb::sql {

/// \brief 1-based location of a token or AST node in the SQL text.
///
/// A value of 0 in any field means that the field is not available. Line and
/// column are derived from the byte offset; offsets count bytes, line and
/// column count characters with tabs as one column.
struct SourcePosition {
  std::size_t offset{0};
  std::size_t line{0};
  std::size_t column{0};

  /// \brief Two positions are equal when they point at the same byte.
  [[nodiscard]] auto operator==(const SourcePosition& other) const -> bool {
    return offset == other.offset;
  }
};

}  // namespace kravidb::sql
