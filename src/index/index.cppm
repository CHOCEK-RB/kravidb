/// \file
/// \brief `Index` contract and search statistics.

export module index.index;

import std;
export import index.btree_node;

export namespace kravidb::index {

/// \brief Counters of the last search performed on an index.
struct SearchStats {
  /// \brief Index nodes visited during the search.
  std::size_t node_accesses{0};

  /// \brief Key comparisons performed during the search.
  std::size_t key_comparisons{0};
};

/// \brief Structural trace of an insertion.
struct InsertStats {
  /// \brief Indicates whether the insertion caused at least one node split.
  bool split_occurred{false};

  /// \brief Last key promoted to the parent, if a split occurred.
  std::optional<Key> promoted_key;

  /// \brief Indicates whether the root changed because the tree grew in height.
  bool new_root_created{false};
};

/// \brief Abstract contract of an index that associates a key with a `RowID`.
///
/// It relates 64-bit integer keys to record locators. It is neither copyable
/// nor movable: it is consumed by reference through the interface.
class Index {
  public:
  /// \brief Constructs the contract, with no state of its own.
  Index() = default;

  /// \brief Copy disabled: the contract is used by reference.
  Index(const Index&) = delete;

  /// \brief Copy assignment disabled.
  auto operator=(const Index&) -> Index& = delete;

  /// \brief Move disabled.
  Index(Index&&) = delete;

  /// \brief Move assignment disabled.
  auto operator=(Index&&) -> Index& = delete;

  /// \brief Virtual destructor.
  virtual ~Index() = default;

  /// \brief Inserts an association between a key and a `RowID`.
  ///
  /// The index does not enforce uniqueness: it may store several entries with
  /// the same key. Primary key uniqueness is a rule of `Table`.
  /// \param key Key to index.
  /// \param row_id Associated record locator.
  virtual void insert(Key key, RowID row_id) = 0;

  /// \brief Statistics of the last insertion performed.
  [[nodiscard]] virtual auto last_insert_stats() const noexcept -> InsertStats = 0;

  /// \brief Searches for a key in the index.
  /// \param key Key to search for.
  /// \return The associated `RowID`, or `std::nullopt` if the key does not exist.
  [[nodiscard]] virtual auto search(Key key) const -> std::optional<RowID> = 0;

  /// \brief Indicates whether the index contains no entries.
  [[nodiscard]] virtual auto empty() const noexcept -> bool = 0;

  /// \brief Removes all entries from the index.
  virtual void clear() = 0;

  /// \brief Statistics of the last search performed.
  [[nodiscard]] virtual auto last_search_stats() const noexcept -> SearchStats = 0;

  /// \brief Resets the search statistics to zero.
  virtual void reset_search_stats() noexcept = 0;
};

}  // namespace kravidb::index
