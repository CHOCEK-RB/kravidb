export module storage.page_manager;

import std;
export import storage.record_store;
export import storage.row_id;

namespace kravidb::storage::detail {

inline constexpr std::size_t slot_overhead = sizeof(std::uint32_t);

class Page {
  public:
  explicit Page(std::size_t capacity) : capacity_{capacity} {}

  [[nodiscard]] auto can_fit(std::size_t record_size) const noexcept -> bool {
    return used_bytes_ + record_size + slot_overhead <= capacity_;
  }

  [[nodiscard]] auto append(std::span<const std::byte> record) -> SlotID {
    slots_.emplace_back(record.begin(), record.end());
    used_bytes_ += record.size() + slot_overhead;
    return static_cast<SlotID>(slots_.size() - 1);
  }

  [[nodiscard]] auto slot(SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> {
    if (slot_id >= slots_.size()) {
      return std::nullopt;
    }
    return std::span<const std::byte>{slots_.at(slot_id)};
  }

  [[nodiscard]] auto slot_count() const noexcept -> std::size_t { return slots_.size(); }
  [[nodiscard]] auto used_bytes() const noexcept -> std::size_t { return used_bytes_; }

  private:
  std::size_t capacity_;
  std::size_t used_bytes_{0};
  std::vector<std::vector<std::byte>> slots_;
};

}  // namespace kravidb::storage::detail

export namespace kravidb::storage {

inline constexpr std::size_t default_page_size = 4096;

class PageManager final : public RecordStore {
  public:
  explicit PageManager(std::size_t page_size = default_page_size) : page_size_{page_size} {}

  [[nodiscard]] auto insert(std::span<const std::byte> record) -> RowID override;

  [[nodiscard]] auto read(RowID row_id) const -> std::optional<std::span<const std::byte>> override;

  [[nodiscard]] auto slot_at(PageID page_id, SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> override;

  [[nodiscard]] auto page_count() const noexcept -> std::size_t override { return pages_.size(); }
  [[nodiscard]] auto slot_count(PageID page_id) const noexcept -> std::size_t override;
  [[nodiscard]] auto page_size() const noexcept -> std::size_t override { return page_size_; }

  [[nodiscard]] auto stats() const noexcept -> const PageIOStats& override { return stats_; }
  void reset_stats() noexcept override { stats_ = PageIOStats{}; }

  private:
  std::size_t page_size_;
  std::vector<detail::Page> pages_;
  mutable PageIOStats stats_{};
};

}  // namespace kravidb::storage
