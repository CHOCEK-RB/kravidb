/// \file
/// \brief Physical heap tuple: header, null bitmap and payload.

export module storage.heap_tuple;

import std;
export import storage.tuple;
import storage.page;

export namespace kravidb::storage {

/// \brief SQLSTATE returned when a tuple does not fit in a page.
inline constexpr std::string_view sqlstate_tuple_too_large{"54000"};

/// \brief Error carrying a SQLSTATE code.
class SqlStateError : public std::runtime_error {
  public:
  /// \brief Builds an error with its SQLSTATE code.
  SqlStateError(std::string_view sqlstate, const std::string& message)
      : std::runtime_error{message}, sqlstate_{sqlstate} {}

  /// \brief SQLSTATE code associated with the error.
  [[nodiscard]] auto sqlstate() const noexcept -> std::string_view { return sqlstate_; }

  private:
  std::string sqlstate_;
};

/// \brief Physical location of a tuple version inside a relation.
struct [[gnu::packed]] ItemPointerData {
  std::uint32_t block{0};
  std::uint16_t offset{0};

  /// \brief Compares two item pointers by block and offset.
  [[nodiscard]] auto operator==(const ItemPointerData&) const -> bool = default;
};

static_assert(sizeof(ItemPointerData) == sizeof(std::uint32_t) + sizeof(std::uint16_t));
static_assert(std::is_trivially_copyable_v<ItemPointerData>);

/// \brief On-page tuple header (23 bytes plus alignment padding).
struct HeapTupleHeader {
  std::uint32_t t_xmin{0};
  std::uint32_t t_xmax{0};
  std::uint32_t t_cid{0};
  ItemPointerData t_ctid{};
  std::uint16_t t_infomask2{0};
  std::uint16_t t_infomask{0};
  std::uint8_t t_hoff{0};
};

static_assert(sizeof(HeapTupleHeader) == page_header_size);
static_assert(std::is_trivially_copyable_v<HeapTupleHeader>);

/// \brief Mask of the attribute count stored in `t_infomask2`.
inline constexpr std::uint16_t heap_natts_mask{0x07FF};

/// \brief `t_infomask` flag: at least one attribute is null.
inline constexpr std::uint16_t heap_has_null{0x0001};

/// \brief `t_infomask` flag: at least one attribute has variable width.
inline constexpr std::uint16_t heap_has_varwidth{0x0002};

/// \brief Number of bits stored in one null-bitmap byte.
inline constexpr std::size_t heap_bitmap_bits_per_byte{8};

/// \brief Mask used to index a bit inside a null-bitmap byte.
inline constexpr std::size_t heap_bitmap_bit_mask{heap_bitmap_bits_per_byte - 1};

/// \brief Alignment of the payload start inside a heap tuple.
inline constexpr std::size_t heap_payload_alignment{8};

/// \brief Physical tuple with a 24-byte header, optional null bitmap and payload.
///
/// The payload stores only non-null fields, each as `[u8 type][data]`. The
/// header records the attribute count, the layout flags and the offset where
/// the payload starts. The tuple is immutable once built.
class HeapTuple {
  public:
  /// \brief Largest tuple that fits in a page with its line pointer.
  static constexpr std::size_t max_size = Page::size - page_header_size - sizeof(ItemId);

  /// \brief Builds a tuple from logical fields.
  /// \param fields Fields in order; `std::nullopt` means SQL NULL.
  /// \param header Header values to preserve (MVCC fields and item pointer).
  /// \return The physical tuple.
  /// \throws SqlStateError with `54000` when the tuple does not fit in a page.
  [[nodiscard]] static auto from_fields(std::span<const std::optional<Field>> fields,
                                        HeapTupleHeader header = {}) -> HeapTuple;

  /// \brief Rebuilds a tuple from its physical representation.
  /// \param bytes Bytes previously produced by `bytes`.
  /// \return The tuple, or `std::nullopt` if the buffer is malformed.
  [[nodiscard]] static auto from_bytes(std::span<const std::byte> bytes)
      -> std::optional<HeapTuple>;

  /// \brief Read-only access to the physical bytes.
  [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte>;

  /// \brief Reads the on-page header.
  [[nodiscard]] auto header() const noexcept -> HeapTupleHeader;

  /// \brief Size of the physical tuple in bytes.
  [[nodiscard]] auto size() const noexcept -> std::size_t;

  /// \brief Number of attributes, including nulls.
  [[nodiscard]] auto field_count() const noexcept -> std::uint16_t;

  /// \brief Tells whether the attribute at `index` is null.
  /// \throws std::out_of_range when `index` is not a valid attribute.
  [[nodiscard]] auto is_null(std::size_t index) const -> bool;

  /// \brief Decodes the logical fields, including nulls.
  [[nodiscard]] auto fields() const -> std::vector<std::optional<Field>>;

  /// \brief Tells whether the tuple fits in a page with its line pointer.
  [[nodiscard]] auto fits_in_page() const noexcept -> bool;

  /// \brief Compares the physical bytes.
  [[nodiscard]] auto operator==(const HeapTuple&) const -> bool = default;

  private:
  /// \brief Wraps validated physical bytes.
  explicit HeapTuple(std::vector<std::byte> bytes) : bytes_{std::move(bytes)} {}

  /// \brief Physical bytes, including header and payload.
  std::vector<std::byte> bytes_;
};

}  // namespace kravidb::storage

namespace kravidb::storage::heap_detail {

/// \brief Aligns `value` up to the next multiple of `alignment`.
[[nodiscard]] inline auto align_up(std::size_t value, std::size_t alignment) -> std::size_t {
  return ((value + alignment - 1U) / alignment) * alignment;
}

/// \brief Appends the raw bytes of a scalar.
template <typename T>
auto append_raw(std::vector<std::byte>& out, T value) -> void {
  const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  out.insert(out.end(), raw.begin(), raw.end());
}

/// \brief Reads a scalar from a buffer and advances the offset.
template <typename T>
[[nodiscard]] auto read_raw(std::span<const std::byte> buffer, std::size_t& offset)
    -> std::optional<T> {
  if (buffer.size() < offset || buffer.size() - offset < sizeof(T)) {
    return std::nullopt;
  }
  std::array<std::byte, sizeof(T)> raw{};
  std::ranges::copy(buffer.subspan(offset, sizeof(T)), raw.begin());
  offset += sizeof(T);
  return std::bit_cast<T>(raw);
}

/// \brief Reads one bit from the null bitmap.
[[nodiscard]] inline auto null_bit(std::span<const std::byte> bytes, std::size_t index) -> bool {
  const auto byte_index = sizeof(HeapTupleHeader) + (index / heap_bitmap_bits_per_byte);
  const auto raw = std::to_integer<unsigned int>(bytes.subspan(byte_index, 1).front());
  return ((raw >> (index % heap_bitmap_bits_per_byte)) & 1U) != 0U;
}

/// \brief Decodes and validates the fields of a physical tuple.
[[nodiscard]] inline auto decode_fields(std::span<const std::byte> bytes,
                                        const HeapTupleHeader& header)
    -> std::optional<std::vector<std::optional<Field>>> {
  const auto count = static_cast<std::size_t>(header.t_infomask2 & heap_natts_mask);
  const bool has_null = (header.t_infomask & heap_has_null) != 0;
  const auto bitmap_size =
      has_null ? (count + heap_bitmap_bit_mask) / heap_bitmap_bits_per_byte : 0U;
  const auto expected_hoff =
      align_up(sizeof(HeapTupleHeader) + bitmap_size, heap_payload_alignment);
  if (header.t_hoff != expected_hoff || header.t_hoff > bytes.size()) {
    return std::nullopt;
  }

  std::vector<std::optional<Field>> fields;
  fields.reserve(count);
  std::size_t offset = header.t_hoff;
  for (std::size_t index = 0; index < count; ++index) {
    if (has_null && !null_bit(bytes, index)) {
      fields.emplace_back(std::nullopt);
      continue;
    }
    const auto type = read_raw<std::uint8_t>(bytes, offset);
    if (!type) {
      return std::nullopt;
    }
    if (*type == static_cast<std::uint8_t>(FieldType::Int)) {
      const auto value = read_raw<std::int64_t>(bytes, offset);
      if (!value) {
        return std::nullopt;
      }
      fields.emplace_back(*value);
    } else if (*type == static_cast<std::uint8_t>(FieldType::Varchar)) {
      const auto length = read_raw<std::uint32_t>(bytes, offset);
      if (!length || bytes.size() < offset || bytes.size() - offset < *length) {
        return std::nullopt;
      }
      const std::size_t text_length = *length;
      std::string text;
      text.reserve(text_length);
      for (const std::byte raw : bytes.subspan(offset, text_length)) {
        text.push_back(static_cast<char>(raw));
      }
      fields.emplace_back(std::move(text));
      offset += text_length;
    } else if (*type == static_cast<std::uint8_t>(FieldType::Decimal)) {
      const auto scale = read_raw<std::uint8_t>(bytes, offset);
      const auto unscaled = read_raw<std::int64_t>(bytes, offset);
      if (!scale || !unscaled || *scale > decimal_max_scale) {
        return std::nullopt;
      }
      fields.emplace_back(Decimal{.scale = *scale, .unscaled = *unscaled});
    } else {
      return std::nullopt;
    }
  }
  if (offset != bytes.size()) {
    return std::nullopt;
  }
  return fields;
}

}  // namespace kravidb::storage::heap_detail

export namespace kravidb::storage {

/// \brief Builds a physical tuple from logical fields.
inline auto HeapTuple::from_fields(std::span<const std::optional<Field>> fields,
                                   HeapTupleHeader header) -> HeapTuple {
  if (fields.size() > heap_natts_mask) {
    throw std::invalid_argument{"heap tuple has too many fields"};
  }
  const bool has_null =
      std::ranges::any_of(fields, [](const auto& field) { return !field.has_value(); });
  const bool has_varwidth = std::ranges::any_of(fields, [](const auto& field) {
    return field.has_value() && std::holds_alternative<std::string>(*field);
  });
  const auto count = fields.size();
  const auto bitmap_size =
      has_null ? (count + heap_bitmap_bit_mask) / heap_bitmap_bits_per_byte : 0U;
  const auto payload_offset =
      heap_detail::align_up(sizeof(HeapTupleHeader) + bitmap_size, heap_payload_alignment);
  if (payload_offset > std::numeric_limits<std::uint8_t>::max()) {
    throw std::invalid_argument{"heap tuple header offset does not fit in t_hoff"};
  }

  const auto natts_mask = ~static_cast<std::uint32_t>(heap_natts_mask);
  const auto layout_mask =
      static_cast<std::uint32_t>(heap_has_null) | static_cast<std::uint32_t>(heap_has_varwidth);
  header.t_infomask2 =
      static_cast<std::uint16_t>((static_cast<std::uint32_t>(header.t_infomask2) & natts_mask) |
                                 static_cast<std::uint32_t>(count));
  const auto null_flag = has_null ? static_cast<std::uint32_t>(heap_has_null) : 0U;
  const auto varwidth_flag = has_varwidth ? static_cast<std::uint32_t>(heap_has_varwidth) : 0U;
  header.t_infomask = static_cast<std::uint16_t>(
      (static_cast<std::uint32_t>(header.t_infomask) & ~layout_mask) | null_flag | varwidth_flag);
  header.t_hoff = static_cast<std::uint8_t>(payload_offset);

  std::vector<std::byte> bytes(payload_offset);
  std::memcpy(bytes.data(), &header, sizeof(header));

  if (has_null) {
    for (std::size_t index = 0; index < count; ++index) {
      const auto& field = *(fields.begin() + static_cast<std::ptrdiff_t>(index));
      if (field.has_value()) {
        const auto byte_index = sizeof(HeapTupleHeader) + (index / heap_bitmap_bits_per_byte);
        bytes.at(byte_index) |=
            std::byte{static_cast<unsigned char>(1U << (index % heap_bitmap_bits_per_byte))};
      }
    }
  }

  for (std::size_t index = 0; index < count; ++index) {
    const auto& field = *(fields.begin() + static_cast<std::ptrdiff_t>(index));
    if (!field.has_value()) {
      continue;
    }
    if (const auto* value = std::get_if<std::int64_t>(&*field)) {
      heap_detail::append_raw(bytes, static_cast<std::uint8_t>(FieldType::Int));
      heap_detail::append_raw(bytes, *value);
    } else if (const auto* text = std::get_if<std::string>(&*field)) {
      heap_detail::append_raw(bytes, static_cast<std::uint8_t>(FieldType::Varchar));
      heap_detail::append_raw(bytes, static_cast<std::uint32_t>(text->size()));
      for (const char character : *text) {
        bytes.push_back(static_cast<std::byte>(character));
      }
    } else if (const auto* decimal = std::get_if<Decimal>(&*field)) {
      heap_detail::append_raw(bytes, static_cast<std::uint8_t>(FieldType::Decimal));
      heap_detail::append_raw(bytes, decimal->scale);
      heap_detail::append_raw(bytes, decimal->unscaled);
    }
  }

  if (bytes.size() > max_size) {
    throw SqlStateError{sqlstate_tuple_too_large, "tuple does not fit in a page"};
  }
  return HeapTuple{std::move(bytes)};
}

/// \brief Rebuilds a physical tuple from its bytes.
inline auto HeapTuple::from_bytes(std::span<const std::byte> bytes) -> std::optional<HeapTuple> {
  if (bytes.size() < sizeof(HeapTupleHeader) || bytes.size() > max_size) {
    return std::nullopt;
  }
  HeapTupleHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  if (!heap_detail::decode_fields(bytes, header)) {
    return std::nullopt;
  }
  std::vector<std::byte> copy(bytes.size());
  std::ranges::copy(bytes, copy.begin());
  return HeapTuple{std::move(copy)};
}

/// \brief Returns the physical bytes of the tuple.
inline auto HeapTuple::bytes() const noexcept -> std::span<const std::byte> {
  return std::span<const std::byte>{bytes_};
}

/// \brief Reads the on-page header.
inline auto HeapTuple::header() const noexcept -> HeapTupleHeader {
  HeapTupleHeader header{};
  std::memcpy(&header, bytes_.data(), sizeof(header));
  return header;
}

/// \brief Returns the size of the physical tuple.
inline auto HeapTuple::size() const noexcept -> std::size_t {
  return bytes_.size();
}

/// \brief Returns the number of attributes.
inline auto HeapTuple::field_count() const noexcept -> std::uint16_t {
  return static_cast<std::uint16_t>(header().t_infomask2 & heap_natts_mask);
}

/// \brief Tells whether an attribute is null.
inline auto HeapTuple::is_null(std::size_t index) const -> bool {
  if (index >= field_count()) {
    throw std::out_of_range{"heap tuple field index out of range"};
  }
  if ((header().t_infomask & heap_has_null) == 0) {
    return false;
  }
  return !heap_detail::null_bit(bytes_, index);
}

/// \brief Decodes the logical fields.
inline auto HeapTuple::fields() const -> std::vector<std::optional<Field>> {
  const auto decoded = heap_detail::decode_fields(bytes_, header());
  if (!decoded) {
    throw std::logic_error{"invalid heap tuple bytes"};
  }
  return *decoded;
}

/// \brief Tells whether the tuple fits in a page.
inline auto HeapTuple::fits_in_page() const noexcept -> bool {
  return size() <= max_size;
}

}  // namespace kravidb::storage
