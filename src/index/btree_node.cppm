export module index.btree_node;

import std;

export namespace kravidb::index {

using Key = std::int64_t;
using RowID = std::uint64_t;

class BTreeNode {
  public:
  explicit BTreeNode(int degree, bool leaf) : degree_{validate_degree(degree)}, is_leaf_{leaf} {
    keys_.reserve(max_keys());
    row_ids_.reserve(max_keys());
    if (!is_leaf_) {
      children_.reserve(max_children());
    }
  }

  BTreeNode(const BTreeNode&) = delete;
  auto operator=(const BTreeNode&) -> BTreeNode& = delete;
  BTreeNode(BTreeNode&&) = delete;
  auto operator=(BTreeNode&&) -> BTreeNode& = delete;
  ~BTreeNode() = default;

  [[nodiscard]] auto min_degree() const noexcept -> int { return degree_; }

  [[nodiscard]] auto is_leaf() const noexcept -> bool { return is_leaf_; }

  [[nodiscard]] auto max_keys() const noexcept -> std::size_t {
    return (2 * static_cast<std::size_t>(degree_)) - 1;
  }

  [[nodiscard]] auto min_keys() const noexcept -> std::size_t {
    return static_cast<std::size_t>(degree_) - 1;
  }

  [[nodiscard]] auto max_children() const noexcept -> std::size_t {
    return 2 * static_cast<std::size_t>(degree_);
  }

  [[nodiscard]] auto key_count() const noexcept -> std::size_t { return keys_.size(); }

  [[nodiscard]] auto child_count() const noexcept -> std::size_t { return children_.size(); }

  [[nodiscard]] auto is_full() const noexcept -> bool { return keys_.size() == max_keys(); }

  [[nodiscard]] auto is_empty() const noexcept -> bool { return keys_.empty(); }

  [[nodiscard]] auto has_valid_shape() const noexcept -> bool {
    if (keys_.size() != row_ids_.size() || keys_.size() > max_keys()) {
      return false;
    }
    if (is_leaf_) {
      return children_.empty();
    }
    return children_.size() == keys_.size() + 1;
  }

  [[nodiscard]] auto lower_bound_index(Key key, std::size_t& comparisons) const -> std::size_t {
    const auto position =
        std::ranges::lower_bound(keys_, key, [&comparisons](Key lhs, Key rhs) -> bool {
          ++comparisons;
          return lhs < rhs;
        });
    return static_cast<std::size_t>(std::distance(keys_.begin(), position));
  }

  [[nodiscard]] auto keys() noexcept -> std::vector<Key>& { return keys_; }
  [[nodiscard]] auto keys() const noexcept -> const std::vector<Key>& { return keys_; }

  [[nodiscard]] auto row_ids() noexcept -> std::vector<RowID>& { return row_ids_; }
  [[nodiscard]] auto row_ids() const noexcept -> const std::vector<RowID>& { return row_ids_; }

  [[nodiscard]] auto children() noexcept -> std::vector<BTreeNode*>& { return children_; }
  [[nodiscard]] auto children() const noexcept -> const std::vector<BTreeNode*>& {
    return children_;
  }

  private:
  int degree_;
  bool is_leaf_;
  std::vector<Key> keys_;
  std::vector<RowID> row_ids_;
  std::vector<BTreeNode*> children_;

  static auto validate_degree(int degree) -> int {
    if (degree < 2) {
      // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
      throw std::invalid_argument{"BTreeNode: el grado minimo t debe ser >= 2"};
    }
    return degree;
  }
};

}  // namespace kravidb::index
