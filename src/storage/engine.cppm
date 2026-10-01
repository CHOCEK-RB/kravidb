export module storage.engine;

import std;
export import storage.page_manager;
export import storage.record_store;
export import storage.table;
export import index.btree;
export import index.index;

export namespace kravidb::storage {

class StorageEngine final {
  public:
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  explicit StorageEngine(std::size_t page_size = default_page_size,
                         int degree = index::recommended_degree)
      : records_{page_size}, btree_{degree}, table_{records_, btree_} {}

  [[nodiscard]] auto records() noexcept -> RecordStore& { return records_; }
  [[nodiscard]] auto index() noexcept -> index::Index& { return btree_; }
  [[nodiscard]] auto table() noexcept -> Table& { return table_; }

  private:
  PageManager records_;
  index::BTree btree_;
  Table table_;
};

}  // namespace kravidb::storage

export void init_storage();
