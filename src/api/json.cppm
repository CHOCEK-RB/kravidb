/// \file
/// \brief Minimal JSON utilities and tuple field extraction.

export module api.json;

import std;
import storage.tuple;

export namespace kravidb::api {

/// \brief Escapes the JSON special characters present in `text`.
/// \param text Input text.
/// \return The escaped text, without enclosing quotes.
[[nodiscard]] auto json_escape(std::string_view text) -> std::string;

/// \brief Returns `text` quoted and with its characters escaped.
/// \param text Input text.
/// \return A valid JSON string literal.
[[nodiscard]] auto json_quote(std::string_view text) -> std::string;

/// \brief Extracts the primary key (first integer field) from a tuple.
/// \param tuple Serialized and deserialized tuple.
/// \return The key, or `0` when the first field is not an integer.
[[nodiscard]] auto tuple_key(const storage::Tuple& tuple) -> std::int64_t;

/// \brief Extracts the first text field from a tuple.
/// \param tuple Serialized and deserialized tuple.
/// \return The first `Varchar`, or the key as text when there is none.
[[nodiscard]] auto tuple_value(const storage::Tuple& tuple) -> std::string;

}  // namespace kravidb::api
