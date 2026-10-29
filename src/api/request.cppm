/// \file
/// \brief Parser for the JSON subset the API routes accept.

export module api.request;

import std;

export namespace kravidb::api {

/// \brief Returns the raw value of the `field` member of a flat JSON object.
/// \param body JSON body of the request.
/// \param field Exact name of the field to look up.
/// \return The untrimmed value, or `std::nullopt` when the field is missing
/// or the object is malformed.
///
/// Only top-level keys are considered: a key repeated inside a string or a
/// nested object does not match.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto field_value(std::string_view body, std::string_view field)
    -> std::optional<std::string_view>;

/// \brief Parses a complete JSON integer.
/// \param text Value text, with optional surrounding whitespace.
/// \return The integer, or `std::nullopt` when there is trailing text, it is
/// empty or it overflows `std::int64_t`.
[[nodiscard]] auto parse_int(std::string_view text) -> std::optional<std::int64_t>;

/// \brief Parses a complete JSON string and undoes its escapes.
/// \param text Text with enclosing quotes and optional surrounding whitespace.
/// \return The decoded string, or `std::nullopt` when it is malformed.
///
/// Accepts `\" \\ \/ \b \f \n \r \t` and `\uXXXX` (including surrogate pairs).
[[nodiscard]] auto parse_string(std::string_view text) -> std::optional<std::string>;

/// \brief Reads an integer field from the JSON body.
/// \param body JSON body of the request.
/// \param field Exact field name.
/// \return The integer, or `std::nullopt` when the field is missing or invalid.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto body_int(std::string_view body, std::string_view field)
    -> std::optional<std::int64_t>;

/// \brief Reads a text field from the JSON body.
/// \param body JSON body of the request.
/// \param field Exact field name.
/// \return The string, or `std::nullopt` when the field is missing or invalid.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto body_string(std::string_view body, std::string_view field)
    -> std::optional<std::string>;

}  // namespace kravidb::api
