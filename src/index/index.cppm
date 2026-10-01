/// \file
/// \brief Contrato `Index` y estadisticas de busqueda.

export module index.index;

import std;
export import index.btree_node;

export namespace kravidb::index {

/// \brief Contadores de la ultima busqueda realizada en un indice.
struct SearchStats {
  /// \brief Nodos del indice visitados durante la busqueda.
  std::size_t node_accesses{0};

  /// \brief Comparaciones de clave realizadas durante la busqueda.
  std::size_t key_comparisons{0};
};

/// \brief Contrato abstracto de un indice que asocia clave con `RowID`.
///
/// Relaciona claves enteras de 64 bits con localizadores de registro. No es
/// copiable ni movible: se consume por referencia a traves de la interfaz.
class Index {
  public:
  /// \brief Construye el contrato, sin estado propio.
  Index() = default;

  /// \brief Copia deshabilitada: el contrato se usa por referencia.
  Index(const Index&) = delete;

  /// \brief Asignacion por copia deshabilitada.
  auto operator=(const Index&) -> Index& = delete;

  /// \brief Movimiento deshabilitado.
  Index(Index&&) = delete;

  /// \brief Asignacion por movimiento deshabilitada.
  auto operator=(Index&&) -> Index& = delete;

  /// \brief Destructor virtual.
  virtual ~Index() = default;

  /// \brief Inserta o reemplaza la asociacion de una clave.
  /// \param key Clave a indexar.
  /// \param row_id Localizador de registro asociado.
  virtual void insert(Key key, RowID row_id) = 0;

  /// \brief Busca una clave en el indice.
  /// \param key Clave buscada.
  /// \return El `RowID` asociado, o `std::nullopt` si la clave no existe.
  [[nodiscard]] virtual auto search(Key key) const -> std::optional<RowID> = 0;

  /// \brief Indica si el indice no contiene ninguna entrada.
  [[nodiscard]] virtual auto empty() const noexcept -> bool = 0;

  /// \brief Elimina todas las entradas del indice.
  virtual void clear() = 0;

  /// \brief Estadisticas de la ultima busqueda realizada.
  [[nodiscard]] virtual auto last_search_stats() const noexcept -> SearchStats = 0;

  /// \brief Pone a cero las estadisticas de busqueda.
  virtual void reset_search_stats() noexcept = 0;
};

}  // namespace kravidb::index
