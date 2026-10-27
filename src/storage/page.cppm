/// \file
/// \brief 8 KiB page layout: header, item ids, checksum and validation.

export module storage.page;

import std;
import storage.relation_file;

export namespace kravidb::storage {

/// \brief Layout version stored in `pd_pagesize_version`.
inline constexpr std::uint16_t page_layout_version = 1;

/// \brief Size of the page header in bytes.
inline constexpr std::size_t page_header_size = 24;

/// \brief Page flag: every tuple on the page is visible to all snapshots.
inline constexpr std::uint16_t pd_all_visible = 0x0001;

/// \brief Page flag: the line table has unused entries that can be reused.
inline constexpr std::uint16_t pd_has_free_lines = 0x0002;

/// \brief Page flag: the page has no room for another tuple.
inline constexpr std::uint16_t pd_page_full = 0x0004;

/// \brief On-disk page header.
struct PageHeader {
  std::uint64_t pd_lsn{0};
  std::uint16_t pd_checksum{0};
  std::uint16_t pd_flags{0};
  std::uint16_t pd_lower{0};
  std::uint16_t pd_upper{0};
  std::uint16_t pd_special{0};
  std::uint16_t pd_pagesize_version{0};
  std::uint32_t pd_prune_xid{0};
};

static_assert(sizeof(PageHeader) == page_header_size);

/// \brief State of a line pointer.
enum class ItemIdFlag : std::uint8_t {
  unused = 0,
  normal = 1,
  redirect = 2,
  dead = 3,
};

/// \brief Line pointer: 15-bit offset, 2-bit flag and 15-bit length.
class ItemId {
  public:
  static constexpr std::uint32_t offset_mask{0x7FFFU};
  static constexpr std::uint32_t flags_mask{0x0003U};
  static constexpr std::uint32_t length_mask{0x7FFFU};
  static constexpr std::uint32_t flags_shift{15};
  static constexpr std::uint32_t length_shift{17};
  static constexpr std::uint32_t flags_bits_mask{flags_mask << flags_shift};
  static constexpr std::uint32_t length_bits_mask{length_mask << length_shift};

  /// \brief Offset of the tuple inside the page.
  [[nodiscard]] auto offset() const noexcept -> std::uint16_t;
  /// \brief Current state of the item id.
  [[nodiscard]] auto flags() const noexcept -> ItemIdFlag;
  /// \brief Length of the tuple in bytes.
  [[nodiscard]] auto length() const noexcept -> std::uint16_t;

  /// \brief Sets the tuple offset.
  auto set_offset(std::uint16_t value) noexcept -> void;
  /// \brief Sets the item id state.
  auto set_flags(ItemIdFlag value) noexcept -> void;
  /// \brief Sets the tuple length.
  auto set_length(std::uint16_t value) noexcept -> void;

  /// \brief Packed 32-bit representation.
  [[nodiscard]] auto raw() const noexcept -> std::uint32_t;
  /// \brief Builds an item id from its packed representation.
  [[nodiscard]] static auto from_raw(std::uint32_t bits) noexcept -> ItemId;

  private:
  std::uint32_t bits_{0};
};

static_assert(sizeof(ItemId) == 4);
static_assert(std::is_trivially_copyable_v<ItemId>);

}  // namespace kravidb::storage

namespace kravidb::storage::detail {

/// \brief FNV-1a offset basis used by the page checksum.
inline constexpr std::uint32_t checksum_offset_basis{0x811C9DC5U};
/// \brief FNV-1a prime used by the page checksum.
inline constexpr std::uint32_t checksum_prime{0x01000193U};
/// \brief Byte offset of `pd_checksum` in the page, fixed by the on-disk layout.
inline constexpr std::size_t checksum_field_offset{8};
/// \brief Size of `pd_checksum` in bytes.
inline constexpr std::size_t checksum_field_size{sizeof(std::uint16_t)};
/// \brief Byte offset where the line table starts.
inline constexpr std::size_t item_area_offset{sizeof(PageHeader)};
/// \brief Shift that keeps the high half of the 32-bit hash.
inline constexpr std::uint32_t checksum_fold_shift{16};
/// \brief Mask that keeps the low half of the 32-bit hash.
inline constexpr std::uint32_t checksum_fold_mask{0xFFFFU};

}  // namespace kravidb::storage::detail

export namespace kravidb::storage {

/// \brief Fixed 8 KiB page: header, line pointers, free space and checksum.
///
/// The page is a value type over a byte buffer. Field access goes through
/// `header()`, `set_header()`, `item()` and `add_item()`; those helpers copy
/// with `memcpy`, so the buffer is never reinterpreted as a struct.
class Page {
  public:
  /// \brief Page size in bytes.
  static constexpr std::size_t size = physical_page_size;
  /// \brief Maximum number of item ids in an empty page.
  static constexpr std::size_t max_items = (size - sizeof(PageHeader)) / sizeof(ItemId);

  /// \brief Builds an empty, checksummed page.
  Page() noexcept;

  /// \brief Copies serialized bytes into a page without validating them.
  [[nodiscard]] static auto from_bytes(std::span<const std::byte, size> input) -> Page;

  /// \brief Serialized page bytes.
  [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte, size>;
  /// \brief Mutable page bytes; call `set_checksum()` after editing.
  [[nodiscard]] auto mutable_bytes() noexcept -> std::span<std::byte, size>;

  /// \brief Header read by value.
  [[nodiscard]] auto header() const noexcept -> PageHeader;
  /// \brief Replaces the header.
  auto set_header(const PageHeader& value) noexcept -> void;

  /// \brief Number of item ids in the line table.
  [[nodiscard]] auto item_count() const noexcept -> std::uint16_t;
  /// \brief Item id at `slot`; throws `std::out_of_range` if it does not exist.
  [[nodiscard]] auto item(std::uint16_t slot) const -> ItemId;
  /// \brief Appends an item id and grows `pd_lower`.
  ///
  /// Throws `std::length_error` when the line table cannot grow.
  auto add_item(ItemId value) -> std::uint16_t;

  /// \brief Bytes between `pd_lower` and `pd_upper`.
  [[nodiscard]] auto free_space() const noexcept -> std::size_t;

  /// \brief Checks the header invariants, the item ids and the checksum.
  [[nodiscard]] auto validate() const noexcept -> bool;

  /// \brief Computes the checksum with the `pd_checksum` bytes treated as zero.
  [[nodiscard]] auto checksum() const noexcept -> std::uint16_t;
  /// \brief Stores the current checksum in the header.
  auto set_checksum() noexcept -> void;
  /// \brief True when `pd_checksum` matches the page contents.
  [[nodiscard]] auto verify_checksum() const noexcept -> bool;

  private:
  [[nodiscard]] auto raw_item(std::uint16_t slot) const noexcept -> ItemId;
  auto write_item(std::uint16_t slot, ItemId value) noexcept -> void;

  alignas(std::uint64_t) std::array<std::byte, size> bytes_{};
};

static_assert(sizeof(Page) == Page::size);
static_assert(alignof(Page) >= alignof(PageHeader));
static_assert(std::endian::native == std::endian::little);

inline auto ItemId::offset() const noexcept -> std::uint16_t {
  return static_cast<std::uint16_t>(bits_ & offset_mask);
}

inline auto ItemId::flags() const noexcept -> ItemIdFlag {
  return static_cast<ItemIdFlag>((bits_ >> flags_shift) & flags_mask);
}

inline auto ItemId::length() const noexcept -> std::uint16_t {
  return static_cast<std::uint16_t>((bits_ >> length_shift) & length_mask);
}

inline auto ItemId::set_offset(std::uint16_t value) noexcept -> void {
  bits_ = (bits_ & ~offset_mask) | (static_cast<std::uint32_t>(value) & offset_mask);
}

inline auto ItemId::set_flags(ItemIdFlag value) noexcept -> void {
  const auto shifted = static_cast<std::uint32_t>(value) << flags_shift;
  bits_ = (bits_ & ~flags_bits_mask) | (shifted & flags_bits_mask);
}

inline auto ItemId::set_length(std::uint16_t value) noexcept -> void {
  const auto shifted = static_cast<std::uint32_t>(value) << length_shift;
  bits_ = (bits_ & ~length_bits_mask) | (shifted & length_bits_mask);
}

inline auto ItemId::raw() const noexcept -> std::uint32_t {
  return bits_;
}

inline auto ItemId::from_raw(std::uint32_t bits) noexcept -> ItemId {
  ItemId item;
  item.bits_ = bits;
  return item;
}

inline Page::Page() noexcept {
  PageHeader page_header{};
  page_header.pd_lower = static_cast<std::uint16_t>(sizeof(PageHeader));
  page_header.pd_upper = static_cast<std::uint16_t>(size);
  page_header.pd_special = static_cast<std::uint16_t>(size);
  page_header.pd_pagesize_version = static_cast<std::uint16_t>(size | page_layout_version);
  set_header(page_header);
  set_checksum();
}

inline auto Page::from_bytes(std::span<const std::byte, size> input) -> Page {
  Page page;
  std::ranges::copy(input, page.bytes_.begin());
  return page;
}

inline auto Page::bytes() const noexcept -> std::span<const std::byte, size> {
  return std::span<const std::byte, size>{bytes_};
}

inline auto Page::mutable_bytes() noexcept -> std::span<std::byte, size> {
  return std::span<std::byte, size>{bytes_};
}

inline auto Page::header() const noexcept -> PageHeader {
  PageHeader value{};
  std::memcpy(&value, bytes_.data(), sizeof(PageHeader));
  return value;
}

inline auto Page::set_header(const PageHeader& value) noexcept -> void {
  std::memcpy(bytes_.data(), &value, sizeof(PageHeader));
}

inline auto Page::item_count() const noexcept -> std::uint16_t {
  const auto page_header = header();
  if (page_header.pd_lower < sizeof(PageHeader) || page_header.pd_lower > size) {
    return 0;
  }
  return static_cast<std::uint16_t>((page_header.pd_lower - sizeof(PageHeader)) / sizeof(ItemId));
}

inline auto Page::raw_item(std::uint16_t slot) const noexcept -> ItemId {
  std::uint32_t bits{0};
  const auto offset = detail::item_area_offset + (static_cast<std::size_t>(slot) * sizeof(ItemId));
  const auto field = bytes().subspan(offset, sizeof(std::uint32_t));
  std::memcpy(&bits, field.data(), sizeof(bits));
  return ItemId::from_raw(bits);
}

inline auto Page::write_item(std::uint16_t slot, ItemId value) noexcept -> void {
  const auto bits = value.raw();
  const auto offset = detail::item_area_offset + (static_cast<std::size_t>(slot) * sizeof(ItemId));
  const auto field = mutable_bytes().subspan(offset, sizeof(std::uint32_t));
  std::memcpy(field.data(), &bits, sizeof(bits));
}

inline auto Page::item(std::uint16_t slot) const -> ItemId {
  if (slot >= item_count()) {
    throw std::out_of_range{"item slot out of range"};
  }
  return raw_item(slot);
}

inline auto Page::add_item(ItemId value) -> std::uint16_t {
  auto page_header = header();
  if (page_header.pd_lower < sizeof(PageHeader) ||
      page_header.pd_lower + sizeof(ItemId) > page_header.pd_upper) {
    throw std::length_error{"page has no room for another item id"};
  }
  const auto slot = item_count();
  write_item(slot, value);
  page_header.pd_lower = static_cast<std::uint16_t>(page_header.pd_lower + sizeof(ItemId));
  set_header(page_header);
  return slot;
}

inline auto Page::free_space() const noexcept -> std::size_t {
  const auto page_header = header();
  if (page_header.pd_lower > page_header.pd_upper) {
    return 0;
  }
  return static_cast<std::size_t>(page_header.pd_upper - page_header.pd_lower);
}

inline auto Page::validate() const noexcept -> bool {
  const auto page_header = header();
  if (page_header.pd_pagesize_version != (size | page_layout_version)) {
    return false;
  }
  if (page_header.pd_lower < sizeof(PageHeader) || page_header.pd_lower > page_header.pd_upper ||
      page_header.pd_upper > page_header.pd_special || page_header.pd_special > size) {
    return false;
  }
  if ((page_header.pd_lower - sizeof(PageHeader)) % sizeof(ItemId) != 0) {
    return false;
  }
  const auto count = item_count();
  if (count > max_items) {
    return false;
  }
  for (std::uint16_t slot = 0; slot < count; ++slot) {
    const auto entry = raw_item(slot);
    if (entry.flags() == ItemIdFlag::unused) {
      continue;
    }
    if (entry.length() == 0 || entry.offset() < page_header.pd_upper ||
        entry.offset() + entry.length() > page_header.pd_special) {
      return false;
    }
  }
  return verify_checksum();
}

inline auto Page::checksum() const noexcept -> std::uint16_t {
  std::uint32_t hash = detail::checksum_offset_basis;
  for (std::size_t index = 0; index < size; ++index) {
    auto value = bytes_.at(index);
    if (index >= detail::checksum_field_offset &&
        index < detail::checksum_field_offset + detail::checksum_field_size) {
      value = std::byte{0};
    }
    hash ^= std::to_integer<std::uint32_t>(value);
    hash *= detail::checksum_prime;
  }
  return static_cast<std::uint16_t>((hash >> detail::checksum_fold_shift) ^
                                    (hash & detail::checksum_fold_mask));
}

inline auto Page::set_checksum() noexcept -> void {
  auto page_header = header();
  page_header.pd_checksum = checksum();
  set_header(page_header);
}

inline auto Page::verify_checksum() const noexcept -> bool {
  return header().pd_checksum == checksum();
}

}  // namespace kravidb::storage
