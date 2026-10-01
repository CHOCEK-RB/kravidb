export module index.btree;

import std;
export import index.btree_node;
export import index.index;

export namespace kravidb::index {

inline constexpr int recommended_degree = 64;

struct SearchResult {
  std::optional<RowID> row_id;
  SearchStats stats{};
};

class BTree final : public Index {
  public:
  explicit BTree(int degree = recommended_degree);

  BTree(const BTree&) = delete;
  auto operator=(const BTree&) -> BTree& = delete;

  BTree(BTree&& other) noexcept;
  auto operator=(BTree&& other) noexcept -> BTree&;

  ~BTree() override = default;

  void clear() override;

  [[nodiscard]] auto empty() const noexcept -> bool override;

  [[nodiscard]] auto last_search_stats() const noexcept -> SearchStats override {
    return last_search_stats_;
  }
  void reset_search_stats() noexcept override { last_search_stats_ = SearchStats{}; }

  void insert(Key key, RowID row_id) override;

  [[nodiscard]] auto search(Key key) const -> std::optional<RowID> override;

  [[nodiscard]] auto min_degree() const noexcept -> int { return degree_; }

  [[nodiscard]] auto root() const noexcept -> const BTreeNode* { return root_; }

  [[nodiscard]] auto height() const noexcept -> std::size_t;

  [[nodiscard]] auto search_with_stats(Key key) const -> SearchResult;

  private:
  int degree_;
  std::vector<std::unique_ptr<BTreeNode>> pool_;
  BTreeNode* root_;
  mutable SearchStats last_search_stats_{};

  void split_child(BTreeNode* parent, std::size_t index, BTreeNode* child);
  void insert_non_full(BTreeNode* node, Key key, RowID row_id);
  auto create_node(bool leaf) -> BTreeNode*;
};

}  // namespace kravidb::index
