/// \file
/// \brief SQL error value and result alias.

export module sql.error;

import std;
export import sql.source;

export namespace kravidb::sql {

/// \brief SQLSTATE codes produced by the SQL front end.
namespace sqlstate {

/// \brief Malformed statement text.
inline constexpr std::string_view syntax_error = "42601";
/// \brief Recognized SQL feature outside the supported subset.
inline constexpr std::string_view feature_not_supported = "0A000";
/// \brief Statement too deeply nested to analyze.
inline constexpr std::string_view statement_too_complex = "54001";
/// \brief Relation named in FROM does not exist.
inline constexpr std::string_view undefined_table = "42P01";
/// \brief Column does not exist in the relation.
inline constexpr std::string_view undefined_column = "42703";
/// \brief A named database object does not exist.
inline constexpr std::string_view undefined_object = "42704";
/// \brief A relation with the requested name already exists.
inline constexpr std::string_view duplicate_table = "42P07";
/// \brief A column name occurs more than once.
inline constexpr std::string_view duplicate_column = "42701";
/// \brief A required value is NULL.
inline constexpr std::string_view not_null_violation = "23502";
/// \brief A unique or primary-key value is duplicated.
inline constexpr std::string_view unique_violation = "23505";
/// \brief An object cannot be removed while dependants exist.
inline constexpr std::string_view dependent_objects_still_exist = "2BP01";
/// \brief Relation exists but is not a table.
inline constexpr std::string_view wrong_object_type = "42809";
/// \brief Operand types are incompatible.
inline constexpr std::string_view datatype_mismatch = "42804";
/// \brief Numeric value outside the range of its type.
inline constexpr std::string_view numeric_value_out_of_range = "22003";
/// \brief Invalid type parameter, such as a DECIMAL precision or scale.
inline constexpr std::string_view invalid_parameter_value = "22023";
/// \brief Inconsistent catalog or engine state.
inline constexpr std::string_view internal_error = "XX000";

}  // namespace sqlstate

/// \brief Error produced while lexing, parsing or analyzing a statement.
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
