#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using namespace kravidb::sql;
using namespace kravidb::storage;

constexpr RelationStats table_stats{.pages = 1000, .tuples = 10000};
constexpr PlannerIndex primary_index{.oid = 42, .first_attnum = 1, .unique = true, .pages = 16};

auto query_for(std::string_view sql) -> SqlResult<Query> {
  SchemaCatalog catalog;
  Transaction transaction;
  static_cast<void>(catalog.create_table(
      transaction, TableDef{.name = "users",
                            .attributes = {{.name = "id", .type_oid = int8_oid},
                                           {.name = "name", .type_oid = varchar_oid},
                                           {.name = "balance",
                                            .type_oid = numeric_oid,
                                            .typmod = encode_typmod(5, 2)}},
                            .primary_key = "id"}));
  return prepare_query(sql, catalog, transaction);
}

TEST(SqlPlanner, PrimaryEqualityUsesIndexAndExposesCost) {
  const auto query = query_for("SELECT name FROM users WHERE id = 1;");
  ASSERT_TRUE(query);
  const auto plan = plan_select(*query, table_stats, std::array{primary_index});
  ASSERT_TRUE(plan);
  EXPECT_EQ(plan->scan.kind, ScanKind::index);
  EXPECT_EQ(plan->scan.index_oid, primary_index.oid);
  ASSERT_TRUE(plan->scan.condition);
  EXPECT_EQ(plan->scan.condition->comparison, BinaryOp::eq);
  EXPECT_EQ(std::get<std::int64_t>(plan->scan.condition->value), 1);
  EXPECT_DOUBLE_EQ(plan->scan.estimated_rows, 1);
  EXPECT_GT(plan->scan.startup_cost, 0);
  EXPECT_LT(plan->scan.total_cost, table_stats.pages);
  EXPECT_TRUE(plan->has_filter);
  EXPECT_EQ(plan->targets.front().name, "name");
  const auto diagnostic = explain_plan(*plan);
  EXPECT_NE(diagnostic.find("Projection"), std::string::npos);
  EXPECT_NE(diagnostic.find("Filter"), std::string::npos);
  EXPECT_NE(diagnostic.find("IndexScan"), std::string::npos);
  EXPECT_NE(diagnostic.find("cost="), std::string::npos);
}

TEST(SqlPlanner, NoPhysicalIndexMeansSequentialEvenForPrimaryKey) {
  const auto query = query_for("SELECT * FROM users WHERE id >= 10;");
  ASSERT_TRUE(query);
  const auto plan = plan_select(*query, table_stats);
  ASSERT_TRUE(plan);
  EXPECT_EQ(plan->scan.kind, ScanKind::sequential);
  EXPECT_FALSE(plan->scan.condition);
  EXPECT_DOUBLE_EQ(plan->scan.total_cost, 1100);
  EXPECT_NE(explain_plan(*plan).find("SeqScan"), std::string::npos);
}

TEST(SqlPlanner, RangeAndReversedComparisonPreserveOperator) {
  // A sparse heap makes a range index path cheaper than fetching all pages.
  constexpr RelationStats sparse{.pages = 10000, .tuples = 1000};
  for (const auto sql :
       {"SELECT * FROM users WHERE id >= 10;", "SELECT * FROM users WHERE 10 <= id;"}) {
    const auto query = query_for(sql);
    ASSERT_TRUE(query);
    const auto plan = plan_select(*query, sparse, std::array{primary_index});
    ASSERT_TRUE(plan);
    ASSERT_EQ(plan->scan.kind, ScanKind::index);
    ASSERT_TRUE(plan->scan.condition);
    EXPECT_EQ(plan->scan.condition->comparison, BinaryOp::ge);
  }
}

TEST(SqlPlanner, ConjunctionCanUseIndexButDisjunctionCannotLoseRows) {
  const auto conjunction = query_for("SELECT * FROM users WHERE id = 1 AND name = 'Ada';");
  ASSERT_TRUE(conjunction);
  const auto indexed = plan_select(*conjunction, table_stats, std::array{primary_index});
  ASSERT_TRUE(indexed);
  EXPECT_EQ(indexed->scan.kind, ScanKind::index);
  EXPECT_TRUE(indexed->has_filter);
  for (const auto sql :
       {"SELECT * FROM users WHERE id = 1 OR name = 'Ada';", "SELECT * FROM users WHERE id <> 1;",
        "SELECT * FROM users WHERE id = NULL;", "SELECT * FROM users WHERE balance = 12.34;",
        "SELECT * FROM users WHERE id = 1.5;"}) {
    const auto query = query_for(sql);
    ASSERT_TRUE(query);
    const auto plan = plan_select(*query, table_stats, std::array{primary_index});
    ASSERT_TRUE(plan);
    EXPECT_EQ(plan->scan.kind, ScanKind::sequential) << sql;
  }
}

TEST(SqlPlanner, OnlyLeadingAttributeAndCheapestPathAreUsed) {
  const auto query = query_for("SELECT * FROM users WHERE id = 1;");
  ASSERT_TRUE(query);
  const std::array indexes{PlannerIndex{.oid = 43, .first_attnum = 2, .unique = true, .pages = 1},
                           primary_index};
  const auto plan = plan_select(*query, table_stats, indexes);
  ASSERT_TRUE(plan);
  EXPECT_EQ(plan->scan.index_oid, primary_index.oid);
  const auto small = plan_select(*query, {.pages = 1, .tuples = 1}, indexes);
  ASSERT_TRUE(small);
  EXPECT_EQ(small->scan.kind, ScanKind::sequential);
}

TEST(SqlPlanner, VarcharEqualityUsesMatchingIndex) {
  const auto query = query_for("SELECT * FROM users WHERE name = 'Ada';");
  ASSERT_TRUE(query);
  const auto plan = plan_select(
      *query, table_stats,
      std::array{PlannerIndex{.oid = 43, .first_attnum = 2, .unique = false, .pages = 16}});
  ASSERT_TRUE(plan);
  ASSERT_EQ(plan->scan.kind, ScanKind::index);
  ASSERT_TRUE(plan->scan.condition);
  EXPECT_EQ(std::get<std::string>(plan->scan.condition->value), "Ada");
}

TEST(SqlPlanner, InvalidStatisticsAreRejectedAndEmptyTablesAreFinite) {
  const auto query = query_for("SELECT * FROM users;");
  ASSERT_TRUE(query);
  EXPECT_FALSE(plan_select(*query, {.pages = -1, .tuples = 1}));
  EXPECT_FALSE(
      plan_select(*query, {.pages = std::numeric_limits<double>::infinity(), .tuples = 1}));
  EXPECT_FALSE(
      plan_select(*query, table_stats, std::array{PlannerIndex{.oid = 0, .first_attnum = 1}}));
  const auto empty = plan_select(*query, {});
  ASSERT_TRUE(empty);
  EXPECT_DOUBLE_EQ(empty->scan.total_cost, 0);
  EXPECT_DOUBLE_EQ(empty->scan.estimated_rows, 0);
  EXPECT_FALSE(empty->has_filter);
}

}  // namespace
