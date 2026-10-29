module storage.catalog;

import std;
import storage.heap_tuple;
import storage.page;
import storage.page_source;
import storage.schema;

namespace kravidb::storage {

namespace {

using Fields = std::vector<std::optional<Field>>;

struct ColumnSpec {
  std::string_view name;
  Oid type_oid;
  bool not_null;
  std::int32_t typmod;
};

struct RelationSpec {
  Oid oid;
  std::string_view name;
  RelKind kind;
  std::span<const ColumnSpec> columns;
};

inline constexpr std::array<ColumnSpec, 9> kd_class_columns{{
    {.name = "oid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "relname", .type_oid = varchar_oid, .not_null = true, .typmod = -1},
    {.name = "relnamespace", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "relkind", .type_oid = varchar_oid, .not_null = true, .typmod = -1},
    {.name = "relnatts", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "relfilenode", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "relam", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "reltablespace", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "relhasindex", .type_oid = int8_oid, .not_null = false, .typmod = -1},
}};

inline constexpr std::array<ColumnSpec, 7> kd_attribute_columns{{
    {.name = "attrelid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "attname", .type_oid = varchar_oid, .not_null = true, .typmod = -1},
    {.name = "atttypid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "attnum", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "atttypmod", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "attnotnull", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "attisdropped", .type_oid = int8_oid, .not_null = false, .typmod = -1},
}};

inline constexpr std::array<ColumnSpec, 6> kd_type_columns{{
    {.name = "oid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "typname", .type_oid = varchar_oid, .not_null = true, .typmod = -1},
    {.name = "typnamespace", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "typlen", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "typbyval", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "typtype", .type_oid = varchar_oid, .not_null = false, .typmod = -1},
}};

inline constexpr std::array<ColumnSpec, 5> kd_index_columns{{
    {.name = "indexrelid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "indrelid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "indkey", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "indisunique", .type_oid = int8_oid, .not_null = false, .typmod = -1},
    {.name = "indisprimary", .type_oid = int8_oid, .not_null = false, .typmod = -1},
}};

inline constexpr std::array<ColumnSpec, 2> kd_namespace_columns{{
    {.name = "oid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "nspname", .type_oid = varchar_oid, .not_null = true, .typmod = -1},
}};

inline constexpr std::array<ColumnSpec, 3> kd_am_columns{{
    {.name = "oid", .type_oid = int8_oid, .not_null = true, .typmod = -1},
    {.name = "amname", .type_oid = varchar_oid, .not_null = true, .typmod = -1},
    {.name = "amhandler", .type_oid = varchar_oid, .not_null = false, .typmod = -1},
}};

inline constexpr std::size_t class_filenode_index = 5;
inline constexpr std::size_t class_am_index = 6;
inline constexpr std::size_t attribute_not_null_index = 5;
inline constexpr std::size_t attribute_dropped_index = 6;
inline constexpr std::int64_t int8_length = 8;

inline constexpr std::array<RelationSpec, 6> catalog_relations{{
    {.oid = kd_class_oid,
     .name = "kd_class",
     .kind = RelKind::table,
     .columns = std::span<const ColumnSpec>{kd_class_columns}},
    {.oid = kd_attribute_oid,
     .name = "kd_attribute",
     .kind = RelKind::table,
     .columns = std::span<const ColumnSpec>{kd_attribute_columns}},
    {.oid = kd_type_oid,
     .name = "kd_type",
     .kind = RelKind::table,
     .columns = std::span<const ColumnSpec>{kd_type_columns}},
    {.oid = kd_index_oid,
     .name = "kd_index",
     .kind = RelKind::table,
     .columns = std::span<const ColumnSpec>{kd_index_columns}},
    {.oid = kd_namespace_oid,
     .name = "kd_namespace",
     .kind = RelKind::table,
     .columns = std::span<const ColumnSpec>{kd_namespace_columns}},
    {.oid = kd_am_oid,
     .name = "kd_am",
     .kind = RelKind::table,
     .columns = std::span<const ColumnSpec>{kd_am_columns}},
}};

struct Row {
  Fields fields;
  BlockNumber block{0};
  std::uint16_t slot{0};
};

/// \brief Reads one column of a catalog row and reports its location on error.
class RowReader {
  public:
  RowReader(const std::filesystem::path& root, const RelationSpec& relation, const Row& row)
      : root_{root}, relation_{&relation}, row_{&row} {}

  auto expect_columns(std::size_t expected) const -> void {
    if (row_->fields.size() != expected) {
      throw corrupt("unexpected column count");
    }
  }

  [[nodiscard]] auto integer_at(std::size_t index) const -> std::int64_t {
    if (index >= row_->fields.size()) {
      throw corrupt("column index out of range");
    }
    const auto& field = row_->fields.at(index);
    if (!field.has_value()) {
      throw corrupt("unexpected null column");
    }
    const auto* value = std::get_if<std::int64_t>(&*field);
    if (value == nullptr) {
      throw corrupt("expected an integer column");
    }
    return *value;
  }

  [[nodiscard]] auto oid_at(std::size_t index) const -> Oid {
    const auto value = integer_at(index);
    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<Oid>::max())) {
      throw corrupt("OID out of range");
    }
    return static_cast<Oid>(value);
  }

  [[nodiscard]] auto int16_at(std::size_t index) const -> std::int16_t {
    const auto value = integer_at(index);
    if (value < static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max())) {
      throw corrupt("int16 out of range");
    }
    return static_cast<std::int16_t>(value);
  }

  [[nodiscard]] auto int32_at(std::size_t index) const -> std::int32_t {
    const auto value = integer_at(index);
    if (value < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
      throw corrupt("int32 out of range");
    }
    return static_cast<std::int32_t>(value);
  }

  [[nodiscard]] auto text_at(std::size_t index) const -> std::string {
    if (index >= row_->fields.size()) {
      throw corrupt("column index out of range");
    }
    const auto& field = row_->fields.at(index);
    if (!field.has_value()) {
      throw corrupt("unexpected null column");
    }
    const auto* value = std::get_if<std::string>(&*field);
    if (value == nullptr) {
      throw corrupt("expected a text column");
    }
    return *value;
  }

  private:
  [[nodiscard]] auto corrupt(std::string_view reason) const -> SqlStateError {
    return SqlStateError{"XX001", "kd_catalog '" + root_.string() + "' relation '" +
                                      std::string{relation_->name} + "' block " +
                                      std::to_string(row_->block) + " slot " +
                                      std::to_string(row_->slot) + ": " + std::string{reason}};
  }

  std::filesystem::path root_;
  const RelationSpec* relation_;
  const Row* row_;
};

[[nodiscard]] auto corrupt_error(const std::filesystem::path& root, const RelationSpec& relation,
                                 BlockNumber block, std::uint16_t slot, std::string_view reason)
    -> SqlStateError {
  return SqlStateError{"XX001", "kd_catalog '" + root.string() + "' relation '" +
                                    std::string{relation.name} + "' block " +
                                    std::to_string(block) + " slot " + std::to_string(slot) + ": " +
                                    std::string{reason}};
}

[[nodiscard]] auto missing_error(const std::filesystem::path& root, const RelationSpec& relation)
    -> SqlStateError {
  return SqlStateError{"XX001", "kd_catalog '" + root.string() + "': required relation '" +
                                    std::string{relation.name} + "' is missing"};
}

[[nodiscard]] auto int_field(std::int64_t value) -> std::optional<Field> {
  return Field{value};
}

[[nodiscard]] auto text_field(std::string value) -> std::optional<Field> {
  return Field{std::move(value)};
}

[[nodiscard]] auto class_row(const RelationSpec& relation) -> Fields {
  return {int_field(relation.oid),
          text_field(std::string{relation.name}),
          int_field(kd_catalog_namespace_oid),
          text_field(std::string(1, static_cast<char>(relation.kind))),
          int_field(static_cast<std::int64_t>(relation.columns.size())),
          int_field(relation.oid),
          int_field(0),
          int_field(0),
          int_field(0)};
}

[[nodiscard]] auto attribute_row(const RelationSpec& relation, const ColumnSpec& column,
                                 std::size_t index) -> Fields {
  return {int_field(relation.oid),
          text_field(std::string{column.name}),
          int_field(column.type_oid),
          int_field(static_cast<std::int64_t>(index) + 1),
          int_field(column.typmod),
          int_field(column.not_null ? 1 : 0),
          int_field(0)};
}

[[nodiscard]] auto bootstrap_rows(Oid oid) -> std::vector<Fields> {
  std::vector<Fields> rows;
  if (oid == kd_class_oid) {
    rows.reserve(std::size(catalog_relations));
    for (const auto& relation : catalog_relations) {
      rows.push_back(class_row(relation));
    }
  } else if (oid == kd_attribute_oid) {
    for (const auto& relation : catalog_relations) {
      for (std::size_t index = 0; index < relation.columns.size(); ++index) {
        rows.push_back(attribute_row(
            relation, *(relation.columns.begin() + static_cast<std::ptrdiff_t>(index)), index));
      }
    }
  } else if (oid == kd_type_oid) {
    rows.push_back({int_field(int8_oid), text_field("int8"), int_field(kd_catalog_namespace_oid),
                    int_field(int8_length), int_field(1), text_field("b")});
    rows.push_back({int_field(varchar_oid), text_field("varchar"),
                    int_field(kd_catalog_namespace_oid), int_field(-1), int_field(0),
                    text_field("b")});
    rows.push_back({int_field(numeric_oid), text_field("numeric"),
                    int_field(kd_catalog_namespace_oid), int_field(-1), int_field(0),
                    text_field("b")});
  } else if (oid == kd_namespace_oid) {
    rows.push_back({int_field(kd_catalog_namespace_oid), text_field("kd_catalog")});
    rows.push_back({int_field(public_namespace_oid), text_field("public")});
  } else if (oid == kd_am_oid) {
    rows.push_back({int_field(nbtree_oid), text_field("nbtree"), text_field("nbtree_handler")});
  }
  return rows;
}

[[nodiscard]] auto build_heap_tuple(const Fields& fields, BlockNumber block, std::uint16_t slot)
    -> HeapTuple {
  HeapTupleHeader header{};
  header.t_xmin = 1;
  header.t_ctid = ItemPointerData{.block = block, .offset = slot};
  return HeapTuple::from_fields(std::span<const std::optional<Field>>{fields}, header);
}

auto place_tuple(Page& page, const HeapTuple& tuple) -> void {
  auto header = page.header();
  const auto length = static_cast<std::uint16_t>(tuple.size());
  if (length > header.pd_upper || length == 0) {
    throw std::logic_error{"catalog tuple does not fit in the page"};
  }
  const auto offset = static_cast<std::uint16_t>(header.pd_upper - length);
  const auto destination = page.mutable_bytes().subspan(offset, tuple.size());
  std::ranges::copy(tuple.bytes(), destination.begin());
  header.pd_upper = offset;
  page.set_header(header);
  ItemId item;
  item.set_offset(offset);
  item.set_length(length);
  item.set_flags(ItemIdFlag::normal);
  static_cast<void>(page.add_item(item));
}

auto write_relation(DirectoryPageSource& source, const RelationSpec& relation,
                    const std::vector<Fields>& rows) -> void {
  const RelFileNode node{.tablespace = 0, .database = 1, .relation = relation.oid};
  if (rows.empty()) {
    if (source.block_count(node, Fork::main) == 0) {
      Page page;
      page.set_checksum();
      source.write(node, Fork::main, 0, page.bytes());
      source.fsync(node, Fork::main);
    }
    return;
  }
  Page page;
  BlockNumber block = 0;
  for (const auto& fields : rows) {
    auto tuple = build_heap_tuple(fields, block, page.item_count());
    if (page.free_space() < tuple.size() + sizeof(ItemId)) {
      if (page.item_count() == 0) {
        throw SqlStateError{"54000", "catalog row does not fit in a page"};
      }
      page.set_checksum();
      source.write(node, Fork::main, block, page.bytes());
      ++block;
      page = Page{};
      tuple = build_heap_tuple(fields, block, 0);
      if (page.free_space() < tuple.size() + sizeof(ItemId)) {
        throw SqlStateError{"54000", "catalog row does not fit in a page"};
      }
    }
    place_tuple(page, tuple);
  }
  page.set_checksum();
  source.write(node, Fork::main, block, page.bytes());
  source.fsync(node, Fork::main);
}

[[nodiscard]] auto read_relation(PageSource& source, const std::filesystem::path& root,
                                 const RelationSpec& relation, bool allow_empty)
    -> std::vector<Row> {
  const RelFileNode node{.tablespace = 0, .database = 1, .relation = relation.oid};
  const auto blocks = source.block_count(node, Fork::main);
  if (blocks == 0) {
    if (allow_empty) {
      return {};
    }
    throw missing_error(root, relation);
  }

  std::vector<Row> rows;
  for (BlockNumber block = 0; block < blocks; ++block) {
    std::array<std::byte, physical_page_size> buffer{};
    source.read(node, Fork::main, block, buffer);
    const auto page = Page::from_bytes(buffer);
    if (!page.validate()) {
      throw corrupt_error(root, relation, block, 0, "invalid page");
    }
    const auto header = page.header();
    const auto count = page.item_count();
    for (std::uint16_t slot = 0; slot < count; ++slot) {
      const auto item = page.item(slot);
      if (item.flags() == ItemIdFlag::unused) {
        continue;
      }
      if (item.flags() != ItemIdFlag::normal) {
        throw corrupt_error(root, relation, block, slot, "unsupported item id");
      }
      const auto offset = item.offset();
      const auto length = item.length();
      if (length == 0 || offset < header.pd_upper ||
          static_cast<std::size_t>(offset) + length > Page::size) {
        throw corrupt_error(root, relation, block, slot, "tuple outside the page");
      }
      const auto bytes = std::span<const std::byte>{buffer}.subspan(offset, length);
      const auto tuple = HeapTuple::from_bytes(bytes);
      if (!tuple.has_value()) {
        throw corrupt_error(root, relation, block, slot, "malformed heap tuple");
      }
      Row row{};
      row.fields = tuple->fields();
      row.block = block;
      row.slot = slot;
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

[[nodiscard]] auto parse_class(const std::filesystem::path& root, const RelationSpec& relation,
                               const Row& row) -> RelationDesc {
  const RowReader reader{root, relation, row};
  reader.expect_columns(relation.columns.size());
  RelationDesc description{};
  description.oid = reader.oid_at(0);
  description.name = reader.text_at(1);
  const auto kind = reader.text_at(3);
  if (kind.size() != 1) {
    throw corrupt_error(root, relation, row.block, row.slot, "invalid relkind");
  }
  description.kind = static_cast<RelKind>(kind.front());
  if (description.kind != RelKind::table && description.kind != RelKind::index) {
    throw corrupt_error(root, relation, row.block, row.slot, "invalid relkind");
  }
  description.relfilenode =
      RelFileNode{.tablespace = 0, .database = 1, .relation = reader.oid_at(class_filenode_index)};
  description.relam = reader.oid_at(class_am_index);
  return description;
}

[[nodiscard]] auto parse_attribute(const std::filesystem::path& root, const RelationSpec& relation,
                                   const Row& row) -> std::optional<AttributeDesc> {
  const RowReader reader{root, relation, row};
  reader.expect_columns(relation.columns.size());
  if (reader.integer_at(attribute_dropped_index) != 0) {
    return std::nullopt;
  }
  AttributeDesc attribute{};
  attribute.name = reader.text_at(1);
  attribute.type_oid = reader.oid_at(2);
  attribute.attnum = reader.int16_at(3);
  attribute.typmod = reader.int32_at(4);
  attribute.not_null = reader.integer_at(attribute_not_null_index) != 0;
  if (attribute.attnum <= 0) {
    throw corrupt_error(root, relation, row.block, row.slot, "invalid attnum");
  }
  return attribute;
}

[[nodiscard]] auto parse_index(const std::filesystem::path& root, const RelationSpec& relation,
                               const Row& row) -> std::pair<Oid, std::pair<Oid, std::int16_t>> {
  const RowReader reader{root, relation, row};
  reader.expect_columns(relation.columns.size());
  const auto index_oid = reader.oid_at(0);
  const auto relation_oid = reader.oid_at(1);
  const auto attnum = reader.int16_at(2);
  if (attnum <= 0) {
    throw corrupt_error(root, relation, row.block, row.slot, "invalid indkey");
  }
  return {index_oid, {relation_oid, attnum}};
}

[[nodiscard]] auto field_type_for(Oid type_oid) -> FieldType {
  switch (type_oid) {
    case int8_oid:
      return FieldType::Int;
    case varchar_oid:
      return FieldType::Varchar;
    case numeric_oid:
      return FieldType::Decimal;
    default:
      throw SqlStateError{"42704", "unknown type OID " + std::to_string(type_oid)};
  }
}

}  // namespace

class FileCatalog::Impl {
  public:
  explicit Impl(std::filesystem::path root) : root_{std::move(root)}, source_{root_} {}

  auto bootstrap() -> void {
    if (!std::filesystem::exists(root_)) {
      throw std::runtime_error{"kd_catalog: data directory '" + root_.string() +
                               "' does not exist"};
    }
    if (!std::filesystem::is_directory(root_)) {
      throw std::runtime_error{"kd_catalog: '" + root_.string() + "' is not a directory"};
    }
    for (const auto& relation : catalog_relations) {
      const RelFileNode node{.tablespace = 0, .database = 1, .relation = relation.oid};
      if (source_.block_count(node, Fork::main) != 0) {
        static_cast<void>(read_relation(source_, root_, relation, true));
        continue;
      }
      write_relation(source_, relation, bootstrap_rows(relation.oid));
    }
    source_.fsync_all();
  }

  auto open() -> void {
    std::map<Oid, RelationDesc> by_oid;
    std::map<Oid, std::int16_t> declared_attributes;
    std::set<Oid> type_oids;
    std::set<Oid> namespace_oids;
    bool has_nbtree = false;

    for (const auto& relation : catalog_relations) {
      const auto rows = read_relation(source_, root_, relation, relation.oid == kd_index_oid);
      if (relation.oid == kd_class_oid) {
        for (const auto& row : rows) {
          auto description = parse_class(root_, relation, row);
          const RowReader reader{root_, relation, row};
          const auto [entry, inserted] =
              by_oid.try_emplace(description.oid, std::move(description));
          if (!inserted) {
            throw corrupt_error(root_, relation, row.block, row.slot, "duplicate relation OID");
          }
          declared_attributes.try_emplace(entry->first, reader.int16_at(4));
        }
      } else if (relation.oid == kd_attribute_oid) {
        for (const auto& row : rows) {
          auto attribute = parse_attribute(root_, relation, row);
          if (!attribute.has_value()) {
            continue;
          }
          const RowReader reader{root_, relation, row};
          const auto relation_oid = reader.oid_at(0);
          auto target = by_oid.find(relation_oid);
          if (target == by_oid.end()) {
            throw corrupt_error(root_, relation, row.block, row.slot,
                                "attribute references an unknown relation");
          }
          target->second.attributes.push_back(std::move(*attribute));
        }
      } else if (relation.oid == kd_type_oid) {
        for (const auto& row : rows) {
          const RowReader reader{root_, relation, row};
          reader.expect_columns(relation.columns.size());
          type_oids.insert(reader.oid_at(0));
        }
      } else if (relation.oid == kd_index_oid) {
        for (const auto& row : rows) {
          const auto [index_oid, target] = parse_index(root_, relation, row);
          auto found = by_oid.find(target.first);
          if (found == by_oid.end()) {
            throw corrupt_error(root_, relation, row.block, row.slot,
                                "index references an unknown relation");
          }
          found->second.indexes.push_back(index_oid);
          const RowReader reader{root_, relation, row};
          if (reader.integer_at(4) != 0) {
            found->second.primary_index = index_oid;
            bool marked = false;
            for (auto& attribute : found->second.attributes) {
              if (attribute.attnum == target.second) {
                attribute.primary_key = true;
                marked = true;
                break;
              }
            }
            if (!marked) {
              throw corrupt_error(root_, relation, row.block, row.slot,
                                  "primary index references an unknown attribute");
            }
          }
        }
      } else if (relation.oid == kd_namespace_oid) {
        for (const auto& row : rows) {
          const RowReader reader{root_, relation, row};
          reader.expect_columns(relation.columns.size());
          namespace_oids.insert(reader.oid_at(0));
        }
      } else if (relation.oid == kd_am_oid) {
        for (const auto& row : rows) {
          const RowReader reader{root_, relation, row};
          reader.expect_columns(relation.columns.size());
          if (reader.oid_at(0) == nbtree_oid) {
            has_nbtree = true;
          }
        }
      }
    }

    for (const auto& relation : catalog_relations) {
      const auto found = by_oid.find(relation.oid);
      if (found == by_oid.end()) {
        throw missing_error(root_, relation);
      }
      if (found->second.name != relation.name) {
        throw SqlStateError{"XX001", "kd_catalog '" + root_.string() + "': relation " +
                                         std::to_string(relation.oid) + " is named '" +
                                         found->second.name + "', expected '" +
                                         std::string{relation.name} + "'"};
      }
      const auto declared = declared_attributes.find(relation.oid);
      if (declared == declared_attributes.end() ||
          static_cast<std::size_t>(declared->second) != found->second.attributes.size()) {
        throw SqlStateError{"XX001", "kd_catalog '" + root_.string() + "': relation '" +
                                         std::string{relation.name} +
                                         "' has an inconsistent attribute count"};
      }
    }

    if (!type_oids.contains(int8_oid) || !type_oids.contains(varchar_oid) ||
        !type_oids.contains(numeric_oid)) {
      throw SqlStateError{
          "XX001", "kd_catalog '" + root_.string() + "': base types are missing from kd_type"};
    }
    if (!namespace_oids.contains(kd_catalog_namespace_oid) ||
        !namespace_oids.contains(public_namespace_oid)) {
      throw SqlStateError{
          "XX001", "kd_catalog '" + root_.string() + "': namespaces are missing from kd_namespace"};
    }
    if (!has_nbtree) {
      throw SqlStateError{"XX001",
                          "kd_catalog '" + root_.string() + "': nbtree is missing from kd_am"};
    }

    relations_.clear();
    for (auto& [oid, description] : by_oid) {
      std::ranges::sort(description.attributes, {}, &AttributeDesc::attnum);
      for (std::size_t index = 0; index < description.attributes.size(); ++index) {
        const auto& attribute = description.attributes.at(index);
        if (attribute.attnum != static_cast<std::int16_t>(index + 1)) {
          throw SqlStateError{"XX001", "kd_catalog '" + root_.string() + "': relation '" +
                                           description.name + "' has a non-consecutive attnum"};
        }
        static_cast<void>(field_type_for(attribute.type_oid));
      }
      relations_.push_back(std::move(description));
    }
    opened_ = true;
  }

  [[nodiscard]] auto lookup_relation(std::string_view name) const -> std::optional<RelationDesc> {
    require_open();
    const auto found = std::ranges::find_if(
        relations_,
        [name](const RelationDesc& candidate) -> bool { return candidate.name == name; });
    if (found == relations_.end()) {
      return std::nullopt;
    }
    return *found;
  }

  [[nodiscard]] auto lookup_attribute(Oid relation, std::string_view name) const
      -> std::optional<AttributeDesc> {
    require_open();
    const auto found = std::ranges::find_if(
        relations_,
        [relation](const RelationDesc& candidate) -> bool { return candidate.oid == relation; });
    if (found == relations_.end()) {
      return std::nullopt;
    }
    const auto attribute = std::ranges::find_if(
        found->attributes,
        [name](const AttributeDesc& candidate) -> bool { return candidate.name == name; });
    if (attribute == found->attributes.end()) {
      return std::nullopt;
    }
    return *attribute;
  }

  [[nodiscard]] auto relations() const -> std::vector<RelationDesc> {
    require_open();
    return relations_;
  }

  private:
  auto require_open() const -> void {
    if (!opened_) {
      throw std::logic_error{"kd_catalog: open() has not been called"};
    }
  }

  std::filesystem::path root_;
  DirectoryPageSource source_;
  std::vector<RelationDesc> relations_;
  bool opened_{false};
};

FileCatalog::FileCatalog(std::filesystem::path root)
    : impl_{std::make_unique<Impl>(std::move(root))} {}

FileCatalog::~FileCatalog() = default;

auto FileCatalog::bootstrap() -> void {
  impl_->bootstrap();
}

auto FileCatalog::open() -> void {
  impl_->open();
}

auto FileCatalog::create_table(Transaction& transaction, const TableDef& definition) -> Oid {
  static_cast<void>(transaction);
  static_cast<void>(definition);
  throw SqlStateError{"0A000", "catalog is read-only"};
}

auto FileCatalog::drop_table(Transaction& transaction, Oid table) -> void {
  static_cast<void>(transaction);
  static_cast<void>(table);
  throw SqlStateError{"0A000", "catalog is read-only"};
}

auto FileCatalog::create_index(Transaction& transaction, const IndexDef& definition) -> Oid {
  static_cast<void>(transaction);
  static_cast<void>(definition);
  throw SqlStateError{"0A000", "catalog is read-only"};
}

auto FileCatalog::lookup_relation(Transaction& transaction, std::string_view name) const
    -> std::optional<RelationDesc> {
  static_cast<void>(transaction);
  return impl_->lookup_relation(name);
}

auto FileCatalog::lookup_attribute(Transaction& transaction, Oid relation,
                                   std::string_view name) const -> std::optional<AttributeDesc> {
  static_cast<void>(transaction);
  return impl_->lookup_attribute(relation, name);
}

auto FileCatalog::list_relations(Transaction& transaction) const -> std::vector<RelationDesc> {
  static_cast<void>(transaction);
  return impl_->relations();
}

RelCache::RelCache(const Catalog& catalog) {
  Transaction transaction{.xid = 1};
  for (auto& description : catalog.list_relations(transaction)) {
    auto shared = std::make_shared<const RelationDesc>(std::move(description));
    relations_.push_back(shared);
    by_oid_.try_emplace(shared->oid, shared);
    by_name_.try_emplace(shared->name, shared);
    std::vector<ColumnDef> columns;
    columns.reserve(shared->attributes.size());
    for (const auto& attribute : shared->attributes) {
      columns.push_back(ColumnDef{.name = attribute.name,
                                  .type = field_type_for(attribute.type_oid),
                                  .is_primary_key = attribute.primary_key});
    }
    schemas_.try_emplace(shared->oid, std::make_shared<const Schema>(std::move(columns)));
  }
  std::ranges::sort(relations_, {}, &RelationDesc::oid);
}

auto RelCache::relation(Oid oid) const -> std::shared_ptr<const RelationDesc> {
  const auto found = by_oid_.find(oid);
  return found == by_oid_.end() ? nullptr : found->second;
}

auto RelCache::relation(std::string_view name) const -> std::shared_ptr<const RelationDesc> {
  const auto found = by_name_.find(std::string{name});
  return found == by_name_.end() ? nullptr : found->second;
}

auto RelCache::schema_of(Oid oid) const -> std::shared_ptr<const Schema> {
  const auto found = schemas_.find(oid);
  return found == schemas_.end() ? nullptr : found->second;
}

auto RelCache::require_column(Oid relation_oid, std::string_view name) const -> AttributeDesc {
  const auto found = by_oid_.find(relation_oid);
  if (found == by_oid_.end()) {
    throw SqlStateError{"42P01", "relation " + std::to_string(relation_oid) + " does not exist"};
  }
  for (const auto& attribute : found->second->attributes) {
    if (attribute.name == name) {
      return attribute;
    }
  }
  throw SqlStateError{"42703", "column '" + std::string{name} + "' does not exist in relation '" +
                                   found->second->name + "'"};
}

auto RelCache::relations() const -> std::vector<std::shared_ptr<const RelationDesc>> {
  return relations_;
}

}  // namespace kravidb::storage
