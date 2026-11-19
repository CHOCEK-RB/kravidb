#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;

[[nodiscard]] auto position(std::size_t line, std::size_t column) -> sql::SourcePosition {
  return sql::SourcePosition{.offset = 0, .line = line, .column = column};
}

[[nodiscard]] auto type(sql::TypeKind kind) -> sql::SqlType {
  return sql::SqlType{.kind = kind, .decimal = std::nullopt};
}

[[nodiscard]] auto column(std::string name, std::int16_t attnum, sql::TypeKind kind,
                          std::size_t line, std::size_t column_position)
    -> std::unique_ptr<sql::TypedExpr> {
  return std::make_unique<sql::TypedExpr>(sql::TypedExpr{
      .node = sql::ColumnVar{.attnum = attnum, .name = std::move(name)},
      .type = type(kind),
      .position = position(line, column_position),
  });
}

[[nodiscard]] auto constant(sql::ConstValue value, sql::TypeKind kind, std::size_t line,
                            std::size_t column_position) -> std::unique_ptr<sql::TypedExpr> {
  return std::make_unique<sql::TypedExpr>(sql::TypedExpr{
      .node = sql::Const{.value = std::move(value)},
      .type = type(kind),
      .position = position(line, column_position),
  });
}

[[nodiscard]] auto operation(sql::BinaryOp binary, sql::TypeKind kind, std::size_t line,
                             std::size_t column_position, std::unique_ptr<sql::TypedExpr> left,
                             std::unique_ptr<sql::TypedExpr> right = nullptr)
    -> std::unique_ptr<sql::TypedExpr> {
  return std::make_unique<sql::TypedExpr>(sql::TypedExpr{
      .node = sql::OpExpr{.op = binary, .left = std::move(left), .right = std::move(right)},
      .type = type(kind),
      .position = position(line, column_position),
  });
}

TEST(SqlExplain, FormatsTargetsAndWhere) {
  const sql::Query query{
      .relation = 42,
      .relation_name = "students",
      .targets =
          {
              sql::TargetEntry{.name = "id", .attnum = 1, .type = type(sql::TypeKind::bigint)},
              sql::TargetEntry{.name = "name", .attnum = 2, .type = type(sql::TypeKind::varchar)},
          },
      .where = operation(sql::BinaryOp::ge, sql::TypeKind::boolean, 1, 35,
                         column("gpa", 3, sql::TypeKind::float8, 1, 29),
                         constant(std::int64_t{3}, sql::TypeKind::float8, 1, 35)),
  };

  EXPECT_EQ(sql::format_query(query),
            "SelectStmt students (oid=42)\n"
            "├─ Targets\n"
            "│  ├─ id : bigint (#1)\n"
            "│  └─ name : varchar (#2)\n"
            "└─ Where : boolean\n"
            "   └─ >= : boolean @ 1:35\n"
            "      ├─ gpa : float (#3) @ 1:29\n"
            "      └─ 3 : float @ 1:35");
}

TEST(SqlExplain, FormatsBooleanAndNullConstants) {
  const sql::Query query{
      .relation = 7,
      .relation_name = "flags",
      .targets =
          {
              sql::TargetEntry{.name = "active", .attnum = 1, .type = type(sql::TypeKind::boolean)},
          },
      .where = operation(sql::BinaryOp::and_, sql::TypeKind::boolean, 1, 20,
                         column("active", 1, sql::TypeKind::boolean, 1, 10),
                         operation(sql::BinaryOp::is_null, sql::TypeKind::boolean, 1, 30,
                                   constant(std::monostate{}, sql::TypeKind::null, 1, 33))),
  };

  EXPECT_EQ(sql::format_query(query),
            "SelectStmt flags (oid=7)\n"
            "├─ Targets\n"
            "│  └─ active : boolean (#1)\n"
            "└─ Where : boolean\n"
            "   └─ AND : boolean @ 1:20\n"
            "      ├─ active : boolean (#1) @ 1:10\n"
            "      └─ IS NULL : boolean @ 1:30\n"
            "         └─ NULL : unknown @ 1:33");
}

TEST(SqlExplain, FormatsEmptyWhere) {
  const sql::Query query{
      .relation = 1,
      .relation_name = "empty",
      .targets =
          {
              sql::TargetEntry{.name = "id", .attnum = 1, .type = type(sql::TypeKind::bigint)},
          },
      .where = nullptr,
  };

  EXPECT_EQ(sql::format_query(query),
            "SelectStmt empty (oid=1)\n"
            "├─ Targets\n"
            "│  └─ id : bigint (#1)\n"
            "└─ Where : none");
}

}  // namespace
