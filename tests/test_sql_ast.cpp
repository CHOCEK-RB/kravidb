#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;

using namespace sql;

constexpr std::size_t stmt_alternative_count = 10;
constexpr std::size_t expr_alternative_count = 3;
constexpr std::size_t literal_alternative_count = 4;
constexpr std::size_t first_byte = 1;
constexpr std::size_t second_byte = 2;
constexpr std::size_t third_byte = 3;
constexpr std::size_t fourth_byte = 4;
constexpr std::size_t fifth_byte = 5;
constexpr std::size_t sixth_byte = 6;
constexpr std::size_t seventh_byte = 7;
constexpr std::size_t eighth_byte = 8;
constexpr std::size_t literal_count = 4;
constexpr std::size_t assignment_count = 2;
constexpr std::size_t column_count = 3;
constexpr std::size_t nested_node_count = 6;
constexpr std::int64_t answer = 42;
constexpr std::int64_t comparison_value = 1;
constexpr std::int64_t age = 30;
constexpr std::uint8_t decimal_precision = 5;
constexpr std::uint8_t decimal_scale = 2;

static_assert(std::variant_size_v<Stmt> == stmt_alternative_count);
static_assert(std::variant_size_v<Expr::Node> == expr_alternative_count);
static_assert(std::variant_size_v<LiteralValue> == literal_alternative_count);
static_assert(!std::is_copy_constructible_v<Stmt>);
static_assert(std::is_move_constructible_v<Stmt>);
static_assert(!std::is_copy_constructible_v<Expr>);
static_assert(std::is_move_constructible_v<Expr>);

[[nodiscard]] auto make_identifier(std::string name, std::size_t offset) -> Identifier {
  return Identifier{.name = std::move(name), .position = SourcePosition{offset}};
}

[[nodiscard]] auto make_int_literal(std::int64_t value, std::size_t offset) -> Literal {
  return Literal{.value = LiteralValue{value}, .position = SourcePosition{offset}};
}

[[nodiscard]] auto make_decimal_literal(std::string text, std::size_t offset) -> Literal {
  return Literal{
      .value = LiteralValue{DecimalLiteral{std::move(text)}},
      .position = SourcePosition{offset},
  };
}

[[nodiscard]] auto make_string_literal(std::string text, std::size_t offset) -> Literal {
  return Literal{.value = LiteralValue{std::move(text)}, .position = SourcePosition{offset}};
}

[[nodiscard]] auto make_null_literal(std::size_t offset) -> Literal {
  return Literal{.value = LiteralValue{NullLiteral{}}, .position = SourcePosition{offset}};
}

[[nodiscard]] auto make_column_expr(std::string name, std::size_t offset) -> Expr {
  return Expr{ColumnRef{.name = make_identifier(std::move(name), offset)}};
}

[[nodiscard]] auto make_binary(BinaryOp operation, std::size_t offset, std::unique_ptr<Expr> left,
                               std::unique_ptr<Expr> right) -> Expr {
  return Expr{BinaryExpr{operation, SourcePosition{offset}, std::move(left), std::move(right)}};
}

[[nodiscard]] auto count_nodes(const Expr& expr) -> std::size_t {
  return std::visit(
      [](const auto& node) -> std::size_t {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, BinaryExpr>) {
          return 1 + count_nodes(*node.left) +
                 (node.right == nullptr ? 0 : count_nodes(*node.right));
        }
        return 1;
      },
      expr.node);
}

TEST(SqlAst, SelectWithoutTargetsMeansStar) {
  SelectStmt select;
  select.table = make_identifier("users", first_byte);

  Stmt stmt{std::move(select)};
  auto* node = std::get_if<SelectStmt>(&stmt);
  ASSERT_TRUE(node != nullptr);
  EXPECT_TRUE(node->targets.empty());
  EXPECT_EQ(node->table.name, "users");
  EXPECT_EQ(node->table.position.offset, first_byte);
  EXPECT_TRUE(node->where == nullptr);
}

TEST(SqlAst, SelectKeepsTargetsAndNestedWhere) {
  SelectStmt select;
  select.targets.push_back(make_identifier("id", first_byte));
  select.table = make_identifier("users", second_byte);
  auto left = std::make_unique<Expr>(make_column_expr("id", third_byte));
  auto right = std::make_unique<Expr>(make_int_literal(answer, fourth_byte));
  select.where = std::make_unique<Expr>(
      make_binary(BinaryOp::eq, fifth_byte, std::move(left), std::move(right)));

  Stmt stmt{std::move(select)};
  auto moved = std::move(stmt);
  auto* node = std::get_if<SelectStmt>(&moved);
  ASSERT_TRUE(node != nullptr);
  ASSERT_FALSE(node->targets.empty());
  EXPECT_EQ(node->targets.at(0).name, "id");
  EXPECT_EQ(node->targets.at(0).position.offset, first_byte);
  ASSERT_TRUE(node->where != nullptr);

  auto* binary = std::get_if<BinaryExpr>(&node->where->node);
  ASSERT_TRUE(binary != nullptr);
  EXPECT_EQ(binary->op, BinaryOp::eq);
  EXPECT_EQ(binary->position.offset, fifth_byte);
  ASSERT_TRUE(binary->left != nullptr);
  ASSERT_TRUE(binary->right != nullptr);

  auto* left_column = std::get_if<ColumnRef>(&binary->left->node);
  ASSERT_TRUE(left_column != nullptr);
  EXPECT_EQ(left_column->name.name, "id");
  EXPECT_EQ(left_column->name.position.offset, third_byte);

  auto* right_literal = std::get_if<Literal>(&binary->right->node);
  ASSERT_TRUE(right_literal != nullptr);
  auto* right_value = std::get_if<std::int64_t>(&right_literal->value);
  ASSERT_TRUE(right_value != nullptr);
  EXPECT_EQ(*right_value, answer);
  EXPECT_EQ(right_literal->position.offset, fourth_byte);
}

TEST(SqlAst, InsertStoresEveryLiteralKind) {
  InsertStmt insert;
  insert.table = make_identifier("events", first_byte);
  insert.values.push_back(make_int_literal(answer, second_byte));
  insert.values.push_back(make_decimal_literal("-12.30", third_byte));
  insert.values.push_back(make_string_literal("hello", fourth_byte));
  insert.values.push_back(make_null_literal(fifth_byte));

  Stmt stmt{std::move(insert)};
  auto* node = std::get_if<InsertStmt>(&stmt);
  ASSERT_TRUE(node != nullptr);
  EXPECT_TRUE(node->columns.empty());
  ASSERT_EQ(node->values.size(), literal_count);

  EXPECT_TRUE(std::holds_alternative<std::int64_t>(node->values.at(0).value));
  auto* decimal = std::get_if<DecimalLiteral>(&node->values.at(1).value);
  ASSERT_TRUE(decimal != nullptr);
  EXPECT_EQ(decimal->text, "-12.30");
  auto* text = std::get_if<std::string>(&node->values.at(2).value);
  ASSERT_TRUE(text != nullptr);
  EXPECT_EQ(*text, "hello");
  EXPECT_TRUE(std::holds_alternative<NullLiteral>(node->values.at(3).value));
  EXPECT_EQ(node->values.at(3).position.offset, fifth_byte);
}

TEST(SqlAst, InsertCanNameColumnsExplicitly) {
  InsertStmt insert;
  insert.table = make_identifier("events", first_byte);
  insert.columns.push_back(make_identifier("id", second_byte));
  insert.values.push_back(make_int_literal(answer, third_byte));

  Stmt stmt{std::move(insert)};
  auto* node = std::get_if<InsertStmt>(&stmt);
  ASSERT_TRUE(node != nullptr);
  ASSERT_FALSE(node->columns.empty());
  EXPECT_EQ(node->columns.at(0).name, "id");
  EXPECT_EQ(node->columns.at(0).position.offset, second_byte);
}

TEST(SqlAst, UpdateStoresAssignmentsAndWhere) {
  UpdateStmt update;
  update.table = make_identifier("users", first_byte);
  update.assignments.push_back(Assignment{
      .column = make_identifier("name", second_byte),
      .value = make_string_literal("alice", third_byte),
  });
  update.assignments.push_back(Assignment{
      .column = make_identifier("age", fourth_byte),
      .value = make_int_literal(age, fifth_byte),
  });
  update.where = std::make_unique<Expr>(make_column_expr("id", sixth_byte));

  Stmt stmt{std::move(update)};
  auto* node = std::get_if<UpdateStmt>(&stmt);
  ASSERT_TRUE(node != nullptr);
  ASSERT_EQ(node->assignments.size(), assignment_count);
  EXPECT_EQ(node->assignments.at(0).column.name, "name");
  EXPECT_EQ(node->assignments.at(1).column.name, "age");
  ASSERT_TRUE(node->where != nullptr);

  auto* where = std::get_if<ColumnRef>(&node->where->node);
  ASSERT_TRUE(where != nullptr);
  EXPECT_EQ(where->name.name, "id");
  EXPECT_EQ(where->name.position.offset, sixth_byte);
}

TEST(SqlAst, DeleteSupportsOptionalWhere) {
  DeleteStmt delete_all;
  delete_all.table = make_identifier("users", first_byte);

  Stmt all_stmt{std::move(delete_all)};
  auto* all_node = std::get_if<DeleteStmt>(&all_stmt);
  ASSERT_TRUE(all_node != nullptr);
  EXPECT_TRUE(all_node->where == nullptr);

  DeleteStmt delete_filtered;
  delete_filtered.table = make_identifier("users", second_byte);
  auto left = std::make_unique<Expr>(make_column_expr("id", third_byte));
  auto right = std::make_unique<Expr>(make_int_literal(answer, fourth_byte));
  delete_filtered.where = std::make_unique<Expr>(
      make_binary(BinaryOp::gt, fifth_byte, std::move(left), std::move(right)));

  Stmt filtered_stmt{std::move(delete_filtered)};
  auto* filtered_node = std::get_if<DeleteStmt>(&filtered_stmt);
  ASSERT_TRUE(filtered_node != nullptr);
  ASSERT_TRUE(filtered_node->where != nullptr);

  auto* binary = std::get_if<BinaryExpr>(&filtered_node->where->node);
  ASSERT_TRUE(binary != nullptr);
  EXPECT_EQ(binary->op, BinaryOp::gt);
}

TEST(SqlAst, CreateTableKeepsDecimalPrecisionAndKeys) {
  CreateTableStmt create;
  create.table = make_identifier("accounts", first_byte);
  create.columns.push_back(ColumnDef{
      .name = make_identifier("id", second_byte),
      .type =
          TypeName{
              .kind = TypeNameKind::bigint,
              .decimal = std::nullopt,
              .position = SourcePosition{third_byte},
          },
      .is_primary_key = true,
  });
  create.columns.push_back(ColumnDef{
      .name = make_identifier("name", fourth_byte),
      .type =
          TypeName{
              .kind = TypeNameKind::varchar,
              .decimal = std::nullopt,
              .position = SourcePosition{fifth_byte},
          },
      .is_primary_key = false,
  });
  create.columns.push_back(ColumnDef{
      .name = make_identifier("balance", sixth_byte),
      .type =
          TypeName{
              .kind = TypeNameKind::decimal,
              .decimal = DecimalType{.precision = decimal_precision, .scale = decimal_scale},
              .position = SourcePosition{seventh_byte},
          },
      .is_primary_key = false,
  });

  Stmt stmt{std::move(create)};
  auto* node = std::get_if<CreateTableStmt>(&stmt);
  ASSERT_TRUE(node != nullptr);
  ASSERT_EQ(node->columns.size(), column_count);
  EXPECT_TRUE(node->columns.at(0).is_primary_key);
  EXPECT_EQ(node->columns.at(0).type.kind, TypeNameKind::bigint);
  EXPECT_FALSE(node->columns.at(0).type.decimal.has_value());
  EXPECT_FALSE(node->columns.at(1).is_primary_key);
  EXPECT_EQ(node->columns.at(1).type.kind, TypeNameKind::varchar);
  EXPECT_FALSE(node->columns.at(1).type.decimal.has_value());

  const auto& decimal = node->columns.at(2).type.decimal;
  ASSERT_TRUE(decimal.has_value());
  EXPECT_EQ(decimal->precision, decimal_precision);
  EXPECT_EQ(decimal->scale, decimal_scale);
  EXPECT_EQ(node->columns.at(2).type.position.offset, seventh_byte);
}

TEST(SqlAst, CreateIndexKeepsUniqueness) {
  CreateIndexStmt unique_index;
  unique_index.index = make_identifier("idx_users_id", first_byte);
  unique_index.table = make_identifier("users", second_byte);
  unique_index.column = make_identifier("id", third_byte);
  unique_index.unique = true;

  Stmt unique_stmt{std::move(unique_index)};
  auto* unique_node = std::get_if<CreateIndexStmt>(&unique_stmt);
  ASSERT_TRUE(unique_node != nullptr);
  EXPECT_TRUE(unique_node->unique);
  EXPECT_EQ(unique_node->index.name, "idx_users_id");
  EXPECT_EQ(unique_node->table.name, "users");
  EXPECT_EQ(unique_node->column.name, "id");

  CreateIndexStmt regular_index;
  regular_index.index = make_identifier("idx_users_name", first_byte);
  regular_index.table = make_identifier("users", second_byte);
  regular_index.column = make_identifier("name", third_byte);

  Stmt regular_stmt{std::move(regular_index)};
  auto* regular_node = std::get_if<CreateIndexStmt>(&regular_stmt);
  ASSERT_TRUE(regular_node != nullptr);
  EXPECT_FALSE(regular_node->unique);
}

TEST(SqlAst, DropStatementsRespectIfExists) {
  DropTableStmt drop_table;
  drop_table.table = make_identifier("users", first_byte);
  drop_table.if_exists = true;

  Stmt table_stmt{std::move(drop_table)};
  auto* table_node = std::get_if<DropTableStmt>(&table_stmt);
  ASSERT_TRUE(table_node != nullptr);
  EXPECT_TRUE(table_node->if_exists);

  DropTableStmt plain_table;
  plain_table.table = make_identifier("users", first_byte);
  Stmt plain_table_stmt{std::move(plain_table)};
  auto* plain_table_node = std::get_if<DropTableStmt>(&plain_table_stmt);
  ASSERT_TRUE(plain_table_node != nullptr);
  EXPECT_FALSE(plain_table_node->if_exists);

  DropIndexStmt drop_index;
  drop_index.index = make_identifier("idx_users_id", second_byte);
  drop_index.if_exists = false;

  Stmt index_stmt{std::move(drop_index)};
  auto* index_node = std::get_if<DropIndexStmt>(&index_stmt);
  ASSERT_TRUE(index_node != nullptr);
  EXPECT_FALSE(index_node->if_exists);
}

TEST(SqlAst, AlterTableAddsAndDropsColumns) {
  AlterTableAddColumnStmt add;
  add.table = make_identifier("users", first_byte);
  add.column = ColumnDef{
      .name = make_identifier("email", second_byte),
      .type =
          TypeName{
              .kind = TypeNameKind::varchar,
              .decimal = std::nullopt,
              .position = SourcePosition{third_byte},
          },
      .is_primary_key = false,
  };

  Stmt add_stmt{std::move(add)};
  auto* add_node = std::get_if<AlterTableAddColumnStmt>(&add_stmt);
  ASSERT_TRUE(add_node != nullptr);
  EXPECT_EQ(add_node->column.name.name, "email");
  EXPECT_EQ(add_node->column.type.kind, TypeNameKind::varchar);

  AlterTableDropColumnStmt drop;
  drop.table = make_identifier("users", fourth_byte);
  drop.column = make_identifier("email", fifth_byte);

  Stmt drop_stmt{std::move(drop)};
  auto* drop_node = std::get_if<AlterTableDropColumnStmt>(&drop_stmt);
  ASSERT_TRUE(drop_node != nullptr);
  EXPECT_EQ(drop_node->column.name, "email");
  EXPECT_EQ(drop_node->column.position.offset, fifth_byte);
}

TEST(SqlAst, RepresentsEveryExpressionAlternative) {
  Expr column = make_column_expr("id", first_byte);
  Expr literal = Expr{make_int_literal(answer, second_byte)};
  auto left = std::make_unique<Expr>(make_column_expr("id", third_byte));
  auto right = std::make_unique<Expr>(make_int_literal(comparison_value, fourth_byte));
  Expr binary = make_binary(BinaryOp::and_, fifth_byte, std::move(left), std::move(right));

  EXPECT_TRUE(std::holds_alternative<ColumnRef>(column.node));
  EXPECT_TRUE(std::holds_alternative<Literal>(literal.node));
  EXPECT_TRUE(std::holds_alternative<BinaryExpr>(binary.node));
}

TEST(SqlAst, RejectsInvalidBinaryExpressions) {
  EXPECT_THROW(BinaryExpr(BinaryOp::is_null, SourcePosition{first_byte},
                          std::make_unique<Expr>(make_column_expr("id", second_byte)),
                          std::make_unique<Expr>(make_int_literal(answer, third_byte))),
               std::invalid_argument);
  EXPECT_THROW(BinaryExpr(BinaryOp::eq, SourcePosition{first_byte},
                          std::make_unique<Expr>(make_column_expr("id", second_byte)), nullptr),
               std::invalid_argument);
  EXPECT_THROW(BinaryExpr(BinaryOp::eq, SourcePosition{first_byte}, nullptr,
                          std::make_unique<Expr>(make_int_literal(answer, third_byte))),
               std::invalid_argument);

  BinaryExpr valid{BinaryOp::is_null, SourcePosition{first_byte},
                   std::make_unique<Expr>(make_column_expr("id", second_byte)), nullptr};
  EXPECT_EQ(valid.op, BinaryOp::is_null);
  EXPECT_TRUE(valid.right == nullptr);
}

TEST(SqlAst, NestedExpressionTraversalTerminates) {
  auto left = std::make_unique<Expr>(make_column_expr("id", third_byte));
  auto right = std::make_unique<Expr>(make_int_literal(comparison_value, fourth_byte));
  auto equality = make_binary(BinaryOp::eq, fifth_byte, std::move(left), std::move(right));
  auto null_left = std::make_unique<Expr>(make_column_expr("name", sixth_byte));
  auto null_check = make_binary(BinaryOp::is_null, seventh_byte, std::move(null_left), nullptr);
  auto root = make_binary(BinaryOp::and_, eighth_byte, std::make_unique<Expr>(std::move(equality)),
                          std::make_unique<Expr>(std::move(null_check)));

  EXPECT_EQ(count_nodes(root), nested_node_count);
}

}  // namespace
