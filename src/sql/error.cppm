/// \file
/// \brief SQL error value and result alias.

export module sql.error;

import std;
export import sql.source;

export namespace kravidb::sql {

/// \brief Error produced while parsing or analyzing a statement.
struct SqlError {
  std::string sqlstate;
  std::string message;
  SourcePosition position;

  /// \brief Two errors are equal when every field is equal.
  [[nodiscard]] auto operator==(const SqlError&) const -> bool = default;
};

/// \brief Result that carries either a value or a SQL error.
template <class T>
using SqlResult = std::expected<T, SqlError>;

}  // namespace kravidb::sql
