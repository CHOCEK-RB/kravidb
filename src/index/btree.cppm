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

  private:
  int degree_;
  std::vector<std::unique_ptr<BTreeNode>> pool_;
  BTreeNode* root_;
  mutable SearchStats last_search_stats_{};

  BTreeNode* create_node(bool leaf) {
    pool_.push_back(std::make_unique<BTreeNode>(degree_, leaf));
    return pool_.back().get();
  }
};

}  // namespace kravidb::index
