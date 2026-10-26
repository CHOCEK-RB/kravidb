/// \file
/// \brief `RecordStore` contract and logical page I/O statistics.

export module storage.record_store;

import std;
export import storage.row_id;

export namespace kravidb::storage {

/// \brief Logical page I/O counters of the store.
///
/// They are only incremented by operations treated as real I/O (`insert` and
/// `read`). Reads through `slot_at` do not modify them, so a full sequential
/// scan may not account any reads.
struct PageIOStats {
  /// \brief Number of accumulated page reads.
  std::size_t page_reads{0};

  /// \brief Number of accumulated page writes.
  std::size_t page_writes{0};
};

/// \brief Abstract contract of a paged record store.
///
/// A record is an opaque byte sequence to which the store assigns a stable
/// `RowID`. The same byte collection can be retrieved both by that identifier
/// (`read`) and by its explicit page and slot location (`slot_at`). The class is
/// neither copyable nor movable: it is consumed by reference through the
/// interface.
class RecordStore {
  public:
  /// \brief Builds the contract, with no state of its own.
  RecordStore() = default;

  /// \brief Copy disabled: the contract is used by reference.
  RecordStore(const RecordStore&) = delete;

  /// \brief Copy assignment disabled.
  auto operator=(const RecordStore&) -> RecordStore& = delete;

  /// \brief Move disabled.
  RecordStore(RecordStore&&) = delete;

  /// \brief Move assignment disabled.
  auto operator=(RecordStore&&) -> RecordStore& = delete;

  /// \brief Virtual destructor.
  virtual ~RecordStore() = default;

  /// \brief Inserts a record and returns its locator.
  /// \param record Bytes of the record to store.
  /// \return The `RowID` assigned to the record.
  [[nodiscard]] virtual auto insert(std::span<const std::byte> record) -> RowID = 0;

  /// \brief Reads a record by its locator, accounting the I/O.
  /// \param row_id Locator previously returned by `insert`.
  /// \return The record bytes, or `std::nullopt` if the identifier does not exist.
  [[nodiscard]] virtual auto read(RowID row_id) const
      -> std::optional<std::span<const std::byte>> = 0;

  /// \brief Reads a record by its explicit location, without accounting I/O.
  /// \param page_id Target page.
  /// \param slot_id Slot within the page.
  /// \return The record bytes, or `std::nullopt` if the location does not exist.
  [[nodiscard]] virtual auto slot_at(PageID page_id, SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> = 0;

  /// \brief Number of pages that make up the store.
  [[nodiscard]] virtual auto page_count() const noexcept -> std::size_t = 0;

  /// \brief Number of occupied slots in a page.
  /// \param page_id Page being queried.
  /// \return The number of slots, or zero if the page does not exist.
  [[nodiscard]] virtual auto slot_count(PageID page_id) const noexcept -> std::size_t = 0;

  /// \brief Configured page size, in bytes.
  [[nodiscard]] virtual auto page_size() const noexcept -> std::size_t = 0;

  /// \brief Accumulated I/O statistics.
  [[nodiscard]] virtual auto stats() const noexcept -> const PageIOStats& = 0;

  /// \brief Resets the I/O statistics to zero.
  virtual void reset_stats() noexcept = 0;
};

}  // namespace kravidb::storage
