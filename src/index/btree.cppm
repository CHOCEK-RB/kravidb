/// \file
/// \brief B-tree as an implementation of the `Index` contract.

export module index.btree;

import std;
export import index.btree_node;
export import index.index;

export namespace kravidb::index {

/// \brief Default minimum degree of the B-tree in production.
/// \details The API starts at 2 so the visualizer shows splits;
/// this value is the index default when no other is configured.
inline constexpr int recommended_degree = 64;

/// \brief Result of a search that also reports its statistics.
struct SearchResult {
  /// \brief Locator found, or `std::nullopt` if the key does not exist.
  std::optional<RowID> row_id;

  /// \brief Statistics of the search that produced the result.
  SearchStats stats{};
};

/// \brief Individual step of the path followed by a search.
struct SearchPathStep {
  /// \brief Comparison between the searched key and the examined key.
  enum class Comparison : std::uint8_t {
    /// \brief The searched key is less than the examined one.
    Less,
    /// \brief The searched key matches the examined one.
    Equal,
    /// \brief The searched key is greater than the examined one.
    Greater,
  };

  /// \brief Identifier of the examined node.
  std::size_t node_id{0};

  /// \brief Position examined within the node keys.
  std::size_t key_index{0};

  /// \brief Result of the comparison at that position.
  Comparison comparison{Comparison::Less};
};

/// \brief Trace of a search: result, traversed path and statistics.
struct SearchTrace {
  /// \brief Locator found, or `std::nullopt` if the key does not exist.
  std::optional<RowID> row_id;

  /// \brief Path steps, in descent order.
  std::vector<SearchPathStep> path;

  /// \brief Statistics of the search.
  SearchStats stats{};
};

/// \brief Classic B-tree (CLRS) that implements the `Index` contract.
///
/// Each node stores at most `2t - 1` keys and has at most `2t` children,
/// where `t` is the minimum degree (`t >= 2`). Nodes are owned through the
/// internal pool and `root_` is a non-owning pointer to the root node. The tree
/// is movable but not copyable.
class BTree final : public Index {
  public:
  /// \brief Constructs an empty B-tree.
  /// \param degree Minimum degree `t`; must be greater than or equal to 2.
  explicit BTree(int degree = recommended_degree);

  /// \brief Copy disabled.
  BTree(const BTree&) = delete;

  /// \brief Copy assignment disabled.
  auto operator=(const BTree&) -> BTree& = delete;

  /// \brief Move-constructs, transferring the other tree's nodes.
  BTree(BTree&& other) noexcept;

  /// \brief Move-assigns, freeing the current contents first.
  auto operator=(BTree&& other) noexcept -> BTree&;

  /// \brief Destructor.
  ~BTree() override = default;

  /// \brief Removes all keys and frees the nodes.
  void clear() override;

  /// \brief Indicates whether the tree has no keys.
  [[nodiscard]] auto empty() const noexcept -> bool override;

  /// \brief Statistics of the last search.
  [[nodiscard]] auto last_search_stats() const noexcept -> SearchStats override {
    return last_search_stats_;
  }

  /// \brief Resets the search statistics to zero.
  void reset_search_stats() noexcept override { last_search_stats_ = SearchStats{}; }

  /// \brief Inserts an association between a key and a `RowID`.
  ///
  /// Does not enforce uniqueness: several entries may share the same key.
  /// Primary key uniqueness is validated in `Table`.
  /// \param key Key to insert.
  /// \param row_id Associated locator.
  void insert(Key key, RowID row_id) override;

  /// \brief Statistics of the last insertion performed.
  [[nodiscard]] auto last_insert_stats() const noexcept -> InsertStats override;

  /// \brief Searches for a key and records its statistics.
  /// \param key Key to search for.
  /// \return The associated `RowID`, or `std::nullopt` if the key does not exist.
  [[nodiscard]] auto search(Key key) const -> std::optional<RowID> override;

  /// \brief Minimum degree `t` of the tree.
  [[nodiscard]] auto min_degree() const noexcept -> int { return degree_; }

  /// \brief Non-owning pointer to the root node.
  [[nodiscard]] auto root() const noexcept -> const BTreeNode* { return root_; }

  /// \brief Height of the tree, in number of nodes from the root to a leaf.
  [[nodiscard]] auto height() const noexcept -> std::size_t;

  /// \brief Searches for a key, returning the result together with its statistics.
  /// \param key Key to search for.
  /// \return The locator found and the search counters.
  [[nodiscard]] auto search_with_stats(Key key) const -> SearchResult;

  /// \brief Searches for a key, reconstructing the path followed by the descent.
  /// \param key Key to search for.
  /// \return The locator, the path steps and the search counters.
  [[nodiscard]] auto search_trace(Key key) const -> SearchTrace;

  /// \brief Inserts a key, recording the structural trace of the operation.
  /// \param key Key to insert.
  /// \param row_id Associated locator.
  /// \return The last promoted key if a split occurred, and whether the height grew.
  auto insert_with_stats(Key key, RowID row_id) -> InsertStats;

  private:
  /// \brief Minimum degree `t` of the tree.
  int degree_;

  /// \brief Owner of all the tree's nodes.
  std::vector<std::unique_ptr<BTreeNode>> pool_;

  /// \brief Next node identifier to assign.
  std::size_t next_node_id_{0};

  /// \brief Non-owning pointer to the root; null if the tree is empty.
  BTreeNode* root_;

  /// \brief Statistics of the last search, mutable from `search` (const).
  mutable SearchStats last_search_stats_{};

  /// \brief Splits produced by the last instrumented insertion.
  std::size_t splits_in_last_insert_{0};

  /// \brief Last key promoted by the last instrumented insertion.
  std::optional<Key> last_split_promoted_key_;

  /// \brief Indicates whether the last insertion made the tree height grow.
  bool last_new_root_created_{false};

  /// \brief Splits the full child `child`, promoting its median key to the parent.
  /// \param parent Parent node of `child`.
  /// \param index Position of `child` within `parent`.
  /// \param child Full child node that is split.
  void split_child(BTreeNode* parent, std::size_t index, BTreeNode* child);

  /// \brief Inserts into a node that is not full, descending if necessary.
  /// \param node Node to insert into.
  /// \param key Key to insert.
  /// \param row_id Associated locator.
  void insert_non_full(BTreeNode* node, Key key, RowID row_id);

  /// \brief Reserves a new node in the pool and returns a pointer to it.
  /// \param leaf Indicates whether the new node is a leaf.
  auto create_node(bool leaf) -> BTreeNode*;
};

}  // namespace kravidb::index
