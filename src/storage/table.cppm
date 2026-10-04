/// \file
/// \brief Fachada de tabla: combina un almacen de registros y un indice.

export module storage.table;

import std;
export import storage.record_store;
export import storage.tuple;
export import storage.schema;
export import index.index;

export namespace kravidb::storage {

/// \brief Tabla que persiste tuplas y las indexa por clave primaria.
///
/// El primer campo de cada tupla debe ser un entero de 64 bits y actua como
/// clave primaria (PK). La tabla no posee el almacen ni el indice: los recibe por
/// referencia en la construccion. Opcionalmente recibe un `Schema` para validar campos.
class Table final {
  public:
  /// \brief Construye la tabla sobre un almacen y un indice existentes, con esquema opcional.
  /// \param records Almacen donde se persisten las tuplas (no se posee).
  /// \param index Indice que asocia la PK con el `RowID` (no se posee).
  /// \param schema Esquema relacional opcional para validacion de tuplas.
  Table(RecordStore& records, index::Index& index, Schema schema = Schema{})
      : records_{records}, index_{index}, schema_{std::move(schema)} {}

  /// \brief Esquema relacional de la tabla.
  [[nodiscard]] auto schema() const noexcept -> const Schema& { return schema_; }

  /// \brief Inserta una tupla y la indexa por su clave primaria.
  /// \param tuple Tupla a insertar; el primer campo es la PK.
  /// \note Lanza `std::invalid_argument` si la tupla esta vacia o si el primer
  /// campo no es de tipo `Int`.
  void insert(const Tuple& tuple);

  /// \brief Busca una tupla por su clave primaria.
  /// \param key Clave primaria buscada.
  /// \return La tupla encontrada, o `std::nullopt` si no existe.
  [[nodiscard]] auto find_by_index(index::Key key) const -> std::optional<Tuple>;

  /// \brief Recorre y deserializa todas las tuplas del almacen.
  /// \return Las tuplas encontradas, en orden de pagina y ranura.
  /// \note Usa `slot_at`, de modo que no incrementa las estadisticas de E/S.
  [[nodiscard]] auto scan_all() const -> std::vector<Tuple>;

  private:
  // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
  /// \brief Almacen de registros (referencia no propietaria).
  RecordStore& records_;

  /// \brief Indice de claves primarias (referencia no propietaria).
  index::Index& index_;
  // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

  /// \brief Esquema relacional de la tabla.
  Schema schema_;
};

}  // namespace kravidb::storage
