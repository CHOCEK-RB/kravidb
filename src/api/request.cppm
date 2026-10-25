/// \file
/// \brief Parser del subconjunto JSON que aceptan las rutas de la API.

export module api.request;

import std;

export namespace kravidb::api {

/// \brief Devuelve el valor crudo del campo `field` de un objeto JSON plano.
/// \param body Cuerpo JSON de la peticion.
/// \param field Nombre exacto del campo buscado.
/// \return El valor sin recortar, o `std::nullopt` si el campo no existe o el
/// objeto esta mal formado.
///
/// Solo considera claves de primer nivel: una clave repetida dentro de una
/// cadena o de un objeto anidado no coincide.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto field_value(std::string_view body, std::string_view field)
    -> std::optional<std::string_view>;

/// \brief Interpreta un entero JSON completo.
/// \param text Texto del valor, con espacios opcionales alrededor.
/// \return El entero, o `std::nullopt` si hay texto sobrante, esta vacio o
/// desborda un `std::int64_t`.
[[nodiscard]] auto parse_int(std::string_view text) -> std::optional<std::int64_t>;

/// \brief Interpreta una cadena JSON completa y deshace sus escapes.
/// \param text Texto con comillas envolventes y espacios opcionales alrededor.
/// \return La cadena decodificada, o `std::nullopt` si esta mal formada.
///
/// Admite `\" \\ \/ \b \f \n \r \t` y `\uXXXX` (incluidos pares suplentes).
[[nodiscard]] auto parse_string(std::string_view text) -> std::optional<std::string>;

/// \brief Lee un campo entero del cuerpo JSON.
/// \param body Cuerpo JSON de la peticion.
/// \param field Nombre exacto del campo.
/// \return El entero, o `std::nullopt` si el campo falta o no es valido.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto body_int(std::string_view body, std::string_view field)
    -> std::optional<std::int64_t>;

/// \brief Lee un campo de texto del cuerpo JSON.
/// \param body Cuerpo JSON de la peticion.
/// \param field Nombre exacto del campo.
/// \return La cadena, o `std::nullopt` si el campo falta o no es valida.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto body_string(std::string_view body, std::string_view field)
    -> std::optional<std::string>;

}  // namespace kravidb::api
