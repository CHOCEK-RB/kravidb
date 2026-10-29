/// \file
/// \brief B-tree node (`BTreeNode`) and its invariants.

export module index.btree_node;

import std;

export namespace kravidb::index {

/// \brief Indexed key: signed 64-bit integer.
using Key = std::int64_t;

/// \brief Record locator stored next to each key.
using RowID = std::uint64_t;

/// \brief Node of a B-tree, with at most `2t - 1` keys.
///
/// The node does not own its children: it only stores pointers, and the tree
/// owns the nodes. A leaf node has no children; an internal node has exactly
/// `keys + 1` children.
class BTreeNode {
  public:
  /// \brief Creates an empty node.
  /// \param degree Minimum degree `t`; must be greater than or equal to 2.
  /// \param leaf Indicates whether the node is a leaf.
  /// \param id Stable identifier of the node within its tree.
  explicit BTreeNode(int degree, bool leaf, std::size_t id = 0)
      : degree_{validate_degree(degree)}, is_leaf_{leaf}, id_{id} {
    keys_.reserve(max_keys());
    row_ids_.reserve(max_keys());
    if (!is_leaf_) {
      children_.reserve(max_children());
    }
  }

  /// \brief Copy disabled.
  BTreeNode(const BTreeNode&) = delete;

  /// \brief Copy assignment disabled.
  auto operator=(const BTreeNode&) -> BTreeNode& = delete;

  /// \brief Move disabled (children are pointers and ownership is fixed by the tree).
  BTreeNode(BTreeNode&&) = delete;

  /// \brief Move assignment disabled.
  auto operator=(BTreeNode&&) -> BTreeNode& = delete;

  /// \brief Destructor.
  ~BTreeNode() = default;

  /// \brief Minimum degree `t` of the node.
  [[nodiscard]] auto min_degree() const noexcept -> int { return degree_; }

  /// \brief Indicates whether the node is a leaf.
  [[nodiscard]] auto is_leaf() const noexcept -> bool { return is_leaf_; }

  /// \brief Stable identifier of the node, assigned by the tree.
  [[nodiscard]] auto id() const noexcept -> std::size_t { return id_; }

  /// \brief Maximum number of keys of a node (`2t - 1`).
  [[nodiscard]] auto max_keys() const noexcept -> std::size_t {
    return (2 * static_cast<std::size_t>(degree_)) - 1;
  }

  /// \brief Minimum number of keys of a non-root node (`t - 1`).
  [[nodiscard]] auto min_keys() const noexcept -> std::size_t {
    return static_cast<std::size_t>(degree_) - 1;
  }

  /// \brief Maximum number of children of an internal node (`2t`).
  [[nodiscard]] auto max_children() const noexcept -> std::size_t {
    return 2 * static_cast<std::size_t>(degree_);
  }

  /// \brief Number of keys stored.
  [[nodiscard]] auto key_count() const noexcept -> std::size_t { return keys_.size(); }

  /// \brief Number of children stored.
  [[nodiscard]] auto child_count() const noexcept -> std::size_t { return children_.size(); }

  /// \brief Indicates whether the node reached the maximum number of keys.
  [[nodiscard]] auto is_full() const noexcept -> bool { return keys_.size() == max_keys(); }

  /// \brief Indicates whether the node has no keys.
  [[nodiscard]] auto is_empty() const noexcept -> bool { return keys_.empty(); }

  /// \brief Checks the node's structural invariants.
  /// \return `true` if there are as many `row_ids` as keys and `max_keys()` is not exceeded;
  /// in leaves, if there are no children, and in internal nodes, if there are `keys + 1` children.
  [[nodiscard]] auto has_valid_shape() const noexcept -> bool {
    if (keys_.size() != row_ids_.size() || keys_.size() > max_keys()) {
      return false;
    }
    if (is_leaf_) {
      return children_.empty();
    }
    return children_.size() == keys_.size() + 1;
  }

  /// \brief Index of the first key that is not less than `key`.
  /// \param key Key to compare.
  /// \param comparisons Counter incremented on each comparison performed.
  /// \return The position within `keys()` where to search for or insert `key`.
  [[nodiscard]] auto lower_bound_index(Key key, std::size_t& comparisons) const -> std::size_t {
    const auto position =
        std::ranges::lower_bound(keys_, key, [&comparisons](Key lhs, Key rhs) -> bool {
          ++comparisons;
          return lhs < rhs;
        });
    return static_cast<std::size_t>(std::distance(keys_.begin(), position));
  }

  /// \brief Mutable access to the keys.
  [[nodiscard]] auto keys() noexcept -> std::vector<Key>& { return keys_; }

  /// \brief Read-only access to the keys.
  [[nodiscard]] auto keys() const noexcept -> const std::vector<Key>& { return keys_; }

  /// \brief Mutable access to the locators.
  [[nodiscard]] auto row_ids() noexcept -> std::vector<RowID>& { return row_ids_; }

  /// \brief Read-only access to the locators.
  [[nodiscard]] auto row_ids() const noexcept -> const std::vector<RowID>& { return row_ids_; }

  /// \brief Mutable access to the children.
  [[nodiscard]] auto children() noexcept -> std::vector<BTreeNode*>& { return children_; }

  /// \brief Read-only access to the children.
  [[nodiscard]] auto children() const noexcept -> const std::vector<BTreeNode*>& {
    return children_;
  }

  private:
  /// \brief Minimum degree `t`.
  int degree_;

  /// \brief Indicates whether the node is a leaf.
  bool is_leaf_;

  /// \brief Stable identifier of the node within its tree.
  std::size_t id_;

  /// \brief Keys, sorted in ascending order.
  std::vector<Key> keys_;

  /// \brief Locators parallel to the keys.
  std::vector<RowID> row_ids_;

  /// \brief Children; empty in leaves and `keys + 1` elements in internal nodes.
  std::vector<BTreeNode*> children_;

  /// \brief Validates the received minimum degree.
  /// \param degree Proposed degree.
  /// \return The same degree if it is valid.
  /// \note Throws `std::invalid_argument` if `degree < 2`.
  static auto validate_degree(int degree) -> int {
    if (degree < 2) {
      throw std::invalid_argument{"BTreeNode: minimum degree t must be >= 2"};
    }
    return degree;
  }
};

}  // namespace kravidb::index
