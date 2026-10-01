/// \file
/// \brief Serializacion binaria de tuplas (`Tuple`).

export module storage.tuple;

import std;

namespace kravidb::storage::detail {

/// \brief Escribe un valor escalar en crudo al final de un buffer.
/// \tparam T Tipo escalar a escribir.
/// \param out Buffer de destino.
/// \param value Valor a serializar (se copian sus bytes tal cual).
template <typename T>
void write_raw(std::vector<std::byte>& out, T value) {
  const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
  out.insert(out.end(), raw.begin(), raw.end());
}

/// \brief Lee un valor escalar en crudo desde un buffer.
/// \tparam T Tipo escalar a leer.
/// \param buffer Bytes de origen.
/// \param offset Desplazamiento de lectura; avanza en `sizeof(T)`.
/// \return El valor leido, o `std::nullopt` si no quedan bytes suficientes.
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

/// \brief Tipo de un campo dentro de una tupla serializada.
enum class FieldType : std::uint8_t {
  /// \brief Entero de 64 bits con signo.
  Int = 0,

  /// \brief Cadena de longitud variable (prefijo `u32` de longitud mas bytes).
  Varchar = 1,
};

/// \brief Valor de un campo: entero de 64 bits o cadena.
using Field = std::variant<std::int64_t, std::string>;

/// \brief Coleccion ordenada de campos con serializacion binaria propia.
///
/// El formato serializado es un `u32` con el numero de campos, seguido de cada
/// campo como un `u8` de tipo mas su carga: `Int` ocupa 8 bytes y `Varchar` un
/// `u32` de longitud seguido de los bytes de la cadena.
class Tuple {
  public:
  /// \brief Construye una tupla vacia.
  Tuple() = default;

  /// \brief Construye una tupla a partir de sus campos.
  /// \param fields Campos en orden.
  explicit Tuple(std::vector<Field> fields) : fields_{std::move(fields)} {}

  /// \brief Numero de campos de la tupla.
  [[nodiscard]] auto field_count() const noexcept -> std::size_t { return fields_.size(); }

  /// \brief Acceso de solo lectura a los campos, en orden.
  [[nodiscard]] auto fields() const noexcept -> const std::vector<Field>& { return fields_; }

  /// \brief Tamano en bytes de la representacion serializada.
  [[nodiscard]] auto serialized_size() const noexcept -> std::size_t {
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

  /// \brief Serializa la tupla a su representacion binaria.
  /// \return Los bytes en el formato descrito en la documentacion de la clase.
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
      } else {
        detail::write_raw(out, static_cast<std::uint8_t>(FieldType::Int));
        detail::write_raw(out, std::get<std::int64_t>(field));
      }
    }
    return out;
  }

  /// \brief Reconstruye una tupla desde su representacion binaria.
  /// \param buffer Bytes previamente producidos por `serialize`.
  /// \return La tupla rehidratada, o `std::nullopt` si el buffer esta truncado o malformado.
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
      } else {
        return std::nullopt;
      }
    }
    return Tuple{std::move(fields)};
  }

  /// \brief Compara dos tuplas campo a campo.
  [[nodiscard]] auto operator==(const Tuple&) const -> bool = default;

  private:
  /// \brief Campos de la tupla, en orden.
  std::vector<Field> fields_;
};

}  // namespace kravidb::storage
