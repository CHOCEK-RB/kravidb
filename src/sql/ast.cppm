/// \file
/// \brief Typed abstract syntax tree for the supported SQL subset.

export module sql.ast;

import std;
export import sql.source;

export namespace kravidb::sql {

/// \brief SQL identifier and the position where it starts.
struct Identifier {
  std::string name;
  SourcePosition position;
};

/// \brief Exact decimal literal text, kept unparsed until analysis.
struct DecimalLiteral {
  std::string text;
};

/// \brief The SQL NULL literal.
struct NullLiteral {};

/// \brief Value of a literal before type analysis.
using LiteralValue = std::variant<std::int64_t, DecimalLiteral, std::string, NullLiteral>;

/// \brief One literal and its source position.
struct Literal {
  LiteralValue value;
  SourcePosition position;
};

/// \brief Reference to a column by name.
struct ColumnRef {
  Identifier name;
};

/// \brief Binary and null-check operators.
enum class BinaryOp : std::uint8_t {
  eq,
  ne,
  lt,
  le,
  gt,
  ge,
  and_,
  or_,
  is_null,
  is_not_null,
};

struct Expr;

/// \brief Binary expression with owned child nodes.
///
/// `right` is null if and only if `op` is `is_null` or `is_not_null`.
struct BinaryExpr {
  BinaryOp op;
  SourcePosition position;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;

  BinaryExpr(BinaryOp operation, SourcePosition position, std::unique_ptr<Expr> left,
             std::unique_ptr<Expr> right);
};

/// \brief One SQL expression node.
struct Expr {
  using Node = std::variant<ColumnRef, Literal, BinaryExpr>;
  Node node;
};

/// \brief Precision and scale of a SQL DECIMAL type.
struct DecimalType {
  std::uint8_t precision{0};
  std::uint8_t scale{0};
};

/// \brief Supported SQL type names.
enum class TypeNameKind : std::uint8_t { bigint, varchar, decimal };

/// \brief SQL type name before conversion to a storage field type.
struct TypeName {
  TypeNameKind kind{};
  std::optional<DecimalType> decimal;  // only for TypeNameKind::decimal
  SourcePosition position;
};

/// \brief Column definition as written in a CREATE TABLE or ALTER TABLE.
struct ColumnDef {
  Identifier name;
  TypeName type;
  bool is_primary_key{false};
};

/// \brief One SELECT statement.
struct SelectStmt {
  std::vector<Identifier> targets;  // empty = '*'
  Identifier table;
  std::unique_ptr<Expr> where;
};

/// \brief One INSERT statement.
struct InsertStmt {
  Identifier table;
  std::vector<Identifier> columns;  // empty = all columns in order
  std::vector<Literal> values;
};

/// \brief One SET assignment of an UPDATE statement.
struct Assignment {
  Identifier column;
  Literal value;
};

/// \brief One UPDATE statement.
struct UpdateStmt {
  Identifier table;
  std::vector<Assignment> assignments;
  std::unique_ptr<Expr> where;
};

/// \brief One DELETE statement.
struct DeleteStmt {
  Identifier table;
  std::unique_ptr<Expr> where;
};

/// \brief One CREATE TABLE statement.
struct CreateTableStmt {
  Identifier table;
  std::vector<ColumnDef> columns;
};

/// \brief One CREATE INDEX statement.
struct CreateIndexStmt {
  Identifier index;
  Identifier table;
  Identifier column;
  bool unique{false};
};

/// \brief One DROP TABLE statement.
struct DropTableStmt {
  Identifier table;
  bool if_exists{false};
};

/// \brief One DROP INDEX statement.
struct DropIndexStmt {
  Identifier index;
  bool if_exists{false};
};

/// \brief One ALTER TABLE ... ADD COLUMN statement.
struct AlterTableAddColumnStmt {
  Identifier table;
  ColumnDef column;
};

/// \brief One ALTER TABLE ... DROP COLUMN statement.
struct AlterTableDropColumnStmt {
  Identifier table;
  Identifier column;
};

/// \brief Any supported SQL statement.
using Stmt =
    std::variant<SelectStmt, InsertStmt, UpdateStmt, DeleteStmt, CreateTableStmt, CreateIndexStmt,
                 DropTableStmt, DropIndexStmt, AlterTableAddColumnStmt, AlterTableDropColumnStmt>;

BinaryExpr::BinaryExpr(BinaryOp operation, SourcePosition position, std::unique_ptr<Expr> left,
                       std::unique_ptr<Expr> right)
    : op{operation}, position{position}, left{std::move(left)}, right{std::move(right)} {
  const bool is_null_check = operation == BinaryOp::is_null || operation == BinaryOp::is_not_null;
  if (this->left == nullptr) {
    throw std::invalid_argument{"BinaryExpr: left must not be null"};
  }
  if ((this->right == nullptr) != is_null_check) {
    throw std::invalid_argument{"BinaryExpr: right must be null only for IS [NOT] NULL"};
  }
}

}  // namespace kravidb::sql
