module storage.heap_scan;

import std;

namespace kravidb::storage {

HeapScan::HeapScan(BufferPool& pool, const PageSource& source, RelFileNode relation)
    : pool_{pool}, relation_{relation}, blocks_{source.block_count(relation, Fork::main)} {}

auto HeapScan::stats() const noexcept -> HeapScanStats {
  return stats_;
}

auto HeapScan::next() -> std::optional<TupleTableSlot> {
  while (block_ < blocks_) {
    if (!guard_) {
      guard_ = pool_.read(BufferTag{relation_, Fork::main, block_});
      guard_->lock_shared();
      ++stats_.page_fetches;
      if (!guard_->page().validate()) {
        guard_.reset();
        // Imported inheritance is not visible to some clang-tidy versions.
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw SqlStateError{"XX001", std::format("invalid heap page: relation {} block {}",
                                                 relation_.relation, block_)};
      }
    }
    const auto& page = guard_->page();
    while (slot_ < page.item_count()) {
      const auto current = slot_++;
      const auto item = page.item(current);
      ++stats_.items_examined;
      if (item.flags() == ItemIdFlag::unused || item.flags() == ItemIdFlag::dead) {
        continue;
      }
      if (item.flags() == ItemIdFlag::redirect) {
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw SqlStateError{"0A000", "readonly heap scan does not resolve redirect items"};
      }
      const auto tuple = HeapTuple::from_bytes(page.bytes().subspan(item.offset(), item.length()));
      const ItemPointerData tid{.block = block_, .offset = current};
      if (!tuple || tuple->header().t_xmin == 0 || tuple->header().t_ctid != tid) {
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw SqlStateError{"XX001", std::format("invalid heap tuple: relation {} block {} slot {}",
                                                 relation_.relation, block_, current)};
      }
      const auto header = tuple->header();
      if (header.t_xmax != 0) {
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw SqlStateError{"0A000", "readonly heap scan requires a published immutable heap"};
      }
      ++stats_.rows_returned;
      return TupleTableSlot{.tid = tid, .header = header, .values = tuple->fields()};
    }
    guard_.reset();
    slot_ = 0;
    ++block_;
  }
  return std::nullopt;
}

}  // namespace kravidb::storage
