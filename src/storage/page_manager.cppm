/// \file
/// \brief Paged implementation of the record store (`PageManager`).

export module storage.page_manager;

import std;
export import storage.record_store;
export import storage.row_id;

namespace kravidb::storage::detail {

/// \brief Slot directory entry: offset and length inside the buffer.
struct SlotEntry {
  /// \brief Offset from the start of the page to the start of the record.
  std::uint16_t offset{0};

  /// \brief Length in bytes of the stored record.
  std::uint16_t length{0};
};

/// \brief Accounting cost per slot: size of one `SlotEntry` (4 bytes).
inline constexpr std::size_t slot_overhead = sizeof(SlotEntry);

/// \brief In-memory physical page with a contiguous slotted-page layout.
///
/// The page holds a single contiguous byte buffer (`data_`):
/// - The slot directory (`SlotEntry`, 4 bytes each) grows forward from byte 0.
/// - Tuple records are written at the end of the buffer and grow backwards.
/// - Free space is the gap between the end of the directory and the start of the last record.
///
/// It is not exported; it is an implementation detail of `PageManager`.
class Page {
  public:
  /// \brief Creates a page with the given capacity and reserves its contiguous buffer.
  /// \param capacity Total capacity of the page, in bytes.
  explicit Page(std::size_t capacity)
      : capacity_{capacity}, free_space_end_{capacity}, data_(capacity, std::byte{0}) {}

  /// \brief Checks whether a record of this size fits in the page.
  /// \param record_size Size of the record, without the slot accounting.
  [[nodiscard]] auto can_fit(std::size_t record_size) const noexcept -> bool {
    return used_bytes_ + record_size + slot_overhead <= capacity_;
  }

  /// \brief Appends a record at the end of the page and records its slot in the directory.
  /// \param record Record bytes.
  /// \return The slot assigned to the record.
  [[nodiscard]] auto append(std::span<const std::byte> record) -> SlotID {
    const auto record_size = record.size();
    const auto target_offset = free_space_end_ - record_size;
    std::ranges::copy(record, data_.begin() + static_cast<std::ptrdiff_t>(target_offset));

    const SlotEntry entry{
        .offset = static_cast<std::uint16_t>(target_offset),
        .length = static_cast<std::uint16_t>(record_size),
    };
    const auto slot_byte_offset = slot_count_ * slot_overhead;
    const auto entry_bytes = std::bit_cast<std::array<std::byte, sizeof(SlotEntry)>>(entry);
    std::ranges::copy(entry_bytes, data_.begin() + static_cast<std::ptrdiff_t>(slot_byte_offset));

    free_space_end_ = target_offset;
    used_bytes_ += record_size + slot_overhead;
    const auto assigned_slot = static_cast<SlotID>(slot_count_);
    ++slot_count_;
    return assigned_slot;
  }

  /// \brief Returns the bytes of a slot by reading its descriptor in the directory.
  /// \param slot_id Slot being queried.
  /// \return The stored bytes, or `std::nullopt` if the slot does not exist.
  [[nodiscard]] auto slot(SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> {
    if (slot_id >= slot_count_) {
      return std::nullopt;
    }
    const auto slot_byte_offset = static_cast<std::size_t>(slot_id) * slot_overhead;
    std::array<std::byte, sizeof(SlotEntry)> raw{};
    std::ranges::copy(
        std::span<const std::byte>{data_}.subspan(slot_byte_offset, sizeof(SlotEntry)),
        raw.begin());
    const auto entry = std::bit_cast<SlotEntry>(raw);
    return std::span<const std::byte>{data_}.subspan(entry.offset, entry.length);
  }

  /// \brief Number of occupied slots in the page.
  [[nodiscard]] auto slot_count() const noexcept -> std::size_t { return slot_count_; }

  /// \brief Bytes consumed by records and slot accounting.
  [[nodiscard]] auto used_bytes() const noexcept -> std::size_t { return used_bytes_; }

  /// \brief Read-only access to the contiguous physical buffer of the page.
  [[nodiscard]] auto raw_bytes() const noexcept -> std::span<const std::byte> { return data_; }

  private:
  /// \brief Total capacity of the page, in bytes.
  std::size_t capacity_;

  /// \brief Bytes consumed so far (slots + tuples).
  std::size_t used_bytes_{0};

  /// \brief Number of slots currently occupied.
  std::size_t slot_count_{0};

  /// \brief Lower bound of the data region (moves down with every insertion).
  std::size_t free_space_end_;

  /// \brief Contiguous memory buffer of the page.
  std::vector<std::byte> data_;
};

}  // namespace kravidb::storage::detail

export namespace kravidb::storage {

/// \brief Default page size, in bytes (4 KiB).
inline constexpr std::size_t default_page_size = 4096;

/// \brief Paged record store, growing only at the end.
///
/// Records are inserted into the last page or into a new one; pages are never
/// reallocated, so the bytes returned by `read` and `slot_at` remain valid
/// after later insertions.
class PageManager final : public RecordStore {
  public:
  /// \brief Creates the store with the given page size.
  /// \param page_size Page size, in bytes.
  /// \throws std::invalid_argument If the size does not fit in `SlotEntry::offset`.
  explicit PageManager(std::size_t page_size = default_page_size) : page_size_{page_size} {
    if (page_size > std::numeric_limits<std::uint16_t>::max()) {
      throw std::invalid_argument{"PageManager: page size exceeds SlotEntry"};
    }
  }

  /// \brief Inserts a record.
  /// \param record Bytes of the record to store.
  /// \return The assigned `RowID`.
  /// \note Throws `std::length_error` if the record does not fit even in an empty page.
  [[nodiscard]] auto insert(std::span<const std::byte> record) -> RowID override;

  /// \brief Reads a record by its locator, accounting the I/O.
  /// \param row_id Locator of the record.
  /// \return The record bytes, or `std::nullopt` if it does not exist.
  [[nodiscard]] auto read(RowID row_id) const -> std::optional<std::span<const std::byte>> override;

  /// \brief Reads a record by explicit location, without accounting I/O.
  /// \param page_id Target page.
  /// \param slot_id Slot within the page.
  /// \return The record bytes, or `std::nullopt` if the location does not exist.
  [[nodiscard]] auto slot_at(PageID page_id, SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> override;

  /// \brief Number of pages in the store.
  [[nodiscard]] auto page_count() const noexcept -> std::size_t override { return pages_.size(); }

  /// \brief Number of occupied slots in a page.
  /// \param page_id Page being queried.
  [[nodiscard]] auto slot_count(PageID page_id) const noexcept -> std::size_t override;

  /// \brief Configured page size, in bytes.
  [[nodiscard]] auto page_size() const noexcept -> std::size_t override { return page_size_; }

  /// \brief Accumulated I/O statistics.
  [[nodiscard]] auto stats() const noexcept -> const PageIOStats& override { return stats_; }

  /// \brief Resets the I/O statistics to zero.
  void reset_stats() noexcept override { stats_ = PageIOStats{}; }

  private:
  /// \brief Configured page size, in bytes.
  std::size_t page_size_;

  /// \brief Pages in creation order.
  std::vector<detail::Page> pages_;

  /// \brief I/O counters, mutable so they can be updated from `read` (const).
  mutable PageIOStats stats_{};
};

}  // namespace kravidb::storage
