/// \file
/// \brief Relation catalog contract and its in-memory adapter.

export module storage.catalog;

import std;
export import storage.heap_tuple;
export import storage.relation_file;
export import storage.schema;
export import storage.transaction;

export namespace kravidb::storage {

/// \brief Object identifier.
using Oid = std::uint32_t;

/// \brief OID that names no object.
inline constexpr Oid invalid_oid = 0;

/// \brief OID of the `kd_catalog` namespace.
inline constexpr Oid kd_catalog_namespace_oid = 1;

/// \brief OID of the `public` namespace.
inline constexpr Oid public_namespace_oid = 2;

/// \brief OID of the `int8` base type.
inline constexpr Oid int8_oid = 10;

/// \brief OID of the `varchar` base type.
inline constexpr Oid varchar_oid = 11;

/// \brief OID of the `numeric` base type.
inline constexpr Oid numeric_oid = 12;

/// \brief OID of the `nbtree` access method.
inline constexpr Oid nbtree_oid = 20;

/// \brief First OID available to user relations.
inline constexpr Oid first_user_oid = 16384;

/// \brief OID of `kd_class`.
inline constexpr Oid kd_class_oid = 100;
/// \brief OID of `kd_attribute`.
inline constexpr Oid kd_attribute_oid = 101;
/// \brief OID of `kd_type`.
inline constexpr Oid kd_type_oid = 102;
/// \brief OID of `kd_index`.
inline constexpr Oid kd_index_oid = 103;
/// \brief OID of `kd_namespace`.
inline constexpr Oid kd_namespace_oid = 104;
/// \brief OID of `kd_am`.
inline constexpr Oid kd_am_oid = 105;

/// \brief Precision and scale decoded from a `numeric` type modifier.
struct Typmod {
  std::uint8_t precision{0};
  std::uint8_t scale{0};

  /// \brief Compares two modifiers field by field.
  [[nodiscard]] auto operator==(const Typmod&) const -> bool = default;
};

/// \brief Largest value accepted for the precision or scale of a typmod.
inline constexpr std::uint32_t typmod_component_max = 255;

/// \brief Shift that places the precision in the high byte of a typmod.
inline constexpr std::uint32_t typmod_precision_shift = 8;

/// \brief Mask of the two bytes used by a typmod.
inline constexpr std::uint32_t typmod_value_max = 0xFFFF;

/// \brief Encodes `numeric` precision and scale as `(precision << 8) | scale`.
/// \throws std::invalid_argument when the precision is zero, exceeds 255 or the
/// scale exceeds the precision.
[[nodiscard]] constexpr auto encode_typmod(std::uint32_t precision, std::uint32_t scale)
    -> std::int32_t {
  if (precision == 0 || precision > typmod_component_max || scale > precision) {
    throw std::invalid_argument{"invalid numeric type modifier"};
  }
  return static_cast<std::int32_t>((precision << typmod_precision_shift) | scale);
}

/// \brief Decodes a `numeric` type modifier, or `std::nullopt` when it is `-1`
/// or malformed.
[[nodiscard]] constexpr auto decode_typmod(std::int32_t typmod) -> std::optional<Typmod> {
  if (typmod < 0) {
    return std::nullopt;
  }
  const auto bits = static_cast<std::uint32_t>(typmod);
  if (bits > typmod_value_max) {
    return std::nullopt;
  }
  const auto precision =
      static_cast<std::uint8_t>((bits >> typmod_precision_shift) & typmod_component_max);
  const auto scale = static_cast<std::uint8_t>(bits & typmod_component_max);
  if (precision == 0 || scale > precision) {
    return std::nullopt;
  }
  return Typmod{.precision = precision, .scale = scale};
}

/// \brief Kind of relation stored in the catalog.
enum class RelKind : char {
  /// \brief Heap relation with rows.
  table = 'r',
  /// \brief Index relation.
  index = 'i',
};

/// \brief One attribute (column) of a relation.
struct AttributeDesc {
  std::string name;
  Oid type_oid{invalid_oid};
  std::int16_t attnum{0};
  bool not_null{false};
  std::int32_t typmod{-1};
  /// \brief True when the attribute is covered by the primary index.
  bool primary_key{false};
};

/// \brief Definition of a relation as seen by the catalog.
struct RelationDesc {
  Oid oid{invalid_oid};
  std::string name;
  RelKind kind{RelKind::table};
  Oid relam{invalid_oid};
  RelFileNode relfilenode{};
  std::vector<AttributeDesc> attributes;
  Oid primary_index{invalid_oid};
  std::vector<Oid> indexes;
};

/// \brief Definition accepted by `Catalog::create_table`.
struct TableDef {
  std::string name;
  std::vector<AttributeDesc> attributes;
  /// \brief Name of the primary-key column.
  std::string primary_key;
};

/// \brief Definition accepted by `Catalog::create_index`.
struct IndexDef {
  /// \brief Relation the index belongs to.
  Oid relation{invalid_oid};
  std::string name;
  std::string column;
  bool unique{false};
};

/// \brief Contract of the relation catalog.
///
/// The catalog replaces the schema as the source of truth for relation and
/// attribute metadata. Every operation runs inside a statement transaction.
class Catalog {
  public:
  Catalog() = default;
  Catalog(const Catalog&) = delete;
  auto operator=(const Catalog&) -> Catalog& = delete;
  Catalog(Catalog&&) = delete;
  auto operator=(Catalog&&) -> Catalog& = delete;
  virtual ~Catalog() = default;

  /// \brief Populates the rows the engine needs to start.
  virtual auto bootstrap() -> void = 0;

  /// \brief Creates `definition` and returns its OID.
  virtual auto create_table(Transaction& transaction, const TableDef& definition) -> Oid = 0;

  /// \brief Drops the table named by `table`.
  virtual auto drop_table(Transaction& transaction, Oid table) -> void = 0;

  /// \brief Creates `definition` and returns its OID.
  virtual auto create_index(Transaction& transaction, const IndexDef& definition) -> Oid = 0;

  /// \brief Relation named `name`, if it exists.
  [[nodiscard]] virtual auto lookup_relation(Transaction& transaction, std::string_view name) const
      -> std::optional<RelationDesc> = 0;

  /// \brief Attribute `name` of relation `relation`, if both exist.
  [[nodiscard]] virtual auto lookup_attribute(Transaction& transaction, Oid relation,
                                              std::string_view name) const
      -> std::optional<AttributeDesc> = 0;

  /// \brief Every relation known to the catalog.
  [[nodiscard]] virtual auto list_relations(Transaction& transaction) const
      -> std::vector<RelationDesc> = 0;
};

/// \brief Catalog adapter that keeps relations in memory.
///
/// It assigns OIDs sequentially from 1 and stamps attribute numbers in
/// definition order. It is a test double; it writes nothing and survives no
/// restart.
class SchemaCatalog final : public Catalog {
  public:
  auto bootstrap() -> void override {}
  auto create_table(Transaction& transaction, const TableDef& definition) -> Oid override;
  auto drop_table(Transaction& transaction, Oid table) -> void override;
  auto create_index(Transaction& transaction, const IndexDef& definition) -> Oid override;
  [[nodiscard]] auto lookup_relation(Transaction& transaction, std::string_view name) const
      -> std::optional<RelationDesc> override;
  [[nodiscard]] auto lookup_attribute(Transaction& transaction, Oid relation,
                                      std::string_view name) const
      -> std::optional<AttributeDesc> override;
  [[nodiscard]] auto list_relations(Transaction& transaction) const
      -> std::vector<RelationDesc> override;

  private:
  Oid next_oid_{invalid_oid};
  std::vector<RelationDesc> relations_;
};

inline auto SchemaCatalog::create_table(Transaction& transaction, const TableDef& definition)
    -> Oid {
  const Oid oid = ++next_oid_;
  RelationDesc relation{};
  relation.oid = oid;
  relation.name = definition.name;
  relation.attributes = definition.attributes;
  for (std::size_t index = 0; index < relation.attributes.size(); ++index) {
    auto& attribute = relation.attributes.at(index);
    attribute.attnum = static_cast<std::int16_t>(index + 1);
    attribute.primary_key = attribute.name == definition.primary_key;
  }
  relations_.push_back(std::move(relation));
  return oid;
}

inline auto SchemaCatalog::drop_table(Transaction& transaction, Oid table) -> void {
  std::erase_if(relations_,
                [table](const RelationDesc& relation) -> bool { return relation.oid == table; });
}

inline auto SchemaCatalog::create_index(Transaction& transaction, const IndexDef& definition)
    -> Oid {
  const auto relation =
      std::ranges::find_if(relations_, [&definition](const RelationDesc& candidate) -> bool {
        return candidate.oid == definition.relation;
      });
  if (relation == relations_.end()) {
    throw std::invalid_argument{"index relation does not exist"};
  }
  const Oid oid = ++next_oid_;
  relation->indexes.push_back(oid);
  return oid;
}

inline auto SchemaCatalog::lookup_relation(Transaction& transaction, std::string_view name) const
    -> std::optional<RelationDesc> {
  const auto relation = std::ranges::find_if(
      relations_, [name](const RelationDesc& candidate) -> bool { return candidate.name == name; });
  if (relation == relations_.end()) {
    return std::nullopt;
  }
  return *relation;
}

inline auto SchemaCatalog::lookup_attribute(Transaction& transaction, Oid relation,
                                            std::string_view name) const
    -> std::optional<AttributeDesc> {
  const auto found_relation = std::ranges::find_if(
      relations_,
      [relation](const RelationDesc& candidate) -> bool { return candidate.oid == relation; });
  if (found_relation == relations_.end()) {
    return std::nullopt;
  }
  const auto attribute = std::ranges::find_if(
      found_relation->attributes,
      [name](const AttributeDesc& candidate) -> bool { return candidate.name == name; });
  if (attribute == found_relation->attributes.end()) {
    return std::nullopt;
  }
  return *attribute;
}

inline auto SchemaCatalog::list_relations(Transaction& transaction) const
    -> std::vector<RelationDesc> {
  return relations_;
}

/// \brief Read-only catalog backed by one heap file per catalog relation.
///
/// `bootstrap()` creates and validates the six catalog relations under `root`;
/// `open()` loads them into memory. Every write operation throws
/// `SqlStateError` with SQLSTATE `0A000`.
class FileCatalog final : public Catalog {
  public:
  /// \brief Opens the catalog rooted at `root`; call `open()` or `bootstrap()`.
  explicit FileCatalog(std::filesystem::path root);
  ~FileCatalog() override;

  FileCatalog(const FileCatalog&) = delete;
  auto operator=(const FileCatalog&) -> FileCatalog& = delete;
  FileCatalog(FileCatalog&&) = delete;
  auto operator=(FileCatalog&&) -> FileCatalog& = delete;

  auto bootstrap() -> void override;
  /// \brief Loads and validates every relation; `bootstrap()` must have run.
  auto open() -> void;
  auto create_table(Transaction& transaction, const TableDef& definition) -> Oid override;
  auto drop_table(Transaction& transaction, Oid table) -> void override;
  auto create_index(Transaction& transaction, const IndexDef& definition) -> Oid override;
  [[nodiscard]] auto lookup_relation(Transaction& transaction, std::string_view name) const
      -> std::optional<RelationDesc> override;
  [[nodiscard]] auto lookup_attribute(Transaction& transaction, Oid relation,
                                      std::string_view name) const
      -> std::optional<AttributeDesc> override;
  [[nodiscard]] auto list_relations(Transaction& transaction) const
      -> std::vector<RelationDesc> override;

  private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/// \brief Immutable snapshot of the catalog used to answer relation lookups.
///
/// The cache shares one `shared_ptr<const RelationDesc>` per relation and builds
/// its `Schema` from the relation attributes. There is no hot invalidation yet.
class RelCache {
  public:
  /// \brief Materializes every relation visible through `catalog`.
  explicit RelCache(const Catalog& catalog);

  /// \brief Relation with `oid`, or `nullptr` when it does not exist.
  [[nodiscard]] auto relation(Oid oid) const -> std::shared_ptr<const RelationDesc>;
  /// \brief Relation named `name`, or `nullptr` when it does not exist.
  [[nodiscard]] auto relation(std::string_view name) const -> std::shared_ptr<const RelationDesc>;
  /// \brief Schema built from `oid`, or `nullptr` when the relation is missing.
  [[nodiscard]] auto schema_of(Oid oid) const -> std::shared_ptr<const Schema>;
  /// \brief Column `name` of `relation`.
  /// \throws SqlStateError with `42703` when the column does not exist and
  /// `42P01` when the relation does not exist.
  [[nodiscard]] auto require_column(Oid relation, std::string_view name) const -> AttributeDesc;
  /// \brief Every cached relation, ordered by OID.
  [[nodiscard]] auto relations() const -> std::vector<std::shared_ptr<const RelationDesc>>;

  private:
  std::vector<std::shared_ptr<const RelationDesc>> relations_;
  std::unordered_map<Oid, std::shared_ptr<const RelationDesc>> by_oid_;
  std::unordered_map<std::string, std::shared_ptr<const RelationDesc>> by_name_;
  std::unordered_map<Oid, std::shared_ptr<const Schema>> schemas_;
};

}  // namespace kravidb::storage
