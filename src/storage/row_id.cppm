/// \file
/// \brief Localizadores de registro: empaquetado y desempaquetado de `RowID`.

export module storage.row_id;

import std;

export namespace kravidb::storage {

/// \brief Identificador de pagina (ocupa los 32 bits altos de un `RowID`).
using PageID = std::uint32_t;

/// \brief Identificador de ranura dentro de una pagina (32 bits bajos de un `RowID`).
using SlotID = std::uint32_t;

/// \brief Localizador fisico de un registro: pagina y ranura empaquetadas en 64 bits.
using RowID = std::uint64_t;

/// \brief Numero de bits reservados para `slot_id` (los bajos).
inline constexpr unsigned slot_bits = 32;

/// \brief Mascara que aisla los bits bajos correspondientes a `slot_id`.
inline constexpr RowID slot_mask = 0xFFFF'FFFFULL;

/// \brief Ubicacion logica de un registro dentro del almacen.
struct RowLocation {
  /// \brief Pagina que contiene el registro.
  PageID page_id{0};

  /// \brief Ranura ocupada dentro de la pagina.
  SlotID slot_id{0};

  /// \brief Compara dos ubicaciones campo a campo.
  [[nodiscard]] auto operator==(const RowLocation&) const -> bool = default;
};

/// \brief Empaqueta una ubicacion en un unico `RowID`.
/// \param location Pagina y ranura a codificar.
/// \return El `RowID` con la pagina en los 32 bits altos y la ranura en los bajos.
[[nodiscard]] constexpr auto encode_row_id(RowLocation location) noexcept -> RowID {
  return (static_cast<RowID>(location.page_id) << slot_bits) | static_cast<RowID>(location.slot_id);
}

/// \brief Separa un `RowID` en sus componentes de pagina y ranura.
/// \param row_id Localizador a decodificar.
/// \return La ubicacion reconstruida a partir del identificador.
[[nodiscard]] constexpr auto decode_row_id(RowID row_id) noexcept -> RowLocation {
  return RowLocation{
      .page_id = static_cast<PageID>(row_id >> slot_bits),
      .slot_id = static_cast<SlotID>(row_id & slot_mask),
  };
}

}  // namespace kravidb::storage
