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

constexpr std::uint8_t price_precision = 8;
constexpr std::uint8_t price_scale = 2;

auto position_of(std::string_view text, std::string_view needle) -> SourcePosition {
  const std::size_t offset = text.find(needle);
  if (offset == std::string_view::npos) {
    throw std::invalid_argument{"needle not found in SQL text"};
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

auto bigint() -> SqlType {
  return SqlType{.kind = TypeKind::bigint, .decimal = std::nullopt};
}
auto varchar() -> SqlType {
  return SqlType{.kind = TypeKind::varchar, .decimal = std::nullopt};
}
auto decimal(std::uint8_t precision, std::uint8_t scale) -> SqlType {
  return SqlType{.kind = TypeKind::decimal,
                 .decimal = DecimalType{.precision = precision, .scale = scale}};
}

/// \brief Catalog with one `products` table, backed by the in-memory adapter.
class SqlAnalyzerTest : public ::testing::Test {
  protected:
  void SetUp() override {
    catalog_.bootstrap();
    products_ = catalog_.create_table(
        transaction_, storage::TableDef{
                          .name = "products",
                          .attributes =
                              {
                                  attribute("id", storage::int8_oid),
                                  attribute("name", storage::varchar_oid),
                                  attribute("price", storage::numeric_oid,
                                            storage::encode_typmod(price_precision, price_scale)),
                                  attribute("stock", storage::int8_oid),
                                  attribute("rating", storage::numeric_oid),
                              },
                          .primary_key = "id",
                      });
  }

  auto prepare(std::string_view text) -> SqlResult<Query> {
    return sql::prepare_query(text, catalog_, transaction_);
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  auto expect_error(std::string_view text, std::string_view sqlstate, SourcePosition position)
      -> void {
    auto query = prepare(text);
    ASSERT_FALSE(query.has_value()) << "accepted: " << text;
    EXPECT_EQ(query.error().sqlstate, sqlstate) << text << " -> " << query.error().message;
    EXPECT_EQ(query.error().position, position) << text << " -> " << query.error().message;
  }

  auto expect_ok(std::string_view text) -> void {
    auto query = prepare(text);
    EXPECT_TRUE(query.has_value()) << text << " -> " << query.error().message;
  }

  [[nodiscard]] auto catalog() -> storage::SchemaCatalog& { return catalog_; }
  [[nodiscard]] auto transaction() -> storage::Transaction& { return transaction_; }
  [[nodiscard]] auto products() const -> storage::Oid { return products_; }

  private:
  storage::SchemaCatalog catalog_;
  storage::Transaction transaction_{};
  storage::Oid products_{storage::invalid_oid};
};

/// \brief Catalog double that serves fixed relations and records every call.
class FakeCatalog final : public storage::Catalog {
  public:
  explicit FakeCatalog(std::vector<storage::RelationDesc> relations)
      : relations_{std::move(relations)} {}

  auto bootstrap() -> void override {}
  auto create_table(storage::Transaction& /*transaction*/, const storage::TableDef& /*definition*/)
      -> storage::Oid override {
    ++mutations_;
    return storage::invalid_oid;
  }
  auto drop_table(storage::Transaction& /*transaction*/, storage::Oid /*table*/) -> void override {
    ++mutations_;
  }
  auto create_index(storage::Transaction& /*transaction*/, const storage::IndexDef& /*definition*/)
      -> storage::Oid override {
    ++mutations_;
    return storage::invalid_oid;
  }
  auto lookup_relation(storage::Transaction& /*transaction*/, std::string_view name) const
      -> std::optional<storage::RelationDesc> override {
    ++relation_lookups_;
    const auto match = std::ranges::find_if(
        relations_, [name](const auto& relation) -> bool { return relation.name == name; });
    if (match == relations_.end()) {
      return std::nullopt;
    }
    return *match;
  }
  auto lookup_attribute(storage::Transaction& /*transaction*/, storage::Oid /*relation*/,
                        std::string_view /*name*/) const
      -> std::optional<storage::AttributeDesc> override {
    return std::nullopt;
  }
  auto list_relations(storage::Transaction& /*transaction*/) const
      -> std::vector<storage::RelationDesc> override {
    return relations_;
  }

  [[nodiscard]] auto relation_lookups() const -> int { return relation_lookups_; }
  [[nodiscard]] auto mutations() const -> int { return mutations_; }

  private:
  std::vector<storage::RelationDesc> relations_;
  mutable int relation_lookups_{0};
  int mutations_{0};
};

auto fake_table(std::string name, std::vector<storage::AttributeDesc> attributes)
    -> storage::RelationDesc {
  storage::RelationDesc relation{};
  relation.oid = 1;
  relation.name = std::move(name);
  relation.kind = storage::RelKind::table;
  relation.attributes = std::move(attributes);
  for (std::size_t index = 0; index < relation.attributes.size(); ++index) {
    relation.attributes.at(index).attnum = static_cast<std::int16_t>(index + 1);
  }
  return relation;
}

auto analyze_with(FakeCatalog& catalog, std::string_view text) -> SqlResult<Query> {
  storage::Transaction transaction{};
  return sql::prepare_query(text, catalog, transaction);
}

auto where_op(const Query& query) -> const OpExpr& {
  return std::get<OpExpr>(query.where->node);
}

// --- Resolution --------------------------------------------------------------------------------

TEST_F(SqlAnalyzerTest, ExpandsStarToEveryColumnInAttnumOrder) {
  auto query = prepare("SELECT * FROM products");
  ASSERT_TRUE(query.has_value()) << query.error().message;

  EXPECT_EQ(query->relation, products());
  EXPECT_EQ(query->relation_name, "products");
  EXPECT_EQ(query->where, nullptr);
  ASSERT_EQ(query->targets.size(), 5U);

  const std::vector<std::string> names{"id", "name", "price", "stock", "rating"};
  for (std::size_t index = 0; index < names.size(); ++index) {
    EXPECT_EQ(query->targets.at(index).name, names.at(index));
    EXPECT_EQ(query->targets.at(index).attnum, static_cast<std::int16_t>(index + 1));
  }
  EXPECT_EQ(query->targets.at(0).type, bigint());
  EXPECT_EQ(query->targets.at(1).type, varchar());
  EXPECT_EQ(query->targets.at(2).type, decimal(price_precision, price_scale));
  EXPECT_EQ(query->targets.at(4).type,
            (SqlType{.kind = TypeKind::decimal, .decimal = std::nullopt}));
}

TEST_F(SqlAnalyzerTest, ResolvesExplicitTargetsInSelectListOrder) {
  auto query = prepare("SELECT price, id, price FROM products");
  ASSERT_TRUE(query.has_value()) << query.error().message;
  ASSERT_EQ(query->targets.size(), 3U);

  constexpr std::int16_t price_attnum = 3;
  EXPECT_EQ(query->targets.at(0).name, "price");
  EXPECT_EQ(query->targets.at(0).attnum, price_attnum);
  EXPECT_EQ(query->targets.at(1).attnum, 1);
  EXPECT_EQ(query->targets.at(2).attnum, price_attnum);
}

TEST_F(SqlAnalyzerTest, BuildsTypedWhereTree) {
  constexpr std::string_view text = "SELECT id FROM products WHERE price >= 10.50 AND name = 'pen'";
  auto query = prepare(text);
  ASSERT_TRUE(query.has_value()) << query.error().message;
  ASSERT_NE(query->where, nullptr);

  const auto& conjunction = where_op(query.value());
  EXPECT_EQ(conjunction.op, BinaryOp::and_);
  EXPECT_EQ(query->where->type.kind, TypeKind::boolean);
  EXPECT_EQ(query->where->position, position_of(text, "AND"));

  const auto& price_check = std::get<OpExpr>(conjunction.left->node);
  EXPECT_EQ(price_check.op, BinaryOp::ge);
  const auto& price = std::get<ColumnVar>(price_check.left->node);
  EXPECT_EQ(price.attnum, 3);
  EXPECT_EQ(price_check.left->type, decimal(price_precision, price_scale));
  constexpr std::int64_t unscaled_price = 1050;
  EXPECT_EQ(std::get<storage::Decimal>(std::get<Const>(price_check.right->node).value),
            (storage::Decimal{.scale = 2, .unscaled = unscaled_price}));

  const auto& name_check = std::get<OpExpr>(conjunction.right->node);
  EXPECT_EQ(std::get<ColumnVar>(name_check.left->node).attnum, 2);
  EXPECT_EQ(std::get<std::string>(std::get<Const>(name_check.right->node).value), "pen");
}

TEST_F(SqlAnalyzerTest, RejectsUnknownRelation) {
  constexpr std::string_view text = "SELECT * FROM missing";
  expect_error(text, sqlstate::undefined_table, position_of(text, "missing"));
}

TEST_F(SqlAnalyzerTest, RejectsUnknownColumnInSelectList) {
  constexpr std::string_view text = "SELECT id, colour FROM products";
  expect_error(text, sqlstate::undefined_column, position_of(text, "colour"));
}

TEST_F(SqlAnalyzerTest, RejectsUnknownColumnInWhere) {
  constexpr std::string_view text = "SELECT id FROM products WHERE stock > 0 OR colour = 'red'";
  expect_error(text, sqlstate::undefined_column, position_of(text, "colour"));
}

TEST_F(SqlAnalyzerTest, IdentifiersAreCaseInsensitive) {
  expect_ok("SELECT ID, Price FROM PRODUCTS WHERE Stock IS NULL");
}

// --- Types -------------------------------------------------------------------------------------

TEST_F(SqlAnalyzerTest, AcceptsCompatibleComparisons) {
  expect_ok("SELECT * FROM products WHERE id = 1");
  expect_ok("SELECT * FROM products WHERE id < 1.5");
  expect_ok("SELECT * FROM products WHERE price > 1");
  expect_ok("SELECT * FROM products WHERE price <> stock");
  expect_ok("SELECT * FROM products WHERE rating >= 4.75");
  expect_ok("SELECT * FROM products WHERE name <> 'x'");
  expect_ok("SELECT * FROM products WHERE 'a' < name");
  expect_ok("SELECT * FROM products WHERE 1 = 1");
  expect_ok("SELECT * FROM products WHERE (id = 1) = (stock = 2)");
}

TEST_F(SqlAnalyzerTest, NullLiteralIsComparableWithEveryType) {
  expect_ok("SELECT * FROM products WHERE id = NULL");
  expect_ok("SELECT * FROM products WHERE NULL <> name");
  expect_ok("SELECT * FROM products WHERE price < NULL");
  expect_ok("SELECT * FROM products WHERE NULL = NULL");
  expect_ok("SELECT * FROM products WHERE NULL");
  expect_ok("SELECT * FROM products WHERE id = 1 AND NULL");
}

TEST_F(SqlAnalyzerTest, IsNullAcceptsEveryType) {
  expect_ok("SELECT * FROM products WHERE id IS NULL");
  expect_ok("SELECT * FROM products WHERE name IS NOT NULL");
  expect_ok("SELECT * FROM products WHERE price IS NULL AND rating IS NOT NULL");
  expect_ok("SELECT * FROM products WHERE (id = 1) IS NULL");
}

TEST_F(SqlAnalyzerTest, RejectsIncompatibleComparisonsAtTheOperator) {
  constexpr std::string_view int_text = "SELECT * FROM products WHERE id = 'one'";
  expect_error(int_text, sqlstate::datatype_mismatch, position_of(int_text, "="));

  constexpr std::string_view text_int = "SELECT * FROM products WHERE name > 3";
  expect_error(text_int, sqlstate::datatype_mismatch, position_of(text_int, ">"));

  constexpr std::string_view decimal_text = "SELECT * FROM products WHERE price <= name";
  expect_error(decimal_text, sqlstate::datatype_mismatch, position_of(decimal_text, "<="));

  constexpr std::string_view bool_int = "SELECT * FROM products WHERE (id = 1) = 1";
  expect_error(bool_int, sqlstate::datatype_mismatch,
               SourcePosition{position_of(bool_int, ") =").offset + 2});
}

TEST_F(SqlAnalyzerTest, RejectsNonBooleanLogicalOperands) {
  constexpr std::string_view and_text = "SELECT * FROM products WHERE stock AND id = 1";
  expect_error(and_text, sqlstate::datatype_mismatch, position_of(and_text, "stock"));

  constexpr std::string_view or_text = "SELECT * FROM products WHERE id = 1 OR 'yes'";
  expect_error(or_text, sqlstate::datatype_mismatch, position_of(or_text, "'yes'"));
}

TEST_F(SqlAnalyzerTest, RejectsNonBooleanWhere) {
  constexpr std::string_view column = "SELECT * FROM products WHERE stock";
  expect_error(column, sqlstate::datatype_mismatch, position_of(column, "stock"));

  constexpr std::string_view literal = "SELECT * FROM products WHERE 1";
  expect_error(literal, sqlstate::datatype_mismatch, position_of(literal, "1"));
}

// --- DECIMAL -----------------------------------------------------------------------------------

TEST_F(SqlAnalyzerTest, AcceptsLiteralsThatFitTheDecimalColumn) {
  expect_ok("SELECT * FROM products WHERE price < 999999.99");
  expect_ok("SELECT * FROM products WHERE price < 999999");
  expect_ok("SELECT * FROM products WHERE price > -999999.999");
  expect_ok("SELECT * FROM products WHERE price = 0.001");
  expect_ok("SELECT * FROM products WHERE rating > 123456789012345678");
}

TEST_F(SqlAnalyzerTest, RejectsLiteralsWithTooManyIntegerDigits) {
  constexpr std::string_view integer = "SELECT * FROM products WHERE price < 1000000";
  expect_error(integer, sqlstate::numeric_value_out_of_range, position_of(integer, "1000000"));

  constexpr std::string_view negative = "SELECT * FROM products WHERE price > -1000000.5";
  expect_error(negative, sqlstate::numeric_value_out_of_range, position_of(negative, "-1000000"));

  constexpr std::string_view reversed = "SELECT * FROM products WHERE 1234567.0 = price";
  expect_error(reversed, sqlstate::numeric_value_out_of_range, position_of(reversed, "1234567"));
}

TEST_F(SqlAnalyzerTest, RejectsNumericLiteralsStorageCannotRepresent) {
  constexpr std::string_view too_large = "SELECT * FROM products WHERE id = 9223372036854775808";
  expect_error(too_large, sqlstate::numeric_value_out_of_range, position_of(too_large, "9223"));

  constexpr std::string_view too_many_digits =
      "SELECT * FROM products WHERE rating = 1234567890.1234567890";
  expect_error(too_many_digits, sqlstate::numeric_value_out_of_range,
               position_of(too_many_digits, "1234"));

  constexpr std::string_view too_much_scale =
      "SELECT * FROM products WHERE rating = 0.0000000000000000001";
  expect_error(too_much_scale, sqlstate::numeric_value_out_of_range,
               position_of(too_much_scale, "0.0"));
}

TEST_F(SqlAnalyzerTest, ConvertsDecimalLiteralsExactly) {
  auto query = prepare("SELECT * FROM products WHERE rating = -0.50");
  ASSERT_TRUE(query.has_value()) << query.error().message;
  const auto& value = std::get<Const>(where_op(query.value()).right->node).value;
  constexpr std::int64_t unscaled = -50;
  EXPECT_EQ(std::get<storage::Decimal>(value),
            (storage::Decimal{.scale = 2, .unscaled = unscaled}));

  auto minimum = prepare("SELECT * FROM products WHERE id = -9223372036854775808");
  ASSERT_TRUE(minimum.has_value()) << minimum.error().message;
  EXPECT_EQ(std::get<std::int64_t>(std::get<Const>(where_op(minimum.value()).right->node).value),
            std::numeric_limits<std::int64_t>::min());

  auto boundary = prepare("SELECT * FROM products WHERE rating = -922337203685477580.8");
  ASSERT_TRUE(boundary.has_value()) << boundary.error().message;
  EXPECT_EQ(
      std::get<storage::Decimal>(std::get<Const>(where_op(boundary.value()).right->node).value),
      (storage::Decimal{.scale = 1, .unscaled = std::numeric_limits<std::int64_t>::min()}));
}

// --- Catalog edge cases ------------------------------------------------------------------------

TEST(SqlAnalyzerCatalog, UnknownColumnFailsBeforeReadingAnything) {
  FakeCatalog catalog{{fake_table("t", {attribute("a", storage::int8_oid)})}};
  auto query = analyze_with(catalog, "SELECT a FROM t WHERE b = 1");

  ASSERT_FALSE(query.has_value());
  EXPECT_EQ(query.error().sqlstate, sqlstate::undefined_column);
  EXPECT_EQ(catalog.relation_lookups(), 1);
  EXPECT_EQ(catalog.mutations(), 0);
}

TEST(SqlAnalyzerCatalog, RejectsRelationThatIsNotATable) {
  auto index = fake_table("t_pkey", {attribute("a", storage::int8_oid)});
  index.kind = storage::RelKind::index;
  FakeCatalog catalog{{index}};

  auto query = analyze_with(catalog, "SELECT * FROM t_pkey");
  ASSERT_FALSE(query.has_value());
  EXPECT_EQ(query.error().sqlstate, sqlstate::wrong_object_type);
}

TEST(SqlAnalyzerCatalog, RejectsInvalidDecimalTypmod) {
  // encode_typmod accepts precision up to 255; storage only represents 18.
  constexpr std::uint8_t too_precise = 19;
  // Raw `(2 << 8) | 5`: scale above precision, which encode_typmod refuses to build.
  constexpr std::int32_t scale_above_precision = 517;
  constexpr std::int32_t negative_typmod = -7;
  constexpr std::int32_t above_two_bytes = 0x10000;
  const std::vector<std::int32_t> typmods{
      invalid_precision_typmod, storage::encode_typmod(too_precise, 0),
      scale_above_precision,    negative_typmod,
      above_two_bytes,
  };
  for (const std::int32_t typmod : typmods) {
    FakeCatalog catalog{{fake_table("t", {attribute("d", storage::numeric_oid, typmod)})}};
    constexpr std::string_view text = "SELECT d FROM t";
    auto query = analyze_with(catalog, text);
    ASSERT_FALSE(query.has_value()) << "typmod " << typmod;
    EXPECT_EQ(query.error().sqlstate, sqlstate::invalid_parameter_value) << "typmod " << typmod;
    EXPECT_EQ(query.error().position, position_of(text, "d FROM"));
  }
}

TEST(SqlAnalyzerCatalog, AcceptsLargestValidDecimal) {
  constexpr std::uint8_t precision = 18;
  FakeCatalog catalog{{fake_table(
      "t", {attribute("d", storage::numeric_oid, storage::encode_typmod(precision, 0))})}};
  auto query = analyze_with(catalog, "SELECT d FROM t WHERE d = 999999999999999999");
  ASSERT_TRUE(query.has_value()) << query.error().message;
  EXPECT_EQ(query->targets.at(0).type, decimal(precision, 0));
}

TEST(SqlAnalyzerCatalog, ReportsUnknownTypeOidAsInternalError) {
  constexpr storage::Oid bool_type_oid = 16;
  FakeCatalog catalog{{fake_table("t", {attribute("flag", bool_type_oid)})}};
  auto query = analyze_with(catalog, "SELECT * FROM t");
  ASSERT_FALSE(query.has_value());
  EXPECT_EQ(query.error().sqlstate, sqlstate::internal_error);
}

// --- Entry points ------------------------------------------------------------------------------

TEST_F(SqlAnalyzerTest, AnalyzeRejectsStatementsOtherThanSelect) {
  const Stmt statement{DeleteStmt{
      .table = Identifier{.name = "products", .position = SourcePosition{1}},
      .where = nullptr,
  }};
  auto query = sql::analyze(statement, catalog(), transaction());
  ASSERT_FALSE(query.has_value());
  EXPECT_EQ(query.error().sqlstate, sqlstate::feature_not_supported);
}

TEST_F(SqlAnalyzerTest, PrepareQueryReturnsParserErrorsUnchanged) {
  constexpr std::string_view text = "SELECT * FROM products LIMIT 1";
  expect_error(text, sqlstate::feature_not_supported, position_of(text, "LIMIT"));
}

TEST_F(SqlAnalyzerTest, ValidatesCreateTableBeforeMutatingCatalog) {
  auto valid =
      sql::prepare_statement("CREATE TABLE invoices (id BIGINT PRIMARY KEY, total DECIMAL(10,2))",
                             catalog(), transaction());
  ASSERT_TRUE(valid.has_value()) << valid.error().message;
  const auto& command = std::get<CreateTableCommand>(valid.value());
  EXPECT_EQ(command.definition.primary_key, "id");
  EXPECT_FALSE(catalog().lookup_relation(transaction(), "invoices").has_value());

  auto varchar_pk =
      sql::prepare_statement("CREATE TABLE bad (id VARCHAR PRIMARY KEY)", catalog(), transaction());
  ASSERT_FALSE(varchar_pk.has_value());
  EXPECT_EQ(varchar_pk.error().sqlstate, sqlstate::feature_not_supported);
  EXPECT_FALSE(catalog().lookup_relation(transaction(), "bad").has_value());
}

TEST_F(SqlAnalyzerTest, RejectsInvalidCreateTableConstraints) {
  auto misplaced = sql::prepare_statement("CREATE TABLE bad (name VARCHAR, id BIGINT PRIMARY KEY)",
                                          catalog(), transaction());
  ASSERT_FALSE(misplaced.has_value());
  EXPECT_EQ(misplaced.error().sqlstate, sqlstate::feature_not_supported);

  auto duplicate = sql::prepare_statement("CREATE TABLE bad (id BIGINT PRIMARY KEY, id VARCHAR)",
                                          catalog(), transaction());
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error().sqlstate, sqlstate::duplicate_column);

  auto decimal = sql::prepare_statement(
      "CREATE TABLE bad (id BIGINT PRIMARY KEY, amount DECIMAL(2,3))", catalog(), transaction());
  ASSERT_FALSE(decimal.has_value());
  EXPECT_EQ(decimal.error().sqlstate, sqlstate::invalid_parameter_value);
}

TEST_F(SqlAnalyzerTest, AllowsIndexesOnlyOnBigintColumns) {
  auto valid = sql::prepare_statement("CREATE INDEX products_stock_idx ON products (stock)",
                                      catalog(), transaction());
  ASSERT_TRUE(valid.has_value()) << valid.error().message;
  EXPECT_EQ(std::get<CreateIndexCommand>(valid.value()).definition.column, "stock");

  auto invalid = sql::prepare_statement("CREATE INDEX products_name_idx ON products (name)",
                                        catalog(), transaction());
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().sqlstate, sqlstate::feature_not_supported);
}

// --- Relation cache ----------------------------------------------------------------------------

TEST_F(SqlAnalyzerTest, RelCacheResolvesLikeTheCatalog) {
  const storage::RelCache cache{catalog()};
  constexpr std::string_view text = "SELECT name, price FROM products WHERE stock > 0";

  auto from_catalog = prepare(text);
  auto from_cache = sql::prepare_query(text, cache);
  ASSERT_TRUE(from_catalog.has_value()) << from_catalog.error().message;
  ASSERT_TRUE(from_cache.has_value()) << from_cache.error().message;

  EXPECT_EQ(from_cache->relation, from_catalog->relation);
  ASSERT_EQ(from_cache->targets.size(), from_catalog->targets.size());
  for (std::size_t index = 0; index < from_cache->targets.size(); ++index) {
    EXPECT_EQ(from_cache->targets.at(index).attnum, from_catalog->targets.at(index).attnum);
    EXPECT_EQ(from_cache->targets.at(index).type, from_catalog->targets.at(index).type);
  }
}

TEST_F(SqlAnalyzerTest, RelCacheReportsUnknownRelationAndColumn) {
  const storage::RelCache cache{catalog()};

  constexpr std::string_view table = "SELECT * FROM missing";
  auto missing_table = sql::prepare_query(table, cache);
  ASSERT_FALSE(missing_table.has_value());
  EXPECT_EQ(missing_table.error().sqlstate, sqlstate::undefined_table);
  EXPECT_EQ(missing_table.error().position, position_of(table, "missing"));

  constexpr std::string_view column = "SELECT colour FROM products";
  auto missing_column = sql::prepare_query(column, cache);
  ASSERT_FALSE(missing_column.has_value());
  EXPECT_EQ(missing_column.error().sqlstate, sqlstate::undefined_column);
  EXPECT_EQ(missing_column.error().position, position_of(column, "colour"));
}

// --- Real catalog (FileCatalog, #44) -----------------------------------------------------------

/// \brief Bootstrapped on-disk catalog in a fresh temporary directory.
class SqlAnalyzerFileCatalog : public ::testing::Test {
  protected:
  void SetUp() override {
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("kravidb_sql_analyzer_" + std::to_string(stamp) + "_" + std::to_string(counter++));
    std::filesystem::create_directories(root_);
    catalog_ = std::make_unique<storage::FileCatalog>(root_);
    catalog_->bootstrap();
    catalog_->open();
  }

  void TearDown() override {
    catalog_.reset();
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(root_, error));
  }

  auto prepare(std::string_view text) -> SqlResult<Query> {
    return sql::prepare_query(text, *catalog_, transaction_);
  }

  [[nodiscard]] auto catalog() const -> const storage::FileCatalog& { return *catalog_; }

  private:
  std::filesystem::path root_;
  std::unique_ptr<storage::FileCatalog> catalog_;
  storage::Transaction transaction_{.xid = 1};
};

TEST_F(SqlAnalyzerFileCatalog, ResolvesBootstrappedRelation) {
  auto query = prepare("SELECT relname, relkind FROM kd_class WHERE oid >= 100 AND relname <> 'x'");
  ASSERT_TRUE(query.has_value()) << query.error().message;

  EXPECT_EQ(query->relation, storage::kd_class_oid);
  ASSERT_EQ(query->targets.size(), 2U);
  constexpr std::int16_t relkind_attnum = 4;
  EXPECT_EQ(query->targets.at(0).attnum, 2);
  EXPECT_EQ(query->targets.at(0).type, varchar());
  EXPECT_EQ(query->targets.at(1).attnum, relkind_attnum);

  const auto& conjunction = where_op(query.value());
  const auto& oid_check = std::get<OpExpr>(conjunction.left->node);
  EXPECT_EQ(std::get<ColumnVar>(oid_check.left->node).attnum, 1);
  EXPECT_EQ(oid_check.left->type, bigint());
}

TEST_F(SqlAnalyzerFileCatalog, ExpandsStarFromDisk) {
  constexpr std::size_t kd_attribute_columns = 7;
  auto query = prepare("SELECT * FROM kd_attribute");
  ASSERT_TRUE(query.has_value()) << query.error().message;
  ASSERT_EQ(query->targets.size(), kd_attribute_columns);
  EXPECT_EQ(query->targets.front().name, "attrelid");
  for (std::size_t index = 0; index < query->targets.size(); ++index) {
    EXPECT_EQ(query->targets.at(index).attnum, static_cast<std::int16_t>(index + 1));
  }
}

TEST_F(SqlAnalyzerFileCatalog, ReportsErrorsAgainstRealCatalog) {
  constexpr std::string_view table = "SELECT * FROM users";
  auto missing_table = prepare(table);
  ASSERT_FALSE(missing_table.has_value());
  EXPECT_EQ(missing_table.error().sqlstate, sqlstate::undefined_table);
  EXPECT_EQ(missing_table.error().position, position_of(table, "users"));

  constexpr std::string_view column = "SELECT typlen, typcolor FROM kd_type";
  auto missing_column = prepare(column);
  ASSERT_FALSE(missing_column.has_value());
  EXPECT_EQ(missing_column.error().sqlstate, sqlstate::undefined_column);
  EXPECT_EQ(missing_column.error().position, position_of(column, "typcolor"));

  constexpr std::string_view mismatch = "SELECT * FROM kd_type WHERE typname = 5";
  auto wrong_type = prepare(mismatch);
  ASSERT_FALSE(wrong_type.has_value());
  EXPECT_EQ(wrong_type.error().sqlstate, sqlstate::datatype_mismatch);
  EXPECT_EQ(wrong_type.error().position, position_of(mismatch, "="));
}

TEST_F(SqlAnalyzerFileCatalog, RelCacheOverRealCatalog) {
  const storage::RelCache cache{catalog()};
  auto query = sql::prepare_query("SELECT nspname FROM kd_namespace WHERE oid = 2", cache);
  ASSERT_TRUE(query.has_value()) << query.error().message;
  EXPECT_EQ(query->relation, storage::kd_namespace_oid);
  ASSERT_EQ(query->targets.size(), 1U);
  EXPECT_EQ(query->targets.at(0).attnum, 2);
}

}  // namespace
