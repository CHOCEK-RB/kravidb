export module storage.tuple;

import std;

namespace kravidb::storage::detail {

template <typename T>
void write_raw(std::vector<std::byte>& out, T value) {
  const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  out.insert(out.end(), raw.begin(), raw.end());
}

template <typename T>
[[nodiscard]] std::optional<T> read_raw(std::span<const std::byte> buffer, std::size_t& offset) {
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

enum class FieldType : std::uint8_t { Int = 0, Varchar = 1 };

using Field = std::variant<std::int64_t, std::string>;

// Formato binario: [u32 n_campos] y luego, por cada campo:
//   INT     -> [u8 tipo=0][i64 valor]
//   VARCHAR -> [u8 tipo=1][u32 longitud][bytes del texto]
class Tuple {
  public:
  Tuple() = default;
  explicit Tuple(std::vector<Field> fields) : fields_{std::move(fields)} {}

  [[nodiscard]] std::size_t field_count() const noexcept { return fields_.size(); }
  [[nodiscard]] const std::vector<Field>& fields() const noexcept { return fields_; }

  [[nodiscard]] std::size_t serialized_size() const noexcept {
    std::size_t total = sizeof(std::uint32_t);
    for (const auto& field : fields_) {
      total += sizeof(std::uint8_t);
      if (const auto* text = std::get_if<std::string>(&field)) {
        total += sizeof(std::uint32_t) + text->size();
      } else {
        total += sizeof(std::int64_t);
      }
    }
    return total;
  }

  [[nodiscard]] std::vector<std::byte> serialize() const {
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
      } else {
        detail::write_raw(out, static_cast<std::uint8_t>(FieldType::Int));
        detail::write_raw(out, std::get<std::int64_t>(field));
      }
    }
    return out;
  }

  [[nodiscard]] static std::optional<Tuple> deserialize(std::span<const std::byte> buffer) {
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
      } else {
        return std::nullopt;
      }
    }
    return Tuple{std::move(fields)};
  }

  [[nodiscard]] bool operator==(const Tuple&) const = default;

  private:
  std::vector<Field> fields_;
};

}  // namespace kravidb::storage
