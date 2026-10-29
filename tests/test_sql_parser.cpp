#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;

using namespace sql;

/// \brief 1-based position of the first occurrence of `needle` in `text`.
auto position_of(std::string_view text, std::string_view needle) -> SourcePosition {
  const std::size_t offset = text.find(needle);
  if (offset == std::string_view::npos) {
    throw std::invalid_argument{"needle not found in SQL text"};
  }
  return SourcePosition{offset + 1};
}

/// \brief 1-based position of the `occurrence`-th match of `needle` (0 = first).
auto position_of(std::string_view text, std::string_view needle, std::size_t occurrence)
    -> SourcePosition {
  std::size_t offset = text.find(needle);
  for (std::size_t index = 0; index < occurrence && offset != std::string_view::npos; ++index) {
    offset = text.find(needle, offset + 1);
  }
  if (offset == std::string_view::npos) {
    throw std::invalid_argument{"needle not found in SQL text"};
  }
  return SourcePosition{offset + 1};
}

auto end_position(std::string_view text) -> SourcePosition {
  return SourcePosition{text.size() + 1};
}

auto op_name(BinaryOp operation) -> std::string_view {
  switch (operation) {
    case BinaryOp::eq:
      return "=";
    case BinaryOp::ne:
      return "<>";
    case BinaryOp::lt:
      return "<";
    case BinaryOp::le:
      return "<=";
    case BinaryOp::gt:
      return ">";
    case BinaryOp::ge:
      return ">=";
    case BinaryOp::and_:
      return "and";
    case BinaryOp::or_:
      return "or";
    case BinaryOp::is_null:
      return "is-null";
    case BinaryOp::is_not_null:
      return "is-not-null";
  }
  return "?";
}

/// \brief Renders an expression as an S-expression, e.g. `(and (= a 1) (is-null b))`.
auto render(const Expr& expr) -> std::string {
  return std::visit(
      [](const auto& node) -> std::string {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, ColumnRef>) {
          return node.name.name;
        } else if constexpr (std::is_same_v<Node, Literal>) {
          return std::visit(
              [](const auto& value) -> std::string {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, std::int64_t>) {
                  return std::to_string(value);
                } else if constexpr (std::is_same_v<Value, DecimalLiteral>) {
                  return std::format("dec:{}", value.text);
                } else if constexpr (std::is_same_v<Value, std::string>) {
                  return std::format("'{}'", value);
                } else {
                  return "null";
                }
              },
              node.value);
        } else {
          if (node.right == nullptr) {
            return std::format("({} {})", op_name(node.op), render(*node.left));
          }
          return std::format("({} {} {})", op_name(node.op), render(*node.left),
                             render(*node.right));
        }
      },
      expr.node);
}

auto target_names(const SelectStmt& select) -> std::vector<std::string> {
  std::vector<std::string> names;
  names.reserve(select.targets.size());
  for (const auto& target : select.targets) {
    names.push_back(target.name);
  }
  return names;
}

/// \brief Parses `text` and renders its WHERE clause, or the error SQLSTATE.
auto where_of(std::string_view text) -> std::string {
  auto statement = sql::parse(text);
  if (!statement.has_value()) {
    return std::format("error {}", statement.error().sqlstate);
  }
  const auto* select = std::get_if<SelectStmt>(&statement.value());
  if (select == nullptr || select->where == nullptr) {
    return "no where";
  }
  return render(*select->where);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto expect_error(std::string_view text, std::string_view sqlstate, SourcePosition position)
    -> void {
  auto statement = sql::parse(text);
  ASSERT_FALSE(statement.has_value()) << "accepted: " << text;
  EXPECT_EQ(statement.error().sqlstate, sqlstate) << text << " -> " << statement.error().message;
  EXPECT_EQ(statement.error().position, position) << text << " -> " << statement.error().message;
}

// --- Accepted statements -----------------------------------------------------------------------

TEST(SqlParser, ParsesSelectStar) {
  constexpr std::string_view text = "SELECT * FROM users";
  auto statement = sql::parse(text);
  ASSERT_TRUE(statement.has_value()) << statement.error().message;

  const auto& select = std::get<SelectStmt>(statement.value());
  EXPECT_TRUE(select.targets.empty());
  EXPECT_EQ(select.table.name, "users");
  EXPECT_EQ(select.table.position, position_of(text, "users"));
  EXPECT_EQ(select.where, nullptr);
}

TEST(SqlParser, ParsesColumnListInOrderWithPositions) {
  constexpr std::string_view text = "SELECT id, name,email FROM users";
  auto statement = sql::parse(text);
  ASSERT_TRUE(statement.has_value()) << statement.error().message;

  const auto& select = std::get<SelectStmt>(statement.value());
  EXPECT_EQ(target_names(select), (std::vector<std::string>{"id", "name", "email"}));
  ASSERT_EQ(select.targets.size(), 3U);
  EXPECT_EQ(select.targets.at(0).position, position_of(text, "id"));
  EXPECT_EQ(select.targets.at(1).position, position_of(text, "name"));
  EXPECT_EQ(select.targets.at(2).position, position_of(text, "email"));
}

TEST(SqlParser, FoldsCaseAndAcceptsTrailingSemicolon) {
  auto statement = sql::parse("select ID from Users where Age >= 18;");
  ASSERT_TRUE(statement.has_value()) << statement.error().message;

  const auto& select = std::get<SelectStmt>(statement.value());
  EXPECT_EQ(target_names(select), (std::vector<std::string>{"id"}));
  EXPECT_EQ(select.table.name, "users");
  ASSERT_NE(select.where, nullptr);
  EXPECT_EQ(render(*select.where), "(>= age 18)");
}

TEST(SqlParser, AcceptsWhitespaceAfterSemicolon) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 1 ;  \n"), "(= a 1)");
}

TEST(SqlParser, ParsesEveryComparisonOperator) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 1"), "(= a 1)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a <> 1"), "(<> a 1)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a != 1"), "(<> a 1)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a < 1"), "(< a 1)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a <= 1"), "(<= a 1)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a > 1"), "(> a 1)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a >= 1"), "(>= a 1)");
}

TEST(SqlParser, ComparesColumnsAndLiteralsOnEitherSide) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE 1 < a"), "(< 1 a)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = b"), "(= a b)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE 1 = 1"), "(= 1 1)");
}

TEST(SqlParser, AndBindsTighterThanOr) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 1 OR b = 2 AND c = 3"),
            "(or (= a 1) (and (= b 2) (= c 3)))");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 1 AND b = 2 OR c = 3"),
            "(or (and (= a 1) (= b 2)) (= c 3))");
}

TEST(SqlParser, AndAndOrAreLeftAssociative) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 1 AND b = 2 AND c = 3"),
            "(and (and (= a 1) (= b 2)) (= c 3))");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 1 OR b = 2 OR c = 3"),
            "(or (or (= a 1) (= b 2)) (= c 3))");
}

TEST(SqlParser, ParenthesesOverridePrecedence) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE (a = 1 OR b = 2) AND c = 3"),
            "(and (or (= a 1) (= b 2)) (= c 3))");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE ((a = 1))"), "(= a 1)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE (a) = (1)"), "(= a 1)");
}

TEST(SqlParser, ParsesIsNullAndIsNotNull) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a IS NULL"), "(is-null a)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a IS NOT NULL"), "(is-not-null a)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a IS NULL AND b IS NOT NULL OR c = 1"),
            "(or (and (is-null a) (is-not-null b)) (= c 1))");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE (a = 1) IS NULL"), "(is-null (= a 1))");
}

TEST(SqlParser, ParsesEveryLiteralKind) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 42"), "(= a 42)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = -42"), "(= a -42)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 12.50"), "(= a dec:12.50)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = -0.5"), "(= a dec:-0.5)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 'it''s'"), "(= a 'it's')");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = ''"), "(= a '')");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = NULL"), "(= a null)");
}

TEST(SqlParser, KeepsIntegersOutsideBigintRangeAsDecimalText) {
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 9223372036854775807"), "(= a 9223372036854775807)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = -9223372036854775808"),
            "(= a -9223372036854775808)");
  EXPECT_EQ(where_of("SELECT * FROM t WHERE a = 9223372036854775808"),
            "(= a dec:9223372036854775808)");
}

TEST(SqlParser, RecordsOperatorAndLiteralPositions) {
  constexpr std::string_view text = "SELECT * FROM t WHERE price >= 10.5 AND code IS NOT NULL";
  auto statement = sql::parse(text);
  ASSERT_TRUE(statement.has_value()) << statement.error().message;

  const auto& select = std::get<SelectStmt>(statement.value());
  ASSERT_NE(select.where, nullptr);
  const auto& conjunction = std::get<BinaryExpr>(select.where->node);
  EXPECT_EQ(conjunction.op, BinaryOp::and_);
  EXPECT_EQ(conjunction.position, position_of(text, "AND"));

  const auto& comparison = std::get<BinaryExpr>(conjunction.left->node);
  EXPECT_EQ(comparison.position, position_of(text, ">="));
  EXPECT_EQ(std::get<ColumnRef>(comparison.left->node).name.position, position_of(text, "price"));
  EXPECT_EQ(std::get<Literal>(comparison.right->node).position, position_of(text, "10.5"));

  const auto& null_check = std::get<BinaryExpr>(conjunction.right->node);
  EXPECT_EQ(null_check.op, BinaryOp::is_not_null);
  EXPECT_EQ(null_check.position, position_of(text, "IS"));
  EXPECT_EQ(null_check.right, nullptr);
}

TEST(SqlParser, AcceptsDistinctAndAllAsColumnNames) {
  auto statement = sql::parse("SELECT distinct, all FROM t");
  ASSERT_TRUE(statement.has_value()) << statement.error().message;
  EXPECT_EQ(target_names(std::get<SelectStmt>(statement.value())),
            (std::vector<std::string>{"distinct", "all"}));
}

TEST(SqlParser, AcceptsModerateNesting) {
  const std::string text =
      std::format("SELECT * FROM t WHERE {}a = 1{}", std::string(64, '('), std::string(64, ')'));
  EXPECT_EQ(where_of(text), "(= a 1)");
}

TEST(SqlParser, ParseTokensAcceptsLexerOutput) {
  auto tokens = sql::tokenize("SELECT a FROM t");
  ASSERT_TRUE(tokens.has_value());
  auto statement = sql::parse_tokens(tokens.value());
  ASSERT_TRUE(statement.has_value()) << statement.error().message;
  EXPECT_TRUE(std::holds_alternative<SelectStmt>(statement.value()));
}

TEST(SqlParser, ParsesInsertWithAndWithoutColumns) {
  auto explicit_columns = sql::parse("INSERT INTO users (id, name) VALUES (1, 'Ana')");
  ASSERT_TRUE(explicit_columns.has_value()) << explicit_columns.error().message;
  const auto& insert = std::get<InsertStmt>(explicit_columns.value());
  EXPECT_EQ(insert.table.name, "users");
  ASSERT_EQ(insert.columns.size(), 2U);
  EXPECT_EQ(insert.columns.at(0).name, "id");
  ASSERT_EQ(insert.values.size(), 2U);

  auto implicit_columns = sql::parse("INSERT INTO users VALUES (1, 'Ana')");
  ASSERT_TRUE(implicit_columns.has_value()) << implicit_columns.error().message;
  EXPECT_TRUE(std::get<InsertStmt>(implicit_columns.value()).columns.empty());
}

TEST(SqlParser, ParsesUpdateAndDeleteWithFilters) {
  auto update = sql::parse("UPDATE users SET name = 'Ana', age = 20 WHERE id = 1");
  ASSERT_TRUE(update.has_value()) << update.error().message;
  const auto& update_stmt = std::get<UpdateStmt>(update.value());
  ASSERT_EQ(update_stmt.assignments.size(), 2U);
  EXPECT_EQ(update_stmt.assignments.at(1).column.name, "age");
  ASSERT_NE(update_stmt.where, nullptr);

  auto delete_ = sql::parse("DELETE FROM users WHERE id = 1");
  ASSERT_TRUE(delete_.has_value()) << delete_.error().message;
  EXPECT_NE(std::get<DeleteStmt>(delete_.value()).where, nullptr);
}

TEST(SqlParser, RejectsMalformedDml) {
  constexpr std::string_view empty_columns = "INSERT INTO t () VALUES (1)";
  expect_error(empty_columns, sqlstate::syntax_error, position_of(empty_columns, ")"));

  constexpr std::string_view trailing_value = "INSERT INTO t VALUES (1,)";
  expect_error(trailing_value, sqlstate::syntax_error, position_of(trailing_value, ")"));

  constexpr std::string_view missing_assignment = "UPDATE t SET WHERE id = 1";
  expect_error(missing_assignment, sqlstate::syntax_error,
               position_of(missing_assignment, "WHERE"));
}

TEST(SqlParser, ParsesCreateTableTypesAndPrimaryKey) {
  auto statement = sql::parse(
      "CREATE TABLE accounts (id BIGINT PRIMARY KEY, name VARCHAR, balance DECIMAL(10,2))");
  ASSERT_TRUE(statement.has_value()) << statement.error().message;
  const auto& create = std::get<CreateTableStmt>(statement.value());
  ASSERT_EQ(create.columns.size(), 3U);
  EXPECT_TRUE(create.columns.at(0).is_primary_key);
  EXPECT_EQ(create.columns.at(1).type.kind, TypeNameKind::varchar);
  ASSERT_TRUE(create.columns.at(2).type.decimal.has_value());
  EXPECT_EQ(create.columns.at(2).type.decimal->precision, 10);
  EXPECT_EQ(create.columns.at(2).type.decimal->scale, 2);
}

TEST(SqlParser, ParsesCreateIndexDropAndAlter) {
  auto index = sql::parse("CREATE UNIQUE INDEX accounts_id_idx ON accounts (id)");
  ASSERT_TRUE(index.has_value()) << index.error().message;
  EXPECT_TRUE(std::get<CreateIndexStmt>(index.value()).unique);

  auto drop = sql::parse("DROP TABLE IF EXISTS accounts");
  ASSERT_TRUE(drop.has_value()) << drop.error().message;
  EXPECT_TRUE(std::get<DropTableStmt>(drop.value()).if_exists);

  auto add = sql::parse("ALTER TABLE accounts ADD COLUMN age BIGINT");
  ASSERT_TRUE(add.has_value()) << add.error().message;
  EXPECT_EQ(std::get<AlterTableAddColumnStmt>(add.value()).column.name.name, "age");

  auto remove = sql::parse("ALTER TABLE accounts DROP COLUMN age");
  ASSERT_TRUE(remove.has_value()) << remove.error().message;
  EXPECT_EQ(std::get<AlterTableDropColumnStmt>(remove.value()).column.name, "age");
}

TEST(SqlParser, RejectsMalformedDdl) {
  constexpr std::string_view empty_table = "CREATE TABLE t ()";
  expect_error(empty_table, sqlstate::syntax_error, position_of(empty_table, ")"));

  constexpr std::string_view decimal_without_scale = "CREATE TABLE t (n DECIMAL(10))";
  expect_error(decimal_without_scale, sqlstate::syntax_error,
               position_of(decimal_without_scale, ")"));

  constexpr std::string_view malformed_if = "DROP TABLE IF t";
  expect_error(malformed_if, sqlstate::syntax_error, position_of(malformed_if, "t"));

  constexpr std::string_view missing_column_keyword = "ALTER TABLE t ADD age BIGINT";
  expect_error(missing_column_keyword, sqlstate::syntax_error,
               position_of(missing_column_keyword, "age"));
}

// --- Rejected statements -----------------------------------------------------------------------

TEST(SqlParser, RejectsJoinOrderByAndLimitAsUnsupported) {
  constexpr std::string_view join = "SELECT * FROM a JOIN b";
  expect_error(join, sqlstate::feature_not_supported, position_of(join, "JOIN"));

  constexpr std::string_view inner = "SELECT * FROM a INNER JOIN b";
  expect_error(inner, sqlstate::feature_not_supported, position_of(inner, "INNER"));

  constexpr std::string_view order = "SELECT * FROM t ORDER BY a";
  expect_error(order, sqlstate::feature_not_supported, position_of(order, "ORDER"));

  constexpr std::string_view limit = "SELECT * FROM t WHERE a = 1 LIMIT 5";
  expect_error(limit, sqlstate::feature_not_supported, position_of(limit, "LIMIT"));
}

TEST(SqlParser, RejectsTwoStatements) {
  constexpr std::string_view text = "SELECT * FROM a; SELECT * FROM b";
  expect_error(text, sqlstate::syntax_error, position_of(text, "SELECT", 1));

  constexpr std::string_view doubled = "SELECT * FROM a;;";
  expect_error(doubled, sqlstate::syntax_error, position_of(doubled, ";", 1));
}

TEST(SqlParser, RejectsTwoStatementsWithoutSeparator) {
  constexpr std::string_view text = "SELECT * FROM a SELECT * FROM b";
  expect_error(text, sqlstate::syntax_error, position_of(text, "SELECT", 1));
}

TEST(SqlParser, RejectsEmptyInput) {
  expect_error("", sqlstate::syntax_error, SourcePosition{1});
  expect_error("   ", sqlstate::syntax_error, end_position("   "));
  expect_error(";", sqlstate::syntax_error, SourcePosition{1});
}

TEST(SqlParser, ReportsSyntaxErrorsAtTheOffendingToken) {
  constexpr std::string_view no_from = "SELECT a";
  expect_error(no_from, sqlstate::syntax_error, end_position(no_from));

  constexpr std::string_view no_table = "SELECT a FROM";
  expect_error(no_table, sqlstate::syntax_error, end_position(no_table));

  constexpr std::string_view no_targets = "SELECT FROM t";
  expect_error(no_targets, sqlstate::syntax_error, position_of(no_targets, "FROM"));

  constexpr std::string_view trailing_comma = "SELECT a, FROM t";
  expect_error(trailing_comma, sqlstate::syntax_error, position_of(trailing_comma, "FROM"));

  constexpr std::string_view keyword_table = "SELECT a FROM where";
  expect_error(keyword_table, sqlstate::syntax_error, position_of(keyword_table, "where"));

  constexpr std::string_view empty_where = "SELECT a FROM t WHERE";
  expect_error(empty_where, sqlstate::syntax_error, end_position(empty_where));

  constexpr std::string_view missing_operand = "SELECT a FROM t WHERE a =";
  expect_error(missing_operand, sqlstate::syntax_error, end_position(missing_operand));

  constexpr std::string_view dangling_and = "SELECT a FROM t WHERE a = 1 AND";
  expect_error(dangling_and, sqlstate::syntax_error, end_position(dangling_and));

  constexpr std::string_view chained = "SELECT a FROM t WHERE a = 1 = 2";
  expect_error(chained, sqlstate::syntax_error, position_of(chained, "=", 1));

  constexpr std::string_view is_value = "SELECT a FROM t WHERE a IS 5";
  expect_error(is_value, sqlstate::syntax_error, position_of(is_value, "5"));

  constexpr std::string_view is_not_value = "SELECT a FROM t WHERE a IS NOT 5";
  expect_error(is_not_value, sqlstate::syntax_error, position_of(is_not_value, "5"));

  constexpr std::string_view unclosed = "SELECT a FROM t WHERE (a = 1";
  expect_error(unclosed, sqlstate::syntax_error, end_position(unclosed));

  constexpr std::string_view unopened = "SELECT a FROM t WHERE a = 1)";
  expect_error(unopened, sqlstate::syntax_error, position_of(unopened, ")"));

  constexpr std::string_view empty_parens = "SELECT a FROM t WHERE ()";
  expect_error(empty_parens, sqlstate::syntax_error, position_of(empty_parens, ")"));

  constexpr std::string_view keyword_operand = "SELECT a FROM t WHERE a = from";
  expect_error(keyword_operand, sqlstate::syntax_error, position_of(keyword_operand, "from"));
}

TEST(SqlParser, ReportsUnsupportedFeaturesAtTheirToken) {
  constexpr std::string_view aggregate = "SELECT count(*) FROM t";
  expect_error(aggregate, sqlstate::feature_not_supported, position_of(aggregate, "count"));

  constexpr std::string_view distinct = "SELECT DISTINCT a FROM t";
  expect_error(distinct, sqlstate::feature_not_supported, position_of(distinct, "DISTINCT"));

  constexpr std::string_view alias = "SELECT a AS b FROM t";
  expect_error(alias, sqlstate::feature_not_supported, position_of(alias, "AS"));

  constexpr std::string_view tables = "SELECT * FROM a, b";
  expect_error(tables, sqlstate::feature_not_supported, position_of(tables, ","));

  constexpr std::string_view subquery = "SELECT * FROM t WHERE (SELECT a FROM u) = 1";
  expect_error(subquery, sqlstate::feature_not_supported, position_of(subquery, "SELECT", 1));

  constexpr std::string_view negation = "SELECT * FROM t WHERE NOT a = 1";
  expect_error(negation, sqlstate::feature_not_supported, position_of(negation, "NOT"));

  constexpr std::string_view in_list = "SELECT * FROM t WHERE a IN (1, 2)";
  expect_error(in_list, sqlstate::feature_not_supported, position_of(in_list, "IN"));

  constexpr std::string_view mixed_star = "SELECT a, * FROM t";
  expect_error(mixed_star, sqlstate::feature_not_supported, position_of(mixed_star, "*"));
}

TEST(SqlParser, RejectsStatementsOtherThanSelect) {
  constexpr std::string_view insert = "INSERT INTO t VALUES (1)";
  expect_error(insert, sqlstate::feature_not_supported, SourcePosition{1});

  constexpr std::string_view begin = "BEGIN";
  expect_error(begin, sqlstate::feature_not_supported, SourcePosition{1});

  constexpr std::string_view unknown = "EXPLAIN SELECT * FROM t";
  expect_error(unknown, sqlstate::syntax_error, SourcePosition{1});
}

TEST(SqlParser, PropagatesLexicalErrors) {
  constexpr std::string_view text = "SELECT * FROM t WHERE a = 'open";
  expect_error(text, sqlstate::syntax_error, position_of(text, "'"));
}

TEST(SqlParser, RejectsExcessiveNesting) {
  const std::string text = std::format("SELECT * FROM t WHERE {}a = 1{}", std::string(1000, '('),
                                       std::string(1000, ')'));
  auto statement = sql::parse(text);
  ASSERT_FALSE(statement.has_value());
  EXPECT_EQ(statement.error().sqlstate, sqlstate::statement_too_complex);
}

TEST(SqlParser, ParseTokensRejectsSequenceWithoutEndOfInput) {
  const std::vector<Token> tokens{
      Token{TokenKind::keyword, "select", SourcePosition{1}, Keyword::select}};
  auto statement = sql::parse_tokens(tokens);
  ASSERT_FALSE(statement.has_value());
  EXPECT_EQ(statement.error().sqlstate, sqlstate::syntax_error);

  auto empty = sql::parse_tokens(std::span<const Token>{});
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error().sqlstate, sqlstate::syntax_error);
}

}  // namespace
