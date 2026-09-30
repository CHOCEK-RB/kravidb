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
  BTreeNode& operator=(const BTreeNode&) = delete;
  BTreeNode(BTreeNode&&) = delete;
  BTreeNode& operator=(BTreeNode&&) = delete;
  ~BTreeNode() = default;

  [[nodiscard]] int min_degree() const noexcept { return degree_; }

  [[nodiscard]] bool is_leaf() const noexcept { return is_leaf_; }

  [[nodiscard]] std::size_t max_keys() const noexcept {
    return (2 * static_cast<std::size_t>(degree_)) - 1;
  }

  [[nodiscard]] std::size_t min_keys() const noexcept {
    return static_cast<std::size_t>(degree_) - 1;
  }

  [[nodiscard]] std::size_t max_children() const noexcept {
    return 2 * static_cast<std::size_t>(degree_);
  }

  [[nodiscard]] std::size_t key_count() const noexcept { return keys_.size(); }

  [[nodiscard]] std::size_t child_count() const noexcept { return children_.size(); }

  [[nodiscard]] bool is_full() const noexcept { return keys_.size() == max_keys(); }

  [[nodiscard]] bool is_empty() const noexcept { return keys_.empty(); }

  [[nodiscard]] bool has_valid_shape() const noexcept {
    if (keys_.size() != row_ids_.size() || keys_.size() > max_keys()) {
      return false;
    }
    if (is_leaf_) {
      return children_.empty();
    }
    return children_.size() == keys_.size() + 1;
  }

  [[nodiscard]] std::size_t lower_bound_index(Key key, std::size_t& comparisons) const {
    const auto position = std::ranges::lower_bound(keys_, key, [&comparisons](Key lhs, Key rhs) {
      ++comparisons;
      return lhs < rhs;
    });
    return static_cast<std::size_t>(std::distance(keys_.begin(), position));
  }

  [[nodiscard]] std::vector<Key>& keys() noexcept { return keys_; }
  [[nodiscard]] const std::vector<Key>& keys() const noexcept { return keys_; }

  [[nodiscard]] std::vector<RowID>& row_ids() noexcept { return row_ids_; }
  [[nodiscard]] const std::vector<RowID>& row_ids() const noexcept { return row_ids_; }

  [[nodiscard]] std::vector<BTreeNode*>& children() noexcept { return children_; }
  [[nodiscard]] const std::vector<BTreeNode*>& children() const noexcept { return children_; }

  private:
  int degree_;
  bool is_leaf_;
  std::vector<Key> keys_;
  std::vector<RowID> row_ids_;
  std::vector<BTreeNode*> children_;

  static int validate_degree(int degree) {
    if (degree < 2) {
      // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
      throw std::invalid_argument{"BTreeNode: el grado minimo t debe ser >= 2"};
    }
    return degree;
  }
};

}  // namespace kravidb::index
