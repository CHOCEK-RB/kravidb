export module storage.record_store;

import std;
export import storage.row_id;

export namespace kravidb::storage {

struct PageIOStats {
  std::size_t page_reads{0};
  std::size_t page_writes{0};
};

class RecordStore {
  public:
  RecordStore() = default;
  RecordStore(const RecordStore&) = delete;
  auto operator=(const RecordStore&) -> RecordStore& = delete;
  RecordStore(RecordStore&&) = delete;
  auto operator=(RecordStore&&) -> RecordStore& = delete;
  virtual ~RecordStore() = default;

  [[nodiscard]] virtual auto insert(std::span<const std::byte> record) -> RowID = 0;

  [[nodiscard]] virtual auto read(RowID row_id) const
      -> std::optional<std::span<const std::byte>> = 0;

  [[nodiscard]] virtual auto slot_at(PageID page_id, SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> = 0;

  [[nodiscard]] virtual auto page_count() const noexcept -> std::size_t = 0;
  [[nodiscard]] virtual auto slot_count(PageID page_id) const noexcept -> std::size_t = 0;
  [[nodiscard]] virtual auto page_size() const noexcept -> std::size_t = 0;

  [[nodiscard]] virtual auto stats() const noexcept -> const PageIOStats& = 0;
  virtual void reset_stats() noexcept = 0;
};

}  // namespace kravidb::storage
