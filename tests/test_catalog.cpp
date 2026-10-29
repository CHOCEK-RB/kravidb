#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using kravidb::storage::AttributeDesc;
using kravidb::storage::Catalog;
using kravidb::storage::decode_typmod;
using kravidb::storage::encode_typmod;
using kravidb::storage::Field;
using kravidb::storage::FieldType;
using kravidb::storage::FileCatalog;
using kravidb::storage::FileRelationFile;
using kravidb::storage::Fork;
using kravidb::storage::HeapTuple;
using kravidb::storage::kd_am_oid;
using kravidb::storage::kd_attribute_oid;
using kravidb::storage::kd_class_oid;
using kravidb::storage::kd_index_oid;
using kravidb::storage::kd_namespace_oid;
using kravidb::storage::kd_type_oid;
using kravidb::storage::numeric_oid;
using kravidb::storage::Oid;
using kravidb::storage::Page;
using kravidb::storage::physical_page_size;
using kravidb::storage::RelCache;
using kravidb::storage::RelFileNode;
using kravidb::storage::SchemaCatalog;
using kravidb::storage::SqlStateError;
using kravidb::storage::TableDef;
using kravidb::storage::Transaction;
using kravidb::storage::Typmod;
using kravidb::storage::varchar_oid;

constexpr std::size_t kd_class_column_count = 9;
constexpr std::size_t kd_attribute_column_count = 7;
constexpr std::size_t kd_type_column_count = 6;
constexpr std::size_t kd_index_column_count = 5;
constexpr std::size_t kd_namespace_column_count = 2;
constexpr std::size_t kd_am_column_count = 3;
constexpr std::uint32_t duplicate_type_oid = 99;
constexpr std::uint8_t flip_mask = 0xFFU;
constexpr std::size_t checksum_offset = 8;
constexpr std::size_t tuple_header_size = 24;
constexpr std::uint8_t corrupt_type = 0xFFU;

[[nodiscard]] auto relation_node(Oid oid) -> RelFileNode {
  return RelFileNode{.tablespace = 0, .database = 1, .relation = oid};
}

[[nodiscard]] auto relation_path(const std::filesystem::path& root, Oid oid)
    -> std::filesystem::path {
  return root / std::to_string(oid);
}

auto flip_file_byte(const std::filesystem::path& root, RelFileNode node, std::size_t offset)
    -> void {
  FileRelationFile file{root, node};
  std::array<std::byte, physical_page_size> buffer{};
  file.read(Fork::main, 0, buffer);
  buffer.at(offset) ^= std::byte{flip_mask};
  file.write(Fork::main, 0, buffer);
  file.fsync(Fork::main);
}

using TupleMutator = std::function<void(std::vector<std::optional<Field>>&)>;

auto mutate_first_tuple(const std::filesystem::path& root, Oid oid, const TupleMutator& mutator)
    -> void {
  FileRelationFile file{root, relation_node(oid)};
  std::array<std::byte, physical_page_size> buffer{};
  file.read(Fork::main, 0, buffer);
  auto page = Page::from_bytes(buffer);
  const auto item = page.item(0);
  const auto bytes = std::span<const std::byte>{buffer}.subspan(item.offset(), item.length());
  const auto tuple = HeapTuple::from_bytes(bytes);
  if (!tuple.has_value()) {
    throw std::runtime_error{"test tuple is malformed"};
  }
  auto fields = tuple->fields();
  mutator(fields);
  const auto replacement = HeapTuple::from_fields(fields, tuple->header());
  if (replacement.size() != tuple->size()) {
    throw std::runtime_error{"test mutation changed the tuple size"};
  }
  const auto destination = page.mutable_bytes().subspan(item.offset(), replacement.size());
  std::ranges::copy(replacement.bytes(), destination.begin());
  page.set_checksum();
  file.write(Fork::main, 0, page.bytes());
  file.fsync(Fork::main);
}

[[nodiscard]] auto sqlstate_of(const std::function<void()>& operation) -> std::string {
  try {
    operation();
  } catch (const SqlStateError& error) {
    return std::string{error.sqlstate()};
  }
  return {};
}

class CatalogTest : public ::testing::Test {
  protected:
  void SetUp() override {
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("kravidb_catalog_" + std::to_string(stamp) + "_" + std::to_string(counter++));
    std::filesystem::create_directories(path_);
  }

  void TearDown() override {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path_, error));
  }

  [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

  private:
  std::filesystem::path path_;
};

TEST(Typmod, RoundTripsPrecisionAndScale) {
  const auto encoded = encode_typmod(10, 2);
  EXPECT_EQ(encoded, static_cast<std::int32_t>((10U << 8U) | 2U));
  const auto decoded = decode_typmod(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->precision, 10);
  EXPECT_EQ(decoded->scale, 2);
  EXPECT_EQ(decode_typmod(-1), std::nullopt);
  EXPECT_EQ(decode_typmod(0), std::nullopt);
  EXPECT_EQ(decode_typmod(0x0302), (Typmod{.precision = 3, .scale = 2}));
}

TEST(Typmod, RejectsInvalidPrecisionScale) {
  EXPECT_THROW(static_cast<void>(encode_typmod(0, 0)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(encode_typmod(256, 0)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(encode_typmod(2, 3)), std::invalid_argument);
  EXPECT_EQ(decode_typmod(0x10000), std::nullopt);
}

TEST_F(CatalogTest, BootstrapCreatesSixRelations) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  for (const Oid oid :
       {kd_class_oid, kd_attribute_oid, kd_type_oid, kd_index_oid, kd_namespace_oid, kd_am_oid}) {
    EXPECT_TRUE(std::filesystem::exists(relation_path(path(), oid))) << oid;
  }
  catalog.open();
  Transaction transaction{.xid = 1};
  EXPECT_EQ(catalog.list_relations(transaction).size(), 6U);
}

TEST_F(CatalogTest, BootstrapIsIdempotent) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  std::map<Oid, std::vector<std::byte>> first;
  for (const Oid oid :
       {kd_class_oid, kd_attribute_oid, kd_type_oid, kd_index_oid, kd_namespace_oid, kd_am_oid}) {
    FileRelationFile file{path(), relation_node(oid)};
    std::array<std::byte, physical_page_size> buffer{};
    file.read(Fork::main, 0, buffer);
    first.try_emplace(oid, std::vector<std::byte>{buffer.begin(), buffer.end()});
  }
  catalog.bootstrap();
  for (const auto& [oid, bytes] : first) {
    FileRelationFile file{path(), relation_node(oid)};
    std::array<std::byte, physical_page_size> buffer{};
    file.read(Fork::main, 0, buffer);
    EXPECT_EQ((std::vector<std::byte>{buffer.begin(), buffer.end()}), bytes) << oid;
  }
}

TEST_F(CatalogTest, BootstrapCompletesMissingRelation) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  std::filesystem::remove(relation_path(path(), kd_type_oid));
  FileCatalog repaired{path()};
  repaired.bootstrap();
  EXPECT_TRUE(std::filesystem::exists(relation_path(path(), kd_type_oid)));
  repaired.open();
  Transaction transaction{.xid = 1};
  const auto relation = repaired.lookup_relation(transaction, "kd_type");
  ASSERT_TRUE(relation.has_value());
  EXPECT_EQ(relation->attributes.size(), kd_type_column_count);
}

TEST_F(CatalogTest, BootstrapRejectsCorruptRelation) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  flip_file_byte(path(), relation_node(kd_class_oid), checksum_offset);
  FileCatalog corrupt{path()};
  EXPECT_THROW(corrupt.bootstrap(), SqlStateError);
}

TEST_F(CatalogTest, OpenRejectsMissingRequiredRelation) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  std::filesystem::remove(relation_path(path(), kd_class_oid));
  FileCatalog missing{path()};
  EXPECT_THROW(missing.open(), SqlStateError);
}

TEST_F(CatalogTest, OpenAcceptsEmptyIndexRelation) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  std::filesystem::remove(relation_path(path(), kd_index_oid));
  FileCatalog empty_index{path()};
  empty_index.open();
  Transaction transaction{.xid = 1};
  const auto relation = empty_index.lookup_relation(transaction, "kd_index");
  ASSERT_TRUE(relation.has_value());
}

TEST_F(CatalogTest, ListsSelfDescribingRelations) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  catalog.open();
  Transaction transaction{.xid = 1};
  const auto relations = catalog.list_relations(transaction);
  ASSERT_EQ(relations.size(), 6U);
  EXPECT_EQ(relations.at(0).oid, kd_class_oid);
  EXPECT_EQ(relations.at(0).name, "kd_class");
  EXPECT_EQ(relations.at(0).attributes.size(), kd_class_column_count);
  EXPECT_EQ(relations.at(1).attributes.size(), kd_attribute_column_count);
  EXPECT_EQ(relations.at(2).attributes.size(), kd_type_column_count);
  EXPECT_EQ(relations.at(3).attributes.size(), kd_index_column_count);
  EXPECT_EQ(relations.at(4).attributes.size(), kd_namespace_column_count);
  EXPECT_EQ(relations.at(5).attributes.size(), kd_am_column_count);
  EXPECT_EQ(relations.at(0).attributes.at(0).name, "oid");
  EXPECT_EQ(relations.at(0).attributes.at(0).type_oid, kravidb::storage::int8_oid);
  EXPECT_TRUE(relations.at(0).attributes.at(0).not_null);
}

TEST_F(CatalogTest, LookupRelationByNameAndOid) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  catalog.open();
  Transaction transaction{.xid = 1};
  const auto by_name = catalog.lookup_relation(transaction, "kd_attribute");
  ASSERT_TRUE(by_name.has_value());
  EXPECT_EQ(by_name->oid, kd_attribute_oid);
  EXPECT_FALSE(catalog.lookup_relation(transaction, "missing").has_value());
  const auto attribute = catalog.lookup_attribute(transaction, kd_type_oid, "typname");
  ASSERT_TRUE(attribute.has_value());
  EXPECT_EQ(attribute->type_oid, varchar_oid);
  EXPECT_FALSE(catalog.lookup_attribute(transaction, kd_type_oid, "missing").has_value());
}

TEST_F(CatalogTest, RejectsBadChecksumWithLocation) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  flip_file_byte(path(), relation_node(kd_class_oid), checksum_offset);
  FileCatalog corrupt{path()};
  try {
    corrupt.open();
    FAIL() << "open() accepted a corrupt page";
  } catch (const SqlStateError& error) {
    const std::string message{error.what()};
    EXPECT_NE(message.find(path().string()), std::string::npos);
    EXPECT_NE(message.find("kd_class"), std::string::npos);
    EXPECT_NE(message.find("block 0"), std::string::npos);
    EXPECT_NE(message.find("slot 0"), std::string::npos);
  }
}

TEST_F(CatalogTest, RejectsMalformedTuple) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  FileRelationFile file{path(), relation_node(kd_class_oid)};
  std::array<std::byte, physical_page_size> buffer{};
  file.read(Fork::main, 0, buffer);
  auto page = Page::from_bytes(buffer);
  const auto item = page.item(0);
  *(page.mutable_bytes().begin() + static_cast<std::ptrdiff_t>(item.offset() + tuple_header_size)) =
      std::byte{corrupt_type};
  page.set_checksum();
  file.write(Fork::main, 0, page.bytes());
  FileCatalog corrupt{path()};
  EXPECT_THROW(corrupt.open(), SqlStateError);
}

TEST_F(CatalogTest, RejectsUnknownTypeOid) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  mutate_first_tuple(path(), kd_attribute_oid, [](std::vector<std::optional<Field>>& fields) {
    fields.at(2) = Field{static_cast<std::int64_t>(duplicate_type_oid)};
  });
  FileCatalog corrupt{path()};
  EXPECT_EQ(sqlstate_of([&corrupt]() -> void { corrupt.open(); }), "42704");
}

TEST_F(CatalogTest, RelCacheBuildsSchemaFromRelationDesc) {
  SchemaCatalog catalog;
  const TableDef definition{
      .name = "users",
      .attributes = {AttributeDesc{.name = "key", .type_oid = kravidb::storage::int8_oid},
                     AttributeDesc{.name = "payload", .type_oid = varchar_oid}},
      .primary_key = "key",
  };
  Transaction transaction{.xid = 1};
  const Oid oid = catalog.create_table(transaction, definition);
  RelCache cache{catalog};
  const auto relation = cache.relation("users");
  ASSERT_NE(relation, nullptr);
  EXPECT_EQ(relation->oid, oid);
  EXPECT_EQ(cache.relation(oid), relation);
  const auto schema = cache.schema_of(oid);
  ASSERT_NE(schema, nullptr);
  ASSERT_EQ(schema->column_count(), 2U);
  EXPECT_EQ(schema->column_at(0).type, FieldType::Int);
  EXPECT_EQ(schema->column_at(1).type, FieldType::Varchar);
}

TEST_F(CatalogTest, RelCacheMarksPrimaryKey) {
  SchemaCatalog catalog;
  const TableDef definition{
      .name = "users",
      .attributes = {AttributeDesc{.name = "key", .type_oid = kravidb::storage::int8_oid},
                     AttributeDesc{.name = "payload", .type_oid = varchar_oid}},
      .primary_key = "key",
  };
  Transaction transaction{.xid = 1};
  const Oid oid = catalog.create_table(transaction, definition);
  RelCache cache{catalog};
  const auto schema = cache.schema_of(oid);
  ASSERT_NE(schema, nullptr);
  ASSERT_TRUE(schema->primary_key_index().has_value());
  EXPECT_EQ(schema->primary_key_index().value(), 0U);
  EXPECT_TRUE(cache.require_column(oid, "key").primary_key);
}

TEST_F(CatalogTest, RelCacheRequireColumnThrows42703) {
  SchemaCatalog catalog;
  Transaction transaction{.xid = 1};
  const Oid oid = catalog.create_table(
      transaction,
      {.name = "users",
       .attributes = {AttributeDesc{.name = "key", .type_oid = kravidb::storage::int8_oid}}});
  RelCache cache{catalog};
  EXPECT_EQ(sqlstate_of([&cache, oid]() -> void {
              static_cast<void>(cache.require_column(oid, "missing"));
            }),
            "42703");
  EXPECT_THROW(static_cast<void>(cache.require_column(oid, "missing")), SqlStateError);
}

TEST_F(CatalogTest, FileCatalogMutatorsThrowReadOnly) {
  FileCatalog catalog{path()};
  catalog.bootstrap();
  catalog.open();
  Transaction transaction{.xid = 1};
  EXPECT_EQ(sqlstate_of([&catalog, &transaction]() -> void {
              static_cast<void>(catalog.create_table(transaction, {}));
            }),
            "0A000");
  EXPECT_EQ(sqlstate_of([&catalog, &transaction]() -> void {
              catalog.drop_table(transaction, kd_class_oid);
            }),
            "0A000");
  EXPECT_EQ(sqlstate_of([&catalog, &transaction]() -> void {
              static_cast<void>(catalog.create_index(transaction, {}));
            }),
            "0A000");
}

TEST_F(CatalogTest, RestartPreservesTablesAndColumns) {
  {
    FileCatalog catalog{path()};
    catalog.bootstrap();
    catalog.open();
  }
  FileCatalog reopened{path()};
  reopened.open();
  Transaction transaction{.xid = 1};
  const auto relation = reopened.lookup_relation(transaction, "kd_attribute");
  ASSERT_TRUE(relation.has_value());
  EXPECT_EQ(relation->oid, kd_attribute_oid);
  ASSERT_EQ(relation->attributes.size(), kd_attribute_column_count);
  EXPECT_EQ(relation->attributes.at(0).name, "attrelid");
  EXPECT_EQ(relation->attributes.at(2).type_oid, kravidb::storage::int8_oid);
  EXPECT_EQ(relation->attributes.at(1).type_oid, varchar_oid);
  EXPECT_EQ(relation->attributes.at(1).name, "attname");
  EXPECT_EQ(relation->attributes.at(4).typmod, -1);
}

}  // namespace
