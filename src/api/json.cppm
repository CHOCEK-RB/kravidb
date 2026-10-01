/// \file
/// \brief Utilidades minimas de JSON y extraccion de campos de una tupla.

export module api.json;

import std;
import storage.tuple;

export namespace kravidb::api {

/// \brief Escapa los caracteres especiales de JSON presentes en `text`.
/// \param text Texto de entrada.
/// \return El texto escapado, sin comillas envolventes.
[[nodiscard]] auto json_escape(std::string_view text) -> std::string;

/// \brief Devuelve `text` entre comillas y con los caracteres escapados.
/// \param text Texto de entrada.
/// \return Un literal de cadena JSON valido.
[[nodiscard]] auto json_quote(std::string_view text) -> std::string;

/// \brief Extrae la clave primaria (primer campo entero) de una tupla.
/// \param tuple Tupla serializada y deserializada.
/// \return La clave, o `0` si el primer campo no es un entero.
[[nodiscard]] auto tuple_key(const storage::Tuple& tuple) -> std::int64_t;

/// \brief Extrae el primer campo de texto de una tupla.
/// \param tuple Tupla serializada y deserializada.
/// \return El primer `Varchar`, o la clave como texto si no hay ninguno.
[[nodiscard]] auto tuple_value(const storage::Tuple& tuple) -> std::string;

}  // namespace kravidb::api
