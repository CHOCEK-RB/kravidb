module storage.page_manager;

import std;
import storage.row_id;

namespace kravidb::storage {

auto PageManager::insert(std::span<const std::byte> record) -> RowID {
  if (record.size() + detail::slot_overhead > page_size_) {
    // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
    throw std::length_error{"PageManager: el registro excede el tamano de pagina"};
  }
  if (pages_.empty() || !pages_.back().can_fit(record.size())) {
    pages_.emplace_back(page_size_);
  }
  const auto page_id = static_cast<PageID>(pages_.size() - 1);
  const SlotID slot_id = pages_.back().append(record);
  ++stats_.page_writes;
  return encode_row_id(RowLocation{.page_id = page_id, .slot_id = slot_id});
}

auto PageManager::read(RowID row_id) const -> std::optional<std::span<const std::byte>> {
  const RowLocation location = decode_row_id(row_id);
  if (location.page_id >= pages_.size()) {
    return std::nullopt;
  }
  ++stats_.page_reads;
  return pages_.at(location.page_id).slot(location.slot_id);
}

auto PageManager::slot_count(PageID page_id) const noexcept -> std::size_t {
  if (page_id >= pages_.size()) {
    return 0;
  }
  return pages_.at(page_id).slot_count();
}

auto PageManager::slot_at(PageID page_id, SlotID slot_id) const noexcept
    -> std::optional<std::span<const std::byte>> {
  if (page_id >= pages_.size()) {
    return std::nullopt;
  }
  return pages_.at(page_id).slot(slot_id);
}

}  // namespace kravidb::storage
