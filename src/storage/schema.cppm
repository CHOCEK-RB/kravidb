/// \file
/// \brief Definicion de columnas y esquema de tabla (`Schema`, `ColumnDef`).

export module storage.schema;

import std;
export import storage.tuple;

export namespace kravidb::storage {

/// \brief Definicion de una columna dentro del esquema de una tabla.
struct ColumnDef {
  /// \brief Nombre identificador de la columna.
  std::string name;

  /// \brief Tipo de dato almacenado en la columna.
  FieldType type{FieldType::Int};

  /// \brief Indica si la columna forma la clave primaria (PK).
  bool is_primary_key{false};

  /// \brief Compara dos definiciones de columna campo a campo.
  [[nodiscard]] auto operator==(const ColumnDef&) const -> bool = default;
};

/// \brief Esquema relacional de una tabla: lista ordenada de columnas y su clave primaria.
class Schema {
  public:
  /// \brief Construye un esquema vacio sin columnas.
  Schema() = default;

  /// \brief Construye un esquema a partir de sus columnas.
  /// \param columns Coleccion ordenada de columnas de la tabla.
  /// \note Lanza `std::invalid_argument` si alguna columna tiene nombre vacio o
  /// si se define mas de una clave primaria.
  explicit Schema(std::vector<ColumnDef> columns) : columns_{std::move(columns)} {
    for (std::size_t index = 0; index < columns_.size(); ++index) {
      const auto& col = columns_.at(index);
      if (col.name.empty()) {
        throw std::invalid_argument{"Schema: el nombre de la columna no puede estar vacio"};
      }
      if (col.is_primary_key) {
        if (pk_index_.has_value()) {
          throw std::invalid_argument{"Schema: solo se permite una clave primaria"};
        }
        pk_index_ = index;
      }
    }
  }

  /// \brief Acceso de solo lectura a la lista de columnas.
  [[nodiscard]] auto columns() const noexcept -> const std::vector<ColumnDef>& { return columns_; }

  /// \brief Numero de columnas definidas en el esquema.
  [[nodiscard]] auto column_count() const noexcept -> std::size_t { return columns_.size(); }

  /// \brief Indica si el esquema no tiene columnas.
  [[nodiscard]] auto empty() const noexcept -> bool { return columns_.empty(); }

  /// \brief Obtiene la definicion de una columna por su posicion ordinal.
  /// \param index Posicion de la columna (base 0).
  /// \return Referencia a la definicion de la columna.
  /// \note Lanza `std::out_of_range` si el indice supera `column_count()`.
  [[nodiscard]] auto column_at(std::size_t index) const -> const ColumnDef& {
    return columns_.at(index);
  }

  /// \brief Busca el indice ordinal de una columna por su nombre.
  /// \param name Nombre de la columna buscada.
  /// \return La posicion ordinal, o `std::nullopt` si no existe.
  [[nodiscard]] auto find_column(std::string_view name) const noexcept
      -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < columns_.size(); ++index) {
      if (columns_.at(index).name == name) {
        return index;
      }
    }
    return std::nullopt;
  }

  /// \brief Indice ordinal de la clave primaria, si esta definida.
  [[nodiscard]] auto primary_key_index() const noexcept -> std::optional<std::size_t> {
    return pk_index_;
  }

  /// \brief Valida si una tupla se ajusta a las columnas y tipos del esquema.
  /// \param tuple Tupla a comprobar.
  /// \return `true` si el numero de campos y cada tipo coinciden con el esquema.
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
  /// \brief Definiciones de columna en orden ordinal.
  std::vector<ColumnDef> columns_;

  /// \brief Indice ordinal de la clave primaria (si se definio alguna).
  std::optional<std::size_t> pk_index_{std::nullopt};
};

}  // namespace kravidb::storage
