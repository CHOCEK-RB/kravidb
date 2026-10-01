export module storage.row_id;

import std;

export namespace kravidb::storage {

using PageID = std::uint32_t;
using SlotID = std::uint32_t;
using RowID = std::uint64_t;

// Un RowID de 64 bits empaqueta la ubicacion fisica de una tupla:
//   bits 63..32 -> PageID (pagina)
//   bits 31..0  -> SlotID (posicion dentro de la pagina)
inline constexpr unsigned slot_bits = 32;
inline constexpr RowID slot_mask = 0xFFFF'FFFFULL;

struct RowLocation {
  PageID page_id{0};
  SlotID slot_id{0};

  [[nodiscard]] bool operator==(const RowLocation&) const = default;
};

[[nodiscard]] constexpr RowID encode_row_id(RowLocation location) noexcept {
  return (static_cast<RowID>(location.page_id) << slot_bits) | static_cast<RowID>(location.slot_id);
}

[[nodiscard]] constexpr RowLocation decode_row_id(RowID row_id) noexcept {
  return RowLocation{
      .page_id = static_cast<PageID>(row_id >> slot_bits),
      .slot_id = static_cast<SlotID>(row_id & slot_mask),
  };
}

}  // namespace kravidb::storage
