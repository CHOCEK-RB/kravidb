export module storage.table;

import std;
export import storage.record_store;
export import storage.tuple;
export import index.index;

export namespace kravidb::storage {

class Table final {
  public:
  Table(RecordStore& records, index::Index& index) : records_{records}, index_{index} {}

  void insert(const Tuple& tuple);
  [[nodiscard]] auto find_by_index(index::Key key) const -> std::optional<Tuple>;
  [[nodiscard]] auto scan_all() const -> std::vector<Tuple>;

  private:
  // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
  RecordStore& records_;
  index::Index& index_;
  // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
};

}  // namespace kravidb::storage
