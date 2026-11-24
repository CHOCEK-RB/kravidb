/// \file
/// \brief Forward-only scan of a published, immutable heap relation.

export module storage.heap_scan;

import std;
export import storage.buffer_pool;
export import storage.heap_tuple;

export namespace kravidb::storage {

/// \brief Owned row returned by a scan; it outlives the buffer pin.
struct TupleTableSlot {
  ItemPointerData tid{};
  HeapTupleHeader header{};
  std::vector<std::optional<Field>> values;
};

/// \brief Logical I/O and tuple counters for one scan.
///
/// `page_fetches` counts buffer read requests, not physical disk reads. Use
/// BufferPool::stats() for cache hits/misses and physical page loads.
struct HeapScanStats {
  std::size_t page_fetches{0};
  std::size_t items_examined{0};
  std::size_t rows_returned{0};
};

/// \brief Scans a published heap in block/slot order using shared buffer locks.
///
/// The caller must keep the relation immutable for the entire scan. This is
/// deliberately not an MVCC scan: normal tuples in a committed generation are
/// visible, and version chains/deletion markers are rejected rather than
/// guessed at. TIDs use zero-based slots, like Page and the loader/catalog.
class HeapScan {
  public:
  HeapScan(BufferPool& pool, const PageSource& source, RelFileNode relation);
  ~HeapScan() = default;
  HeapScan(const HeapScan&) = delete;
  auto operator=(const HeapScan&) -> HeapScan& = delete;
  HeapScan(HeapScan&&) = delete;
  auto operator=(HeapScan&&) -> HeapScan& = delete;

  /// \brief Returns the next owned row, or nullopt at end of relation.
  /// \throws SqlStateError (`XX001`) for malformed pages/tuples and (`0A000`)
  /// for heaps requiring transaction visibility or redirect resolution.
  [[nodiscard]] auto next() -> std::optional<TupleTableSlot>;
  /// \brief Counters accumulated by this scan.
  [[nodiscard]] auto stats() const noexcept -> HeapScanStats;

  private:
  BufferPool& pool_;
  RelFileNode relation_;
  BlockNumber blocks_{0};
  BlockNumber block_{0};
  std::uint16_t slot_{0};
  std::unique_ptr<PageGuard> guard_;
  HeapScanStats stats_;
};

}  // namespace kravidb::storage
