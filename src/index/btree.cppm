export module index.btree;

import std;
export import index.btree_node;

export namespace kravidb::index {

inline constexpr int recommended_degree = 64;

struct SearchStats {
  std::size_t node_accesses{0};
  std::size_t key_comparisons{0};
};

struct SearchResult {
  std::optional<RowID> row_id;
  SearchStats stats{};
};

class BTree {
  public:
  explicit BTree(int degree = recommended_degree) : degree_{degree}, root_{create_node(true)} {}

  BTree(const BTree&) = delete;
  BTree& operator=(const BTree&) = delete;

  BTree(BTree&& other) noexcept
      : degree_{other.degree_},
        pool_{std::move(other.pool_)},
        root_{std::exchange(other.root_, nullptr)},
        last_search_stats_{other.last_search_stats_} {}

  BTree& operator=(BTree&& other) noexcept {
    if (this != &other) {
      degree_ = other.degree_;
      pool_ = std::move(other.pool_);
      root_ = std::exchange(other.root_, nullptr);
      last_search_stats_ = other.last_search_stats_;
    }
    return *this;
  }

  ~BTree() = default;

  void clear() {
    pool_.clear();
    root_ = nullptr;
    root_ = create_node(true);
    last_search_stats_ = SearchStats{};
  }

  [[nodiscard]] int min_degree() const noexcept { return degree_; }

  [[nodiscard]] const BTreeNode* root() const noexcept { return root_; }

  [[nodiscard]] bool empty() const noexcept { return root_ == nullptr || root_->is_empty(); }

  [[nodiscard]] std::size_t height() const noexcept {
    std::size_t levels = 0;
    for (const BTreeNode* node = root_; node != nullptr && !node->is_leaf();
         node = node->children().front()) {
      ++levels;
    }
    return levels;
  }

  [[nodiscard]] SearchResult search_with_stats(Key key) const {
    SearchResult result{};
    const BTreeNode* node = root_;

    while (node != nullptr) {
      ++result.stats.node_accesses;

      const std::size_t index = node->lower_bound_index(key, result.stats.key_comparisons);

      if (index < node->key_count()) {
        ++result.stats.key_comparisons;
        if (node->keys().at(index) == key) {
          result.row_id = node->row_ids().at(index);
          return result;
        }
      }

      if (node->is_leaf() || index >= node->child_count()) {
        break;
      }
      node = node->children().at(index);
    }
    return result;
  }

  [[nodiscard]] std::optional<RowID> search(Key key) const {
    const SearchResult result = search_with_stats(key);
    last_search_stats_ = result.stats;
    return result.row_id;
  }

  [[nodiscard]] SearchStats last_search_stats() const noexcept { return last_search_stats_; }

  void reset_search_stats() noexcept { last_search_stats_ = SearchStats{}; }

  void insert(Key key, RowID row_id) {
    BTreeNode* r = root_;
    if (r->is_full()) {
      BTreeNode* s = create_node(false);
      root_ = s;
      s->children().push_back(r);
      split_child(s, 0, r);
      insert_non_full(s, key, row_id);
    } else {
      insert_non_full(r, key, row_id);
    }
  }

  private:
  int degree_;
  std::vector<std::unique_ptr<BTreeNode>> pool_;
  BTreeNode* root_;
  mutable SearchStats last_search_stats_{};

  void split_child(BTreeNode* parent, std::size_t index, BTreeNode* child) {
    BTreeNode* new_child = create_node(child->is_leaf());
    const std::size_t t = static_cast<std::size_t>(degree_);

    new_child->keys().assign(child->keys().begin() + t, child->keys().end());
    new_child->row_ids().assign(child->row_ids().begin() + t, child->row_ids().end());

    if (!child->is_leaf()) {
      new_child->children().assign(child->children().begin() + t, child->children().end());
    }

    const Key promoted_key = child->keys()[t - 1];
    const RowID promoted_row_id = child->row_ids()[t - 1];

    child->keys().resize(t - 1);
    child->row_ids().resize(t - 1);
    if (!child->is_leaf()) {
      child->children().resize(t);
    }

    parent->children().insert(parent->children().begin() + index + 1, new_child);
    parent->keys().insert(parent->keys().begin() + index, promoted_key);
    parent->row_ids().insert(parent->row_ids().begin() + index, promoted_row_id);
  }

  void insert_non_full(BTreeNode* node, Key key, RowID row_id) {
    if (node->is_leaf()) {
      const auto it = std::ranges::upper_bound(node->keys(), key);
      const std::size_t index = static_cast<std::size_t>(std::distance(node->keys().begin(), it));

      node->keys().insert(node->keys().begin() + index, key);
      node->row_ids().insert(node->row_ids().begin() + index, row_id);
    } else {
      const auto it = std::ranges::upper_bound(node->keys(), key);
      std::size_t index = static_cast<std::size_t>(std::distance(node->keys().begin(), it));

      BTreeNode* child = node->children()[index];
      if (child->is_full()) {
        split_child(node, index, child);
        if (key > node->keys()[index]) {
          ++index;
        }
      }
      insert_non_full(node->children()[index], key, row_id);
    }
  }

  BTreeNode* create_node(bool leaf) {
    pool_.push_back(std::make_unique<BTreeNode>(degree_, leaf));
    return pool_.back().get();
  }
};

}  // namespace kravidb::index
