#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace storage = kravidb::storage;

TEST(SchemaCatalogLookup, FindsRelationsAndColumnsByKey) {
  storage::SchemaCatalog catalog;
  storage::Transaction transaction{};

  const storage::Oid oid = catalog.create_table(
      transaction,
      {.name = "students",
       .attributes =
           {
               storage::AttributeDesc{.name = "id", .type_oid = storage::int8_oid},
               storage::AttributeDesc{.name = "name", .type_oid = storage::varchar_oid},
           },
       .primary_key = "id"});

  const auto relation = catalog.lookup_relation(transaction, "students");
  ASSERT_TRUE(relation.has_value());
  EXPECT_EQ(relation->oid, oid);
  EXPECT_EQ(relation->attributes.at(0).attnum, 1);
  EXPECT_EQ(relation->attributes.at(1).attnum, 2);

  const auto column = catalog.lookup_attribute(transaction, oid, "name");
  ASSERT_TRUE(column.has_value());
  EXPECT_EQ(column->attnum, 2);
  EXPECT_EQ(column->type_oid, storage::varchar_oid);
  EXPECT_FALSE(catalog.lookup_attribute(transaction, oid, "missing").has_value());
}

TEST(SchemaCatalogLookup, DropRemovesNameAndColumnEntries) {
  storage::SchemaCatalog catalog;
  storage::Transaction transaction{};

  const storage::Oid oid = catalog.create_table(
      transaction,
      {.name = "courses",
       .attributes = {storage::AttributeDesc{.name = "id", .type_oid = storage::int8_oid}},
       .primary_key = "id"});

  catalog.drop_table(transaction, oid);

  EXPECT_FALSE(catalog.lookup_relation(transaction, "courses").has_value());
  EXPECT_FALSE(catalog.lookup_attribute(transaction, oid, "id").has_value());
}

TEST(SchemaCatalogLookup, CreateIndexStillFindsTheRelation) {
  storage::SchemaCatalog catalog;
  storage::Transaction transaction{};

  const storage::Oid oid = catalog.create_table(
      transaction,
      {.name = "enrollments",
       .attributes = {storage::AttributeDesc{.name = "student_id", .type_oid = storage::int8_oid}},
       .primary_key = "student_id"});
  const storage::Oid index = catalog.create_index(
      transaction,
      {.relation = oid, .name = "enrollments_student_id", .column = "student_id", .unique = false});

  const auto relation = catalog.lookup_relation(transaction, "enrollments");
  ASSERT_TRUE(relation.has_value());
  ASSERT_EQ(relation->indexes.size(), 1U);
  EXPECT_EQ(relation->indexes.at(0), index);
}

TEST(RelCacheLookup, ResolvesColumnsThroughTheCatalogIndex) {
  storage::SchemaCatalog catalog;
  storage::Transaction transaction{};

  const storage::Oid oid = catalog.create_table(
      transaction,
      {.name = "products",
       .attributes =
           {
               storage::AttributeDesc{.name = "id", .type_oid = storage::int8_oid},
               storage::AttributeDesc{.name = "label", .type_oid = storage::varchar_oid},
           },
       .primary_key = "id"});
  const storage::RelCache cache{catalog};

  EXPECT_EQ(cache.require_column(oid, "label").attnum, 2);
  EXPECT_THROW(static_cast<void>(cache.require_column(oid, "missing")), storage::SqlStateError);
}

}  // namespace
