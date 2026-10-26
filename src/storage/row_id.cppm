/// \file
/// \brief Record locators: packing and unpacking of `RowID`.

export module storage.row_id;

import std;

export namespace kravidb::storage {

/// \brief Page identifier (occupies the high 32 bits of a `RowID`).
using PageID = std::uint32_t;

/// \brief Slot identifier within a page (low 32 bits of a `RowID`).
using SlotID = std::uint32_t;

/// \brief Physical locator of a record: page and slot packed into 64 bits.
using RowID = std::uint64_t;

/// \brief Number of bits reserved for `slot_id` (the low ones).
inline constexpr unsigned slot_bits = 32;

/// \brief Mask that isolates the low bits corresponding to `slot_id`.
inline constexpr RowID slot_mask = 0xFFFF'FFFFULL;

/// \brief Logical location of a record inside the store.
struct RowLocation {
  /// \brief Page that contains the record.
  PageID page_id{0};

  /// \brief Occupied slot within the page.
  SlotID slot_id{0};

  /// \brief Compares two locations field by field.
  [[nodiscard]] auto operator==(const RowLocation&) const -> bool = default;
};

/// \brief Packs a location into a single `RowID`.
/// \param location Page and slot to encode.
/// \return The `RowID` with the page in the high 32 bits and the slot in the low ones.
[[nodiscard]] constexpr auto encode_row_id(RowLocation location) noexcept -> RowID {
  return (static_cast<RowID>(location.page_id) << slot_bits) | static_cast<RowID>(location.slot_id);
}

/// \brief Splits a `RowID` into its page and slot components.
/// \param row_id Locator to decode.
/// \return The location rebuilt from the identifier.
[[nodiscard]] constexpr auto decode_row_id(RowID row_id) noexcept -> RowLocation {
  return RowLocation{
      .page_id = static_cast<PageID>(row_id >> slot_bits),
      .slot_id = static_cast<SlotID>(row_id & slot_mask),
  };
}

}  // namespace kravidb::storage
