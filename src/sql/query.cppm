/// \file
/// \brief Analyzed query: names resolved to typed columns and expressions.
///
/// \details The analyzer turns a `SelectStmt` into a `Query`. Every column is
/// identified by its `attnum` and every expression node carries its SQL type,
/// so later stages never look names up again.

export module sql.query;

import std;
export import sql.ast;
export import sql.source;
export import storage.catalog;
export import storage.tuple;

export namespace kravidb::sql {

/// \brief SQL type families known to the analyzer.
enum class TypeKind : std::uint8_t {
  bigint,
  varchar,
  decimal,
  /// \brief Result of a comparison, `AND`, `OR` or `IS [NOT] NULL`.
  boolean,
  /// \brief Type of the bare `NULL` literal; compatible with every type.
  null,
};

/// \brief Resolved type of a column or expression.
struct SqlType {
  TypeKind kind{TypeKind::null};
  /// \brief Declared precision and scale; only for a constrained `DECIMAL`.
  std::optional<DecimalType> decimal;

  [[nodiscard]] auto operator==(const SqlType& other) const -> bool {
    if (kind != other.kind || decimal.has_value() != other.decimal.has_value()) {
      return false;
    }
    return !decimal.has_value() || (decimal->precision == other.decimal->precision &&
                                    decimal->scale == other.decimal->scale);
  }
};

/// \brief Reference to a column of the query relation.
struct ColumnVar {
  std::int16_t attnum{0};
  std::string name;
};

/// \brief Value of a constant after analysis.
using ConstValue = std::variant<std::monostate, std::int64_t, storage::Decimal, std::string>;

/// \brief Constant expression; `std::monostate` stands for `NULL`.
struct Const {
  ConstValue value;
};

struct TypedExpr;

/// \brief Comparison, `AND`/`OR` or null test over typed operands.
///
/// `right` is null if and only if `op` is `is_null` or `is_not_null`.
struct OpExpr {
  BinaryOp op;
  std::unique_ptr<TypedExpr> left;
  std::unique_ptr<TypedExpr> right;
};

/// \brief Expression node with its resolved type and source position.
struct TypedExpr {
  using Node = std::variant<ColumnVar, Const, OpExpr>;
  Node node;
  SqlType type;
  SourcePosition position;
};

/// \brief One output column of the query.
struct TargetEntry {
  std::string name;
  std::int16_t attnum{0};
  SqlType type;
};

/// \brief Analyzed SELECT, ready for the planner.
struct Query {
  storage::Oid relation{storage::invalid_oid};
  std::string relation_name;
  /// \brief Output columns in select-list order; `*` expands to every column by `attnum`.
  std::vector<TargetEntry> targets;
  /// \brief Boolean filter, or null when there is no WHERE clause.
  std::unique_ptr<TypedExpr> where;
};

/// \brief SQL spelling of a type, used in error messages.
[[nodiscard]] auto type_name(const SqlType& type) -> std::string {
  switch (type.kind) {
    case TypeKind::bigint:
      return "bigint";
    case TypeKind::varchar:
      return "varchar";
    case TypeKind::decimal:
      if (type.decimal.has_value()) {
        return std::format("decimal({},{})", type.decimal->precision, type.decimal->scale);
      }
      return "decimal";
    case TypeKind::boolean:
      return "boolean";
    case TypeKind::null:
      return "unknown";
  }
  return "unknown";
}

}  // namespace kravidb::sql
