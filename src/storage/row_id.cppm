export module storage.row_id;

import std;

export namespace kravidb::storage {

using PageID = std::uint32_t;
using SlotID = std::uint32_t;
using RowID = std::uint64_t;

inline constexpr unsigned slot_bits = 32;
inline constexpr RowID slot_mask = 0xFFFF'FFFFULL;

struct RowLocation {
  PageID page_id{0};
  SlotID slot_id{0};

  [[nodiscard]] auto operator==(const RowLocation&) const -> bool = default;
};

[[nodiscard]] constexpr auto encode_row_id(RowLocation location) noexcept -> RowID {
  return (static_cast<RowID>(location.page_id) << slot_bits) | static_cast<RowID>(location.slot_id);
}

[[nodiscard]] constexpr auto decode_row_id(RowID row_id) noexcept -> RowLocation {
  return RowLocation{
      .page_id = static_cast<PageID>(row_id >> slot_bits),
      .slot_id = static_cast<SlotID>(row_id & slot_mask),
  };
}

}  // namespace kravidb::storage
