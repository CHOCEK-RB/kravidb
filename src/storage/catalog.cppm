/// \file
/// \brief Relation catalog contract and its in-memory adapter.

export module storage.catalog;

import std;
export import storage.relation_file;
export import storage.transaction;

export namespace kravidb::storage {

/// \brief Object identifier.
using Oid = std::uint32_t;

/// \brief OID that names no object.
inline constexpr Oid invalid_oid = 0;

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
  [[nodiscard]] virtual auto lookup_relation(Transaction& transaction, std::string_view name)
      -> std::optional<RelationDesc> = 0;

  /// \brief Attribute `name` of relation `relation`, if both exist.
  [[nodiscard]] virtual auto lookup_attribute(Transaction& transaction, Oid relation,
                                              std::string_view name)
      -> std::optional<AttributeDesc> = 0;

  /// \brief Every relation known to the catalog.
  [[nodiscard]] virtual auto list_relations(Transaction& transaction)
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
  [[nodiscard]] auto lookup_relation(Transaction& transaction, std::string_view name)
      -> std::optional<RelationDesc> override;
  [[nodiscard]] auto lookup_attribute(Transaction& transaction, Oid relation, std::string_view name)
      -> std::optional<AttributeDesc> override;
  [[nodiscard]] auto list_relations(Transaction& transaction) -> std::vector<RelationDesc> override;

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
    relation.attributes.at(index).attnum = static_cast<std::int16_t>(index + 1);
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

inline auto SchemaCatalog::lookup_relation(Transaction& transaction, std::string_view name)
    -> std::optional<RelationDesc> {
  const auto relation = std::ranges::find_if(
      relations_, [name](const RelationDesc& candidate) -> bool { return candidate.name == name; });
  if (relation == relations_.end()) {
    return std::nullopt;
  }
  return *relation;
}

inline auto SchemaCatalog::lookup_attribute(Transaction& transaction, Oid relation,
                                            std::string_view name) -> std::optional<AttributeDesc> {
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

inline auto SchemaCatalog::list_relations(Transaction& transaction) -> std::vector<RelationDesc> {
  return relations_;
}

}  // namespace kravidb::storage
