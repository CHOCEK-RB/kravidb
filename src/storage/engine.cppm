/// \file
/// \brief Fachada `StorageEngine` que ensambla los componentes del motor.

export module storage.engine;

import std;
export import storage.page_manager;
export import storage.record_store;
export import storage.table;
export import index.btree;
export import index.index;

export namespace kravidb::storage {

/// \brief Motor de almacenamiento: posee y conecta almacen, indice y tabla.
///
/// Es el punto de entrada de mas alto nivel. Construye un `PageManager`, un
/// `BTree` y un `Table` que los enlaza, y expone cada componente por referencia.
class StorageEngine final {
  public:
  /// \brief Construye el motor con el tamano de pagina y el grado indicados.
  /// \param page_size Tamano de pagina del almacen, en bytes.
  /// \param degree Grado minimo `t` del arbol B del indice.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  explicit StorageEngine(std::size_t page_size = default_page_size,
                         int degree = index::recommended_degree)
      : records_{page_size}, btree_{degree}, table_{records_, btree_} {}

  /// \brief Acceso al almacen de registros.
  [[nodiscard]] auto records() noexcept -> RecordStore& { return records_; }

  /// \brief Acceso al indice.
  [[nodiscard]] auto index() noexcept -> index::Index& { return btree_; }

  /// \brief Acceso concreto al arbol B, para estadisticas de insercion y trazas.
  [[nodiscard]] auto btree() noexcept -> index::BTree& { return btree_; }

  /// \brief Acceso concreto de solo lectura al arbol B.
  [[nodiscard]] auto btree() const noexcept -> const index::BTree& { return btree_; }

  /// \brief Acceso a la tabla.
  [[nodiscard]] auto table() noexcept -> Table& { return table_; }

  private:
  /// \brief Almacen de registros paginado.
  PageManager records_;

  /// \brief Indice sobre la clave primaria.
  index::BTree btree_;

  /// \brief Tabla que combina almacen e indice.
  Table table_;
};

}  // namespace kravidb::storage

/// \brief Imprime el mensaje de arranque del motor.
export void init_storage();
