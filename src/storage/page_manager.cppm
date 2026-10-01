export module storage.page_manager;

import std;
export import storage.row_id;

export namespace kravidb::storage {

inline constexpr std::size_t default_page_size = 4096;
// Bytes que ocupa cada entrada del directorio de slots (longitud del registro).
inline constexpr std::size_t slot_overhead = sizeof(std::uint32_t);

struct PageIOStats {
  std::size_t page_reads{0};
  std::size_t page_writes{0};
};

// Pagina en memoria: cada slot guarda un registro serializado (por ejemplo, una Tuple).
class Page {
  public:
  explicit Page(std::size_t capacity) : capacity_{capacity} {}

  [[nodiscard]] bool can_fit(std::size_t record_size) const noexcept {
    return used_bytes_ + record_size + slot_overhead <= capacity_;
  }

  [[nodiscard]] SlotID append(std::span<const std::byte> record) {
    slots_.emplace_back(record.begin(), record.end());
    used_bytes_ += record.size() + slot_overhead;
    return static_cast<SlotID>(slots_.size() - 1);
  }

  [[nodiscard]] std::optional<std::span<const std::byte>> slot(SlotID slot_id) const noexcept {
    if (slot_id >= slots_.size()) {
      return std::nullopt;
    }
    return std::span<const std::byte>{slots_[slot_id]};
  }

  [[nodiscard]] std::size_t slot_count() const noexcept { return slots_.size(); }
  [[nodiscard]] std::size_t used_bytes() const noexcept { return used_bytes_; }

  private:
  std::size_t capacity_;
  std::size_t used_bytes_{0};
  std::vector<std::vector<std::byte>> slots_;
};

class PageManager {
  public:
  explicit PageManager(std::size_t page_size = default_page_size) : page_size_{page_size} {}

  // Escribe el registro en la ultima pagina (o en una nueva si no cabe) y devuelve su RowID.
  // Lanza std::length_error si el registro no cabe ni en una pagina vacia.
  [[nodiscard]] RowID insert(std::span<const std::byte> record) {
    if (record.size() + slot_overhead > page_size_) {
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

  // Acceso O(1): el RowID indica directamente la pagina y el slot.
  // El span devuelto sigue siendo valido aunque se inserten mas registros.
  [[nodiscard]] std::optional<std::span<const std::byte>> read(RowID row_id) {
    const RowLocation location = decode_row_id(row_id);
    if (location.page_id >= pages_.size()) {
      return std::nullopt;
    }
    ++stats_.page_reads;
    return pages_[location.page_id].slot(location.slot_id);
  }

  [[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }
  [[nodiscard]] std::size_t page_size() const noexcept { return page_size_; }
  [[nodiscard]] const Page& page(PageID page_id) const { return pages_.at(page_id); }

  [[nodiscard]] const PageIOStats& stats() const noexcept { return stats_; }
  void reset_stats() noexcept { stats_ = PageIOStats{}; }

  private:
  std::size_t page_size_;
  std::vector<Page> pages_;
  PageIOStats stats_{};
};

}  // namespace kravidb::storage
