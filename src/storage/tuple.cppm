/// \file
/// \brief Binary serialization of tuples (`Tuple`).

export module storage.tuple;

import std;

namespace kravidb::storage::detail {

/// \brief Writes a scalar value raw at the end of a buffer.
/// \tparam T Scalar type to write.
/// \param out Destination buffer.
/// \param value Value to serialize (its bytes are copied as-is).
template <typename T>
void write_raw(std::vector<std::byte>& out, T value) {
  const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  out.insert(out.end(), raw.begin(), raw.end());
}

/// \brief Reads a scalar value raw from a buffer.
/// \tparam T Scalar type to read.
/// \param buffer Source bytes.
/// \param offset Read offset; advances by `sizeof(T)`.
/// \return The value read, or `std::nullopt` if there are not enough bytes left.
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

}  // namespace kravidb::storage::detail

export namespace kravidb::storage {

/// \brief Type of a field inside a serialized tuple.
enum class FieldType : std::uint8_t {
  /// \brief Signed 64-bit integer.
  Int = 0,

  /// \brief Variable-length string (`u32` length prefix plus bytes).
  Varchar = 1,

  /// \brief Fixed-point decimal (`u8` scale plus `i64` unscaled value).
  Decimal = 2,
};

/// \brief Exact fixed-point value: `unscaled / 10^scale`.
struct Decimal {
  /// \brief Number of decimal digits after the point.
  std::uint8_t scale{0};

  /// \brief Value without the decimal point.
  std::int64_t unscaled{0};

  /// \brief Compares two decimal values field by field.
  [[nodiscard]] auto operator==(const Decimal&) const -> bool = default;
};

/// \brief Largest supported decimal scale (`DECIMAL(p,s)` with `p <= 18`).
inline constexpr std::uint8_t decimal_max_scale{18};

/// \brief Value of a field: 64-bit integer, string, or decimal.
using Field = std::variant<std::int64_t, std::string, Decimal>;

/// \brief Ordered collection of fields with its own binary serialization.
///
/// The serialized format is a `u32` with the field count, followed by each
/// field as a `u8` type plus its payload: `Int` occupies 8 bytes and `Varchar`
/// a `u32` length followed by the string bytes.
class Tuple {
  public:
  /// \brief Builds an empty tuple.
  Tuple() = default;

  /// \brief Builds a tuple from its fields.
  /// \param fields Fields in order.
  explicit Tuple(std::vector<Field> fields) : fields_{std::move(fields)} {}

  /// \brief Number of fields in the tuple.
  [[nodiscard]] auto field_count() const noexcept -> std::size_t { return fields_.size(); }

  /// \brief Read-only access to the fields, in order.
  [[nodiscard]] auto fields() const noexcept -> const std::vector<Field>& { return fields_; }

  /// \brief Size in bytes of the serialized representation.
  [[nodiscard]] auto serialized_size() const noexcept -> std::size_t {
    std::size_t total = sizeof(std::uint32_t);
    for (const auto& field : fields_) {
      total += sizeof(std::uint8_t);
      if (const auto* text = std::get_if<std::string>(&field)) {
        total += sizeof(std::uint32_t) + text->size();
      } else if (std::holds_alternative<Decimal>(field)) {
        total += sizeof(std::uint8_t) + sizeof(std::int64_t);
      } else {
        total += sizeof(std::int64_t);
      }
    }
    return total;
  }

  /// \brief Serializes the tuple to its binary representation.
  /// \return The bytes in the format described in the class documentation.
  [[nodiscard]] auto serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> out;
    out.reserve(serialized_size());
    detail::write_raw(out, static_cast<std::uint32_t>(fields_.size()));
    for (const auto& field : fields_) {
      if (const auto* text = std::get_if<std::string>(&field)) {
        detail::write_raw(out, static_cast<std::uint8_t>(FieldType::Varchar));
        detail::write_raw(out, static_cast<std::uint32_t>(text->size()));
        for (const char character : *text) {
          out.push_back(static_cast<std::byte>(character));
        }
      } else if (const auto* decimal = std::get_if<Decimal>(&field)) {
        detail::write_raw(out, static_cast<std::uint8_t>(FieldType::Decimal));
        detail::write_raw(out, decimal->scale);
        detail::write_raw(out, decimal->unscaled);
      } else {
        detail::write_raw(out, static_cast<std::uint8_t>(FieldType::Int));
        detail::write_raw(out, std::get<std::int64_t>(field));
      }
    }
    return out;
  }

  /// \brief Rebuilds a tuple from its binary representation.
  /// \param buffer Bytes previously produced by `serialize`.
  /// \return The rehydrated tuple, or `std::nullopt` if the buffer is truncated or malformed.
  [[nodiscard]] static auto deserialize(std::span<const std::byte> buffer) -> std::optional<Tuple> {
    std::size_t offset = 0;
    const auto count = detail::read_raw<std::uint32_t>(buffer, offset);
    if (!count) {
      return std::nullopt;
    }
    std::vector<Field> fields;
    for (std::uint32_t index = 0; index < *count; ++index) {
      const auto type = detail::read_raw<std::uint8_t>(buffer, offset);
      if (!type) {
        return std::nullopt;
      }
      if (*type == static_cast<std::uint8_t>(FieldType::Int)) {
        const auto value = detail::read_raw<std::int64_t>(buffer, offset);
        if (!value) {
          return std::nullopt;
        }
        fields.emplace_back(*value);
      } else if (*type == static_cast<std::uint8_t>(FieldType::Varchar)) {
        const auto length = detail::read_raw<std::uint32_t>(buffer, offset);
        if (!length || buffer.size() - offset < *length) {
          return std::nullopt;
        }
        const std::size_t text_length = *length;
        std::string text;
        text.reserve(text_length);
        for (const std::byte raw : buffer.subspan(offset, text_length)) {
          text.push_back(static_cast<char>(raw));
        }
        fields.emplace_back(std::move(text));
        offset += text_length;
      } else if (*type == static_cast<std::uint8_t>(FieldType::Decimal)) {
        const auto scale = detail::read_raw<std::uint8_t>(buffer, offset);
        const auto unscaled = detail::read_raw<std::int64_t>(buffer, offset);
        if (!scale || !unscaled || *scale > decimal_max_scale) {
          return std::nullopt;
        }
        fields.emplace_back(Decimal{.scale = *scale, .unscaled = *unscaled});
      } else {
        return std::nullopt;
      }
    }
    return Tuple{std::move(fields)};
  }

  /// \brief Compares two tuples field by field.
  [[nodiscard]] auto operator==(const Tuple&) const -> bool = default;

  private:
  /// \brief Fields of the tuple, in order.
  std::vector<Field> fields_;
};

}  // namespace kravidb::storage
