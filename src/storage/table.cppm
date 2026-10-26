/// \file
/// \brief Table facade: combines a record store and an index.

export module storage.table;

import std;
export import storage.record_store;
export import storage.tuple;
export import storage.schema;
export import index.index;

export namespace kravidb::storage {

/// \brief Result of an insertion into the table.
struct InsertResult {
  /// \brief Locator of the stored record.
  RowID row_id{0};

  /// \brief Structural trace of the insertion in the index.
  index::InsertStats index_stats{};
};

/// \brief Table that persists tuples and indexes them by primary key.
///
/// The first field of every tuple must be a 64-bit integer and acts as the
/// primary key (PK). The table rejects duplicate PKs. It owns neither the store
/// nor the index: it receives them by reference at construction. It optionally
/// receives a `Schema` to validate fields.
class Table final {
  public:
  /// \brief Builds the table over an existing store and index, with an optional schema.
  /// \param records Store where the tuples are persisted (not owned).
  /// \param index Index that associates the PK with the `RowID` (not owned).
  /// \param schema Optional relational schema for tuple validation.
  Table(RecordStore& records, index::Index& index, Schema schema = Schema{})
      : records_{records}, index_{index}, schema_{std::move(schema)} {}

  /// \brief Relational schema of the table.
  [[nodiscard]] auto schema() const noexcept -> const Schema& { return schema_; }

  /// \brief Inserts a tuple and indexes it by its primary key.
  /// \param tuple Tuple to insert; the first field is the PK.
  /// \return The locator of the record and the trace of the insertion in the index.
  /// \note Throws `std::invalid_argument` if the tuple is empty, does not match
  /// the schema, the PK field is not of type `Int`, or the PK already exists.
  auto insert(const Tuple& tuple) -> InsertResult;

  /// \brief Searches a tuple by its primary key.
  /// \param key Primary key being searched.
  /// \return The found tuple, or `std::nullopt` if it does not exist.
  [[nodiscard]] auto find_by_index(index::Key key) const -> std::optional<Tuple>;

  /// \brief Walks and deserializes every tuple in the store.
  /// \return The tuples found, in page and slot order.
  /// \note Uses `slot_at`, so it does not increment the I/O statistics.
  [[nodiscard]] auto scan_all() const -> std::vector<Tuple>;

  private:
  // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
  /// \brief Record store (non-owning reference).
  RecordStore& records_;

  /// \brief Primary key index (non-owning reference).
  index::Index& index_;
  // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

  /// \brief Relational schema of the table.
  Schema schema_;
};

}  // namespace kravidb::storage
