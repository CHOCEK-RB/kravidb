/// \file
/// \brief Definition of table columns and schema (`Schema`, `ColumnDef`).

export module storage.schema;

import std;
export import storage.tuple;

export namespace kravidb::storage {

/// \brief Definition of a column within a table schema.
struct ColumnDef {
  /// \brief Identifier name of the column.
  std::string name;

  /// \brief Data type stored in the column.
  FieldType type{FieldType::Int};

  /// \brief Indicates whether the column forms the primary key (PK).
  bool is_primary_key{false};

  /// \brief Compares two column definitions field by field.
  [[nodiscard]] auto operator==(const ColumnDef&) const -> bool = default;
};

/// \brief Relational schema of a table: ordered list of columns and their primary key.
class Schema {
  public:
  /// \brief Builds an empty schema with no columns.
  Schema() = default;

  /// \brief Builds a schema from its columns.
  /// \param columns Ordered collection of table columns.
  /// \note Throws `std::invalid_argument` if any column has an empty name or
  /// if more than one primary key is defined.
  explicit Schema(std::vector<ColumnDef> columns) : columns_{std::move(columns)} {
    for (std::size_t index = 0; index < columns_.size(); ++index) {
      const auto& col = columns_.at(index);
      if (col.name.empty()) {
        throw std::invalid_argument{"Schema: column name must not be empty"};
      }
      if (col.is_primary_key) {
        if (pk_index_.has_value()) {
          throw std::invalid_argument{"Schema: only one primary key is allowed"};
        }
        pk_index_ = index;
      }
    }
  }

  /// \brief Read-only access to the column list.
  [[nodiscard]] auto columns() const noexcept -> const std::vector<ColumnDef>& { return columns_; }

  /// \brief Number of columns defined in the schema.
  [[nodiscard]] auto column_count() const noexcept -> std::size_t { return columns_.size(); }

  /// \brief Indicates whether the schema has no columns.
  [[nodiscard]] auto empty() const noexcept -> bool { return columns_.empty(); }

  /// \brief Gets the definition of a column by its ordinal position.
  /// \param index Column position (0-based).
  /// \return Reference to the column definition.
  /// \note Throws `std::out_of_range` if the index exceeds `column_count()`.
  [[nodiscard]] auto column_at(std::size_t index) const -> const ColumnDef& {
    return columns_.at(index);
  }

  /// \brief Searches the ordinal index of a column by its name.
  /// \param name Name of the column being searched.
  /// \return The ordinal position, or `std::nullopt` if it does not exist.
  [[nodiscard]] auto find_column(std::string_view name) const noexcept
      -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < columns_.size(); ++index) {
      if (columns_.at(index).name == name) {
        return index;
      }
    }
    return std::nullopt;
  }

  /// \brief Ordinal index of the primary key, if it is defined.
  [[nodiscard]] auto primary_key_index() const noexcept -> std::optional<std::size_t> {
    return pk_index_;
  }

  /// \brief Validates whether a tuple matches the columns and types of the schema.
  /// \param tuple Tuple to check.
  /// \return `true` if the field count and each type match the schema.
  [[nodiscard]] auto validate(const Tuple& tuple) const noexcept -> bool {
    if (tuple.field_count() != columns_.size()) {
      return false;
    }
    const auto& fields = tuple.fields();
    for (std::size_t index = 0; index < columns_.size(); ++index) {
      const auto& col = columns_.at(index);
      const auto& field = fields.at(index);
      if (col.type == FieldType::Int && !std::holds_alternative<std::int64_t>(field)) {
        return false;
      }
      if (col.type == FieldType::Varchar && !std::holds_alternative<std::string>(field)) {
        return false;
      }
    }
    return true;
  }

  private:
  /// \brief Column definitions in ordinal order.
  std::vector<ColumnDef> columns_;

  /// \brief Ordinal index of the primary key (if one was defined).
  std::optional<std::size_t> pk_index_{std::nullopt};
};

}  // namespace kravidb::storage
