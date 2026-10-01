/// \file
/// \brief Arbol B como implementacion del contrato `Index`.

export module index.btree;

import std;
export import index.btree_node;
export import index.index;

export namespace kravidb::index {

/// \brief Grado minimo por defecto del arbol B.
inline constexpr int recommended_degree = 64;

/// \brief Resultado de una busqueda que ademas informa de sus estadisticas.
struct SearchResult {
  /// \brief Localizador encontrado, o `std::nullopt` si la clave no existe.
  std::optional<RowID> row_id;

  /// \brief Estadisticas de la busqueda que produjo el resultado.
  SearchStats stats{};
};

/// \brief Arbol B clasico (CLRS) que implementa el contrato `Index`.
///
/// Cada nodo guarda como maximo `2t - 1` claves y tiene como maximo `2t` hijos,
/// donde `t` es el grado minimo (`t >= 2`). Los nodos se poseen a traves del pool
/// interno y `root_` es un puntero no propietario al nodo raiz. El arbol es
/// movible pero no copiable.
class BTree final : public Index {
  public:
  /// \brief Construye un arbol B vacio.
  /// \param degree Grado minimo `t`; debe ser mayor o igual que 2.
  explicit BTree(int degree = recommended_degree);

  /// \brief Copia deshabilitada.
  BTree(const BTree&) = delete;

  /// \brief Asignacion por copia deshabilitada.
  auto operator=(const BTree&) -> BTree& = delete;

  /// \brief Construye por movimiento, transfiriendo los nodos del otro arbol.
  BTree(BTree&& other) noexcept;

  /// \brief Asigna por movimiento, liberando antes el contenido actual.
  auto operator=(BTree&& other) noexcept -> BTree&;

  /// \brief Destructor.
  ~BTree() override = default;

  /// \brief Elimina todas las claves y libera los nodos.
  void clear() override;

  /// \brief Indica si el arbol no tiene claves.
  [[nodiscard]] auto empty() const noexcept -> bool override;

  /// \brief Estadisticas de la ultima busqueda.
  [[nodiscard]] auto last_search_stats() const noexcept -> SearchStats override {
    return last_search_stats_;
  }

  /// \brief Pone a cero las estadisticas de busqueda.
  void reset_search_stats() noexcept override { last_search_stats_ = SearchStats{}; }

  /// \brief Inserta o reemplaza la asociacion de una clave.
  /// \param key Clave a insertar.
  /// \param row_id Localizador asociado.
  void insert(Key key, RowID row_id) override;

  /// \brief Busca una clave y registra sus estadisticas.
  /// \param key Clave buscada.
  /// \return El `RowID` asociado, o `std::nullopt` si la clave no existe.
  [[nodiscard]] auto search(Key key) const -> std::optional<RowID> override;

  /// \brief Grado minimo `t` del arbol.
  [[nodiscard]] auto min_degree() const noexcept -> int { return degree_; }

  /// \brief Puntero no propietario al nodo raiz.
  [[nodiscard]] auto root() const noexcept -> const BTreeNode* { return root_; }

  /// \brief Altura del arbol, en numero de nodos desde la raiz hasta una hoja.
  [[nodiscard]] auto height() const noexcept -> std::size_t;

  /// \brief Busca una clave devolviendo el resultado junto con sus estadisticas.
  /// \param key Clave buscada.
  /// \return El localizador encontrado y los contadores de la busqueda.
  [[nodiscard]] auto search_with_stats(Key key) const -> SearchResult;

  private:
  /// \brief Grado minimo `t` del arbol.
  int degree_;

  /// \brief Propietario de todos los nodos del arbol.
  std::vector<std::unique_ptr<BTreeNode>> pool_;

  /// \brief Puntero no propietario a la raiz; nulo si el arbol esta vacio.
  BTreeNode* root_;

  /// \brief Estadisticas de la ultima busqueda, mutables desde `search` (const).
  mutable SearchStats last_search_stats_{};

  /// \brief Divide el hijo lleno `child`, promoviendo su clave media al padre.
  /// \param parent Nodo padre de `child`.
  /// \param index Posicion de `child` dentro de `parent`.
  /// \param child Nodo hijo lleno que se divide.
  void split_child(BTreeNode* parent, std::size_t index, BTreeNode* child);

  /// \brief Inserta en un nodo que no esta lleno, descendiendo si es necesario.
  /// \param node Nodo en el que insertar.
  /// \param key Clave a insertar.
  /// \param row_id Localizador asociado.
  void insert_non_full(BTreeNode* node, Key key, RowID row_id);

  /// \brief Reserva un nodo nuevo en el pool y devuelve un puntero a el.
  /// \param leaf Indica si el nodo nuevo es hoja.
  auto create_node(bool leaf) -> BTreeNode*;
};

}  // namespace kravidb::index
