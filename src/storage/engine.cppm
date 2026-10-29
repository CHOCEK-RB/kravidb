/// \file
/// \brief `StorageEngine` facade that assembles the engine components.

export module storage.engine;

import std;
export import storage.page_manager;
export import storage.record_store;
export import storage.table;
export import index.btree;
export import index.index;

export namespace kravidb::storage {

/// \brief Storage engine: owns and wires the record store, index and table.
///
/// It is the highest-level entry point. It builds a `PageManager`, a `BTree` and
/// a `Table` that links them, and exposes each component by reference.
class StorageEngine final {
  public:
  /// \brief Builds the engine with the given page size, degree and schema.
  /// \param page_size Page size of the store, in bytes.
  /// \param degree Minimum degree `t` of the index B-tree.
  /// \param schema Optional schema that validates the table tuples.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  explicit StorageEngine(std::size_t page_size = default_page_size,
                         int degree = index::recommended_degree, Schema schema = Schema{})
      : records_{page_size}, btree_{degree}, table_{records_, btree_, std::move(schema)} {}

  /// \brief Access to the record store.
  [[nodiscard]] auto records() noexcept -> RecordStore& { return records_; }

  /// \brief Read-only access to the record store.
  [[nodiscard]] auto records() const noexcept -> const RecordStore& { return records_; }

  /// \brief Access to the index.
  [[nodiscard]] auto index() noexcept -> index::Index& { return btree_; }

  /// \brief Read-only access to the index.
  [[nodiscard]] auto index() const noexcept -> const index::Index& { return btree_; }

  /// \brief Concrete access to the B-tree, for insertion statistics and traces.
  [[nodiscard]] auto btree() noexcept -> index::BTree& { return btree_; }

  /// \brief Concrete read-only access to the B-tree.
  [[nodiscard]] auto btree() const noexcept -> const index::BTree& { return btree_; }

  /// \brief Access to the table.
  [[nodiscard]] auto table() noexcept -> Table& { return table_; }

  /// \brief Read-only access to the table.
  [[nodiscard]] auto table() const noexcept -> const Table& { return table_; }

  private:
  /// \brief Paged record store.
  PageManager records_;

  /// \brief Index over the primary key.
  index::BTree btree_;

  /// \brief Table that combines store and index.
  Table table_;
};

}  // namespace kravidb::storage

/// \brief Prints the engine startup message.
export void init_storage();
