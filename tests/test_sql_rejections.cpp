/// Rejection matrix of the read path (issue #61).
///
/// Every case runs through `prepare_query`, the only door to the executor, and
/// must fail with the expected SQLSTATE at the expected position. A counting
/// executor double proves that no rejected statement reaches execution.

#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;
namespace storage = kravidb::storage;

using namespace sql;

/// \brief `atttypmod` of an attribute without type modifier.
constexpr std::int32_t unconstrained_typmod = -1;

/// \brief `(0 << 8) | 0`: DECIMAL with precision 0, rejected by `encode_typmod`.
constexpr std::int32_t invalid_precision_typmod = 0;

constexpr std::uint8_t amount_precision = 5;
constexpr std::uint8_t amount_scale = 2;

/// \brief Marker that checks the error position at the end of the input.
constexpr std::string_view at_end = "<end>";

/// \brief Marker that skips the position check.
constexpr std::string_view any_position{};

struct RejectionCase {
  /// \brief Test name suffix; letters, digits and underscores only.
  std::string_view name;
  std::string_view sql;
  std::string_view sqlstate;
  /// \brief First occurrence of this text is where the error must point.
  std::string_view at;
  /// \brief Which occurrence of `at` to use (0 = first).
  std::size_t occurrence{0};
};

/// \brief Shows the SQL text instead of raw bytes when a case fails.
void PrintTo(const RejectionCase& test_case, std::ostream* out) {
  *out << '"' << test_case.sql << "\" -> " << test_case.sqlstate;
}

auto expected_position(const RejectionCase& test_case) -> std::optional<SourcePosition> {
  if (test_case.at.empty()) {
    return std::nullopt;
  }
  if (test_case.at == at_end) {
    return SourcePosition{test_case.sql.size() + 1};
  }
  std::size_t offset = test_case.sql.find(test_case.at);
  for (std::size_t index = 0; index < test_case.occurrence && offset != std::string_view::npos;
       ++index) {
    offset = test_case.sql.find(test_case.at, offset + 1);
  }
  if (offset == std::string_view::npos) {
    throw std::invalid_argument{"position marker not found in SQL text"};
  }
  return SourcePosition{offset + 1};
}

auto attribute(std::string name, storage::Oid type, std::int32_t typmod = unconstrained_typmod)
    -> storage::AttributeDesc {
  return storage::AttributeDesc{.name = std::move(name),
                                .type_oid = type,
                                .attnum = 0,
                                .not_null = false,
                                .typmod = typmod,
                                .primary_key = false};
}

/// \brief Front end plus an executor double that only counts what reaches it.
class ReadPath {
  public:
  ReadPath() {
    catalog_.bootstrap();
    catalog_.create_table(
        transaction_, storage::TableDef{
                          .name = "items",
                          .attributes =
                              {
                                  attribute("id", storage::int8_oid),
                                  attribute("label", storage::varchar_oid),
                                  attribute("amount", storage::numeric_oid,
                                            storage::encode_typmod(amount_precision, amount_scale)),
                                  attribute("qty", storage::int8_oid),
                              },
                          .primary_key = "id",
                      });
    // A catalog row whose DECIMAL typmod is invalid (precision 0).
    catalog_.create_table(
        transaction_,
        storage::TableDef{
            .name = "broken",
            .attributes = {attribute("d", storage::numeric_oid, invalid_precision_typmod)},
            .primary_key = "",
        });
  }

  auto run(std::string_view text) -> SqlResult<Query> {
    auto query = sql::prepare_query(text, catalog_, transaction_);
    if (query.has_value()) {
      ++executed_;
    }
    return query;
  }

  [[nodiscard]] auto executed() const -> int { return executed_; }

  private:
  storage::SchemaCatalog catalog_;
  storage::Transaction transaction_{};
  int executed_{0};
};

// clang-format off
const std::array rejection_cases{
    // --- Syntax (42601) -------------------------------------------------------------------------
    RejectionCase{.name = "syntax_missing_from", .sql = "SELECT id items", .sqlstate = sqlstate::syntax_error, .at = "items"},
    RejectionCase{.name = "syntax_missing_table", .sql = "SELECT id FROM", .sqlstate = sqlstate::syntax_error, .at = at_end},
    RejectionCase{.name = "syntax_empty_select_list", .sql = "SELECT FROM items", .sqlstate = sqlstate::syntax_error, .at = "FROM"},
    RejectionCase{.name = "syntax_trailing_comma", .sql = "SELECT id, FROM items", .sqlstate = sqlstate::syntax_error, .at = "FROM"},
    RejectionCase{.name = "syntax_empty_where", .sql = "SELECT * FROM items WHERE", .sqlstate = sqlstate::syntax_error, .at = at_end},
    RejectionCase{.name = "syntax_missing_operand", .sql = "SELECT * FROM items WHERE id >", .sqlstate = sqlstate::syntax_error, .at = at_end},
    RejectionCase{.name = "syntax_chained_comparison", .sql = "SELECT * FROM items WHERE id = 1 = 1", .sqlstate = sqlstate::syntax_error, .at = "=", .occurrence = 1},
    RejectionCase{.name = "syntax_unclosed_paren", .sql = "SELECT * FROM items WHERE (id = 1", .sqlstate = sqlstate::syntax_error, .at = at_end},
    RejectionCase{.name = "syntax_is_without_null", .sql = "SELECT * FROM items WHERE id IS 1", .sqlstate = sqlstate::syntax_error, .at = "1"},
    RejectionCase{.name = "syntax_unknown_operator", .sql = "SELECT * FROM items WHERE id == 1", .sqlstate = sqlstate::syntax_error, .at = "=", .occurrence = 1},
    RejectionCase{.name = "syntax_lexical_error", .sql = "SELECT * FROM items WHERE id = 1 + 2", .sqlstate = sqlstate::syntax_error, .at = "+"},
    RejectionCase{.name = "syntax_unterminated_string", .sql = "SELECT * FROM items WHERE label = 'abc", .sqlstate = sqlstate::syntax_error, .at = "'"},
    RejectionCase{.name = "syntax_comment", .sql = "SELECT * FROM items -- all", .sqlstate = sqlstate::syntax_error, .at = "-"},
    RejectionCase{.name = "syntax_empty_query", .sql = "", .sqlstate = sqlstate::syntax_error, .at = at_end},
    RejectionCase{.name = "syntax_only_semicolon", .sql = ";", .sqlstate = sqlstate::syntax_error, .at = ";"},
    RejectionCase{.name = "syntax_unknown_statement", .sql = "EXPLAIN SELECT * FROM items", .sqlstate = sqlstate::syntax_error, .at = "EXPLAIN"},
    RejectionCase{.name = "syntax_long_identifier", .sql = "SELECT * FROM aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", .sqlstate = sqlstate::syntax_error, .at = "a"},

    // --- Two statements and quoted identifiers (42601) ------------------------------------------
    RejectionCase{.name = "two_statements", .sql = "SELECT * FROM items; SELECT * FROM items", .sqlstate = sqlstate::syntax_error, .at = "SELECT", .occurrence = 1},
    RejectionCase{.name = "two_statements_no_separator", .sql = "SELECT * FROM items SELECT * FROM items", .sqlstate = sqlstate::syntax_error, .at = "SELECT", .occurrence = 1},
    RejectionCase{.name = "two_statements_second_invalid", .sql = "SELECT * FROM items; DROP TABLE items", .sqlstate = sqlstate::syntax_error, .at = "DROP"},
    RejectionCase{.name = "double_semicolon", .sql = "SELECT * FROM items;;", .sqlstate = sqlstate::syntax_error, .at = ";", .occurrence = 1},
    RejectionCase{.name = "quoted_table", .sql = "SELECT * FROM \"items\"", .sqlstate = sqlstate::syntax_error, .at = "\""},
    RejectionCase{.name = "quoted_column", .sql = "SELECT \"id\" FROM items", .sqlstate = sqlstate::syntax_error, .at = "\""},
    RejectionCase{.name = "quoted_column_in_where", .sql = "SELECT * FROM items WHERE \"qty\" > 1", .sqlstate = sqlstate::syntax_error, .at = "\""},
    RejectionCase{.name = "backquoted_identifier", .sql = "SELECT `id` FROM items", .sqlstate = sqlstate::syntax_error, .at = "`"},

    // --- Unknown relation and column (42P01, 42703) ---------------------------------------------
    RejectionCase{.name = "unknown_table", .sql = "SELECT * FROM missing", .sqlstate = sqlstate::undefined_table, .at = "missing"},
    RejectionCase{.name = "unknown_table_with_where", .sql = "SELECT id FROM missing WHERE id = 1", .sqlstate = sqlstate::undefined_table, .at = "missing"},
    RejectionCase{.name = "unknown_column_in_list", .sql = "SELECT id, price FROM items", .sqlstate = sqlstate::undefined_column, .at = "price"},
    RejectionCase{.name = "unknown_column_in_where", .sql = "SELECT * FROM items WHERE price > 1", .sqlstate = sqlstate::undefined_column, .at = "price"},
    RejectionCase{.name = "unknown_column_in_null_test", .sql = "SELECT * FROM items WHERE note IS NULL", .sqlstate = sqlstate::undefined_column, .at = "note"},
    RejectionCase{.name = "unknown_column_deep", .sql = "SELECT * FROM items WHERE (id = 1 OR (qty > 2 AND nope = 3))", .sqlstate = sqlstate::undefined_column, .at = "nope"},
    RejectionCase{.name = "unquoted_true_is_a_column", .sql = "SELECT * FROM items WHERE true", .sqlstate = sqlstate::undefined_column, .at = "true"},

    // --- Incompatible types (42804) -------------------------------------------------------------
    RejectionCase{.name = "type_bigint_vs_varchar", .sql = "SELECT * FROM items WHERE id = 'one'", .sqlstate = sqlstate::datatype_mismatch, .at = "="},
    RejectionCase{.name = "type_varchar_vs_bigint", .sql = "SELECT * FROM items WHERE label < 10", .sqlstate = sqlstate::datatype_mismatch, .at = "<"},
    RejectionCase{.name = "type_decimal_vs_varchar", .sql = "SELECT * FROM items WHERE amount >= label", .sqlstate = sqlstate::datatype_mismatch, .at = ">="},
    RejectionCase{.name = "type_boolean_vs_bigint", .sql = "SELECT * FROM items WHERE (id = 1) <> 0", .sqlstate = sqlstate::datatype_mismatch, .at = "<>"},
    RejectionCase{.name = "type_and_operand", .sql = "SELECT * FROM items WHERE qty AND id = 1", .sqlstate = sqlstate::datatype_mismatch, .at = "qty"},
    RejectionCase{.name = "type_or_operand", .sql = "SELECT * FROM items WHERE id = 1 OR label", .sqlstate = sqlstate::datatype_mismatch, .at = "label"},
    RejectionCase{.name = "type_where_not_boolean", .sql = "SELECT * FROM items WHERE amount", .sqlstate = sqlstate::datatype_mismatch, .at = "amount"},
    RejectionCase{.name = "type_where_literal", .sql = "SELECT * FROM items WHERE 'yes'", .sqlstate = sqlstate::datatype_mismatch, .at = "'yes'"},

    // --- DECIMAL out of range and invalid precision (22003, 22023) ------------------------------
    RejectionCase{.name = "decimal_too_many_integer_digits", .sql = "SELECT * FROM items WHERE amount < 1000", .sqlstate = sqlstate::numeric_value_out_of_range, .at = "1000"},
    RejectionCase{.name = "decimal_negative_out_of_range", .sql = "SELECT * FROM items WHERE amount > -1000.00", .sqlstate = sqlstate::numeric_value_out_of_range, .at = "-1000"},
    RejectionCase{.name = "decimal_literal_on_left", .sql = "SELECT * FROM items WHERE 12345.6 = amount", .sqlstate = sqlstate::numeric_value_out_of_range, .at = "12345"},
    RejectionCase{.name = "decimal_literal_too_long", .sql = "SELECT * FROM items WHERE qty = 12345678901234567890.5", .sqlstate = sqlstate::numeric_value_out_of_range, .at = "1234"},
    RejectionCase{.name = "decimal_literal_scale_too_large", .sql = "SELECT * FROM items WHERE qty < 0.0000000000000000001", .sqlstate = sqlstate::numeric_value_out_of_range, .at = "0.0"},
    RejectionCase{.name = "bigint_literal_too_large", .sql = "SELECT * FROM items WHERE id = 9223372036854775808", .sqlstate = sqlstate::numeric_value_out_of_range, .at = "9223"},
    RejectionCase{.name = "decimal_invalid_precision", .sql = "SELECT d FROM broken", .sqlstate = sqlstate::invalid_parameter_value, .at = "d FROM"},
    RejectionCase{.name = "decimal_invalid_precision_star", .sql = "SELECT * FROM broken", .sqlstate = sqlstate::invalid_parameter_value, .at = "broken"},

    // --- Unsupported features (0A000) -----------------------------------------------------------
    RejectionCase{.name = "join", .sql = "SELECT * FROM items JOIN other", .sqlstate = sqlstate::feature_not_supported, .at = "JOIN"},
    RejectionCase{.name = "inner_join", .sql = "SELECT * FROM items INNER JOIN other ON id = id", .sqlstate = sqlstate::feature_not_supported, .at = "INNER"},
    RejectionCase{.name = "left_join", .sql = "SELECT * FROM items LEFT JOIN other ON id = id", .sqlstate = sqlstate::feature_not_supported, .at = "LEFT"},
    RejectionCase{.name = "cross_join", .sql = "SELECT * FROM items CROSS JOIN other", .sqlstate = sqlstate::feature_not_supported, .at = "CROSS"},
    RejectionCase{.name = "comma_join", .sql = "SELECT * FROM items, other", .sqlstate = sqlstate::feature_not_supported, .at = ","},
    RejectionCase{.name = "aggregate_count", .sql = "SELECT count(*) FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "count"},
    RejectionCase{.name = "aggregate_sum", .sql = "SELECT id, sum(qty) FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "sum"},
    RejectionCase{.name = "aggregate_in_where", .sql = "SELECT * FROM items WHERE max(qty) > 1", .sqlstate = sqlstate::feature_not_supported, .at = "max"},
    RejectionCase{.name = "group_by", .sql = "SELECT qty FROM items GROUP BY qty", .sqlstate = sqlstate::feature_not_supported, .at = "GROUP"},
    RejectionCase{.name = "having", .sql = "SELECT qty FROM items HAVING qty > 1", .sqlstate = sqlstate::feature_not_supported, .at = "HAVING"},
    RejectionCase{.name = "order_by", .sql = "SELECT * FROM items ORDER BY id", .sqlstate = sqlstate::feature_not_supported, .at = "ORDER"},
    RejectionCase{.name = "order_by_after_where", .sql = "SELECT * FROM items WHERE id > 1 ORDER BY id", .sqlstate = sqlstate::feature_not_supported, .at = "ORDER"},
    RejectionCase{.name = "limit", .sql = "SELECT * FROM items LIMIT 10", .sqlstate = sqlstate::feature_not_supported, .at = "LIMIT"},
    RejectionCase{.name = "limit_after_where", .sql = "SELECT * FROM items WHERE id > 1 LIMIT 10;", .sqlstate = sqlstate::feature_not_supported, .at = "LIMIT"},
    RejectionCase{.name = "offset", .sql = "SELECT * FROM items OFFSET 5", .sqlstate = sqlstate::feature_not_supported, .at = "OFFSET"},
    RejectionCase{.name = "union", .sql = "SELECT id FROM items UNION SELECT id FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "UNION"},
    RejectionCase{.name = "distinct", .sql = "SELECT DISTINCT qty FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "DISTINCT"},
    RejectionCase{.name = "column_alias", .sql = "SELECT id AS key FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "AS"},
    RejectionCase{.name = "table_alias", .sql = "SELECT * FROM items AS i", .sqlstate = sqlstate::feature_not_supported, .at = "AS"},
    RejectionCase{.name = "subquery_in_where", .sql = "SELECT * FROM items WHERE (SELECT 1) = 1", .sqlstate = sqlstate::feature_not_supported, .at = "SELECT", .occurrence = 1},
    RejectionCase{.name = "subquery_in_from", .sql = "SELECT * FROM (SELECT * FROM items)", .sqlstate = sqlstate::feature_not_supported, .at = "("},
    RejectionCase{.name = "not_operator", .sql = "SELECT * FROM items WHERE NOT id = 1", .sqlstate = sqlstate::feature_not_supported, .at = "NOT"},
    RejectionCase{.name = "in_list", .sql = "SELECT * FROM items WHERE id IN (1, 2)", .sqlstate = sqlstate::feature_not_supported, .at = "IN"},
    RejectionCase{.name = "like", .sql = "SELECT * FROM items WHERE label LIKE 'a'", .sqlstate = sqlstate::feature_not_supported, .at = "LIKE"},
    RejectionCase{.name = "between", .sql = "SELECT * FROM items WHERE qty BETWEEN 1 AND 2", .sqlstate = sqlstate::feature_not_supported, .at = "BETWEEN"},
    RejectionCase{.name = "literal_in_select_list", .sql = "SELECT 1 FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "1"},
    RejectionCase{.name = "star_with_columns", .sql = "SELECT id, * FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "*"},

    // --- Transaction control and other commands (0A000) -----------------------------------------
    RejectionCase{.name = "begin", .sql = "BEGIN", .sqlstate = sqlstate::feature_not_supported, .at = "BEGIN"},
    RejectionCase{.name = "begin_semicolon", .sql = "BEGIN;", .sqlstate = sqlstate::feature_not_supported, .at = "BEGIN"},
    RejectionCase{.name = "start_transaction", .sql = "START TRANSACTION", .sqlstate = sqlstate::feature_not_supported, .at = "START"},
    RejectionCase{.name = "commit", .sql = "COMMIT", .sqlstate = sqlstate::feature_not_supported, .at = "COMMIT"},
    RejectionCase{.name = "commit_lowercase", .sql = "commit;", .sqlstate = sqlstate::feature_not_supported, .at = "commit"},
    RejectionCase{.name = "rollback", .sql = "ROLLBACK", .sqlstate = sqlstate::feature_not_supported, .at = "ROLLBACK"},
    RejectionCase{.name = "begin_then_select", .sql = "BEGIN; SELECT * FROM items", .sqlstate = sqlstate::feature_not_supported, .at = "BEGIN"},
    // The DML/DDL parser (#70) may later accept these; the read path must still
    // reject them, so only the SQLSTATE is pinned.
    RejectionCase{.name = "insert", .sql = "INSERT INTO items VALUES (1, 'a', 1.00, 1)", .sqlstate = sqlstate::feature_not_supported, .at = any_position},
    RejectionCase{.name = "update", .sql = "UPDATE items SET qty = 0", .sqlstate = sqlstate::feature_not_supported, .at = any_position},
    RejectionCase{.name = "delete", .sql = "DELETE FROM items", .sqlstate = sqlstate::feature_not_supported, .at = any_position},
    RejectionCase{.name = "create_table", .sql = "CREATE TABLE t (a BIGINT)", .sqlstate = sqlstate::feature_not_supported, .at = any_position},
    RejectionCase{.name = "drop_table", .sql = "DROP TABLE items", .sqlstate = sqlstate::feature_not_supported, .at = any_position},
    RejectionCase{.name = "alter_table", .sql = "ALTER TABLE items ADD COLUMN b BIGINT", .sqlstate = sqlstate::feature_not_supported, .at = any_position},
};

/// \brief Statements of the supported subset; they must reach the executor.
const std::array accepted_cases{
    std::string_view{"SELECT * FROM items"},
    std::string_view{"SELECT id, label FROM items;"},
    std::string_view{"select ID from ITEMS where QTY >= 1"},
    std::string_view{"SELECT * FROM items WHERE amount <= 999.99 AND (label = 'x' OR qty IS NULL)"},
    std::string_view{"SELECT * FROM items WHERE label IS NOT NULL OR id <> 1"},
    std::string_view{"SELECT * FROM items WHERE amount = NULL"},
    std::string_view{"SELECT * FROM items WHERE id < 2.5"},
};
// clang-format on

class SqlRejectionMatrix : public ::testing::TestWithParam<RejectionCase> {};

TEST_P(SqlRejectionMatrix, ReturnsExpectedSqlstateAndNeverExecutes) {
  const RejectionCase& test_case = GetParam();
  ReadPath read_path;

  auto query = read_path.run(test_case.sql);

  ASSERT_FALSE(query.has_value()) << "accepted: " << test_case.sql;
  EXPECT_EQ(query.error().sqlstate, test_case.sqlstate)
      << test_case.sql << " -> " << query.error().message;
  if (const auto position = expected_position(test_case)) {
    EXPECT_EQ(query.error().position, *position)
        << test_case.sql << " -> " << query.error().message;
  }
  EXPECT_FALSE(query.error().message.empty());
  EXPECT_EQ(read_path.executed(), 0);
}

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
INSTANTIATE_TEST_SUITE_P(ReadPath, SqlRejectionMatrix, ::testing::ValuesIn(rejection_cases),
                         [](const ::testing::TestParamInfo<RejectionCase>& info) -> std::string {
                           return std::string{info.param.name};
                         });
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

TEST(SqlRejectionMatrixCoverage, CoversEveryReadSqlstate) {
  const std::array required{
      sqlstate::syntax_error,
      sqlstate::feature_not_supported,
      sqlstate::undefined_table,
      sqlstate::undefined_column,
      sqlstate::datatype_mismatch,
      sqlstate::numeric_value_out_of_range,
      sqlstate::invalid_parameter_value,
  };
  for (const auto state : required) {
    const bool covered = std::ranges::any_of(
        rejection_cases,
        [state](const RejectionCase& entry) -> bool { return entry.sqlstate == state; });
    EXPECT_TRUE(covered) << "no rejection case for SQLSTATE " << state;
  }
}

TEST(SqlRejectionMatrixCoverage, SupportedStatementsReachTheExecutor) {
  ReadPath read_path;
  for (const auto text : accepted_cases) {
    auto query = read_path.run(text);
    EXPECT_TRUE(query.has_value()) << text << " -> " << query.error().message;
  }
  EXPECT_EQ(read_path.executed(), static_cast<int>(accepted_cases.size()));
}

}  // namespace
