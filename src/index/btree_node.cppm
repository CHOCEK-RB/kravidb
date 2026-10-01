/// \file
/// \brief Nodo del arbol B (`BTreeNode`) y sus invariantes.

export module index.btree_node;

import std;

export namespace kravidb::index {

/// \brief Clave indexada: entero de 64 bits con signo.
using Key = std::int64_t;

/// \brief Localizador de registro almacenado junto a cada clave.
using RowID = std::uint64_t;

/// \brief Nodo de un arbol B, con `2t - 1` claves como maximo.
///
/// El nodo no posee a sus hijos: solo guarda punteros, y es el arbol quien posee
/// los nodos. Un nodo hoja no tiene hijos; un nodo interno tiene exactamente
/// `claves + 1` hijos.
class BTreeNode {
  public:
  /// \brief Crea un nodo vacio.
  /// \param degree Grado minimo `t`; debe ser mayor o igual que 2.
  /// \param leaf Indica si el nodo es hoja.
  explicit BTreeNode(int degree, bool leaf) : degree_{validate_degree(degree)}, is_leaf_{leaf} {
    keys_.reserve(max_keys());
    row_ids_.reserve(max_keys());
    if (!is_leaf_) {
      children_.reserve(max_children());
    }
  }

  /// \brief Copia deshabilitada.
  BTreeNode(const BTreeNode&) = delete;

  /// \brief Asignacion por copia deshabilitada.
  auto operator=(const BTreeNode&) -> BTreeNode& = delete;

  /// \brief Movimiento deshabilitado (los hijos son punteros y la propiedad la fija el arbol).
  BTreeNode(BTreeNode&&) = delete;

  /// \brief Asignacion por movimiento deshabilitada.
  auto operator=(BTreeNode&&) -> BTreeNode& = delete;

  /// \brief Destructor.
  ~BTreeNode() = default;

  /// \brief Grado minimo `t` del nodo.
  [[nodiscard]] auto min_degree() const noexcept -> int { return degree_; }

  /// \brief Indica si el nodo es hoja.
  [[nodiscard]] auto is_leaf() const noexcept -> bool { return is_leaf_; }

  /// \brief Numero maximo de claves de un nodo (`2t - 1`).
  [[nodiscard]] auto max_keys() const noexcept -> std::size_t {
    return (2 * static_cast<std::size_t>(degree_)) - 1;
  }

  /// \brief Numero minimo de claves de un nodo no raiz (`t - 1`).
  [[nodiscard]] auto min_keys() const noexcept -> std::size_t {
    return static_cast<std::size_t>(degree_) - 1;
  }

  /// \brief Numero maximo de hijos de un nodo interno (`2t`).
  [[nodiscard]] auto max_children() const noexcept -> std::size_t {
    return 2 * static_cast<std::size_t>(degree_);
  }

  /// \brief Numero de claves almacenadas.
  [[nodiscard]] auto key_count() const noexcept -> std::size_t { return keys_.size(); }

  /// \brief Numero de hijos almacenados.
  [[nodiscard]] auto child_count() const noexcept -> std::size_t { return children_.size(); }

  /// \brief Indica si el nodo alcanzo el maximo de claves.
  [[nodiscard]] auto is_full() const noexcept -> bool { return keys_.size() == max_keys(); }

  /// \brief Indica si el nodo no tiene claves.
  [[nodiscard]] auto is_empty() const noexcept -> bool { return keys_.empty(); }

  /// \brief Comprueba las invariantes estructurales del nodo.
  /// \return `true` si hay tantos `row_ids` como claves y no se supera `max_keys()`; en
  /// las hojas, si no hay hijos, y en los nodos internos, si hay `claves + 1` hijos.
  [[nodiscard]] auto has_valid_shape() const noexcept -> bool {
    if (keys_.size() != row_ids_.size() || keys_.size() > max_keys()) {
      return false;
    }
    if (is_leaf_) {
      return children_.empty();
    }
    return children_.size() == keys_.size() + 1;
  }

  /// \brief Indice de la primera clave que no es menor que `key`.
  /// \param key Clave a comparar.
  /// \param comparisons Contador que se incrementa con cada comparacion realizada.
  /// \return La posicion dentro de `keys()` donde buscar o insertar `key`.
  [[nodiscard]] auto lower_bound_index(Key key, std::size_t& comparisons) const -> std::size_t {
    const auto position =
        std::ranges::lower_bound(keys_, key, [&comparisons](Key lhs, Key rhs) -> bool {
          ++comparisons;
          return lhs < rhs;
        });
    return static_cast<std::size_t>(std::distance(keys_.begin(), position));
  }

  /// \brief Acceso mutable a las claves.
  [[nodiscard]] auto keys() noexcept -> std::vector<Key>& { return keys_; }

  /// \brief Acceso de solo lectura a las claves.
  [[nodiscard]] auto keys() const noexcept -> const std::vector<Key>& { return keys_; }

  /// \brief Acceso mutable a los localizadores.
  [[nodiscard]] auto row_ids() noexcept -> std::vector<RowID>& { return row_ids_; }

  /// \brief Acceso de solo lectura a los localizadores.
  [[nodiscard]] auto row_ids() const noexcept -> const std::vector<RowID>& { return row_ids_; }

  /// \brief Acceso mutable a los hijos.
  [[nodiscard]] auto children() noexcept -> std::vector<BTreeNode*>& { return children_; }

  /// \brief Acceso de solo lectura a los hijos.
  [[nodiscard]] auto children() const noexcept -> const std::vector<BTreeNode*>& {
    return children_;
  }

  private:
  /// \brief Grado minimo `t`.
  int degree_;

  /// \brief Indica si el nodo es hoja.
  bool is_leaf_;

  /// \brief Claves, ordenadas de forma ascendente.
  std::vector<Key> keys_;

  /// \brief Localizadores paralelos a las claves.
  std::vector<RowID> row_ids_;

  /// \brief Hijos; vacio en las hojas y `claves + 1` elementos en los nodos internos.
  std::vector<BTreeNode*> children_;

  /// \brief Valida el grado minimo recibido.
  /// \param degree Grado propuesto.
  /// \return El mismo grado si es valido.
  /// \note Lanza `std::invalid_argument` si `degree < 2`.
  static auto validate_degree(int degree) -> int {
    if (degree < 2) {
      throw std::invalid_argument{"BTreeNode: el grado minimo t debe ser >= 2"};
    }
    return degree;
  }
};

}  // namespace kravidb::index
