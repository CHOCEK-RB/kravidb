export module index.btree;

import std;
export import index.btree_node;

export namespace kravidb::index {

inline constexpr int recommended_degree = 64;

class BTree {
  public:
  explicit BTree(int degree = recommended_degree) : degree_{degree}, root_{create_node(true)} {}

  BTree(const BTree&) = delete;
  BTree& operator=(const BTree&) = delete;

  BTree(BTree&& other) noexcept
      : degree_{other.degree_},
        pool_{std::move(other.pool_)},
        root_{std::exchange(other.root_, nullptr)} {}

  BTree& operator=(BTree&& other) noexcept {
    if (this != &other) {
      degree_ = other.degree_;
      pool_ = std::move(other.pool_);
      root_ = std::exchange(other.root_, nullptr);
    }
    return *this;
  }

  ~BTree() = default;

  void clear() {
    pool_.clear();
    root_ = nullptr;
    root_ = create_node(true);
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

  private:
  int degree_;
  std::vector<std::unique_ptr<BTreeNode>> pool_;
  BTreeNode* root_;

  BTreeNode* create_node(bool leaf) {
    pool_.push_back(std::make_unique<BTreeNode>(degree_, leaf));
    return pool_.back().get();
  }
};

}  // namespace kravidb::index
