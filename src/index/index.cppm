export module index.index;

import std;
export import index.btree_node;

export namespace kravidb::index {

struct SearchStats {
  std::size_t node_accesses{0};
  std::size_t key_comparisons{0};
};

class Index {
  public:
  Index() = default;
  Index(const Index&) = delete;
  auto operator=(const Index&) -> Index& = delete;
  Index(Index&&) = delete;
  auto operator=(Index&&) -> Index& = delete;
  virtual ~Index() = default;

  virtual void insert(Key key, RowID row_id) = 0;

  [[nodiscard]] virtual auto search(Key key) const -> std::optional<RowID> = 0;

  [[nodiscard]] virtual auto empty() const noexcept -> bool = 0;

  virtual void clear() = 0;

  [[nodiscard]] virtual auto last_search_stats() const noexcept -> SearchStats = 0;
  virtual void reset_search_stats() noexcept = 0;
};

}  // namespace kravidb::index
