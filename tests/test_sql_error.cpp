#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;

using namespace sql;

constexpr std::size_t first_byte = 1;
constexpr std::size_t second_byte = 2;
constexpr int answer = 42;

TEST(SqlError, StoresAndComparesFields) {
  const SqlError error{
      .sqlstate = "42601",
      .message = "syntax error",
      .position = SourcePosition{first_byte},
  };

  EXPECT_EQ(error.sqlstate, "42601");
  EXPECT_EQ(error.message, "syntax error");
  EXPECT_EQ(error.position.offset, first_byte);
  EXPECT_EQ(error, (SqlError{.sqlstate = "42601",
                             .message = "syntax error",
                             .position = SourcePosition{first_byte}}));
  EXPECT_NE(error, (SqlError{.sqlstate = "42601",
                             .message = "syntax error",
                             .position = SourcePosition{second_byte}}));
}

TEST(SqlError, PositionZeroMeansUnknown) {
  EXPECT_EQ(SourcePosition{}.offset, 0U);
  EXPECT_EQ(SourcePosition{first_byte}.offset, first_byte);
}

TEST(SqlError, ResultCarriesValueOrError) {
  const auto value = SqlResult<int>{answer};
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), answer);

  const auto error = SqlResult<int>{std::unexpected(SqlError{
      .sqlstate = "22003",
      .message = "numeric value out of range",
      .position = SourcePosition{first_byte},
  })};
  ASSERT_FALSE(error.has_value());
  EXPECT_EQ(error.error().sqlstate, "22003");
  EXPECT_EQ(error.error().position.offset, first_byte);
}

}  // namespace
