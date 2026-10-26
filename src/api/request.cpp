module api.request;

import std;

namespace kravidb::api {

namespace {

constexpr std::uint32_t decimal_digit_base = 10U;
constexpr std::uint32_t hex_digit_base = 16U;
constexpr std::uint32_t two_byte_lead = 0xC0U;
constexpr std::uint32_t three_byte_lead = 0xE0U;
constexpr std::uint32_t four_byte_lead = 0xF0U;
constexpr std::uint32_t two_byte_shift = 6U;
constexpr std::uint32_t three_byte_shift = 12U;
constexpr std::uint32_t four_byte_shift = 18U;
constexpr std::uint32_t surrogate_shift = 10U;
constexpr std::size_t false_length = 5;

// Tells whether `character` is JSON whitespace.
auto is_whitespace(char character) -> bool {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

// Advances `position` over the whitespace in `text`.
auto skip_whitespace(std::string_view text, std::size_t position) -> std::size_t {
  while (position < text.size() && is_whitespace(text.at(position))) {
    ++position;
  }
  return position;
}

// Returns the hexadecimal value of a digit, when it is one.
auto hex_value(char character) -> std::optional<std::uint32_t> {
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint32_t>(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return static_cast<std::uint32_t>(character - 'a') + decimal_digit_base;
  }
  if (character >= 'A' && character <= 'F') {
    return static_cast<std::uint32_t>(character - 'A') + decimal_digit_base;
  }
  return std::nullopt;
}

// Reads four hexadecimal digits starting at `position`.
auto read_hex4(std::string_view text, std::size_t position) -> std::optional<std::uint32_t> {
  constexpr std::size_t hex_digits = 4;
  if (position + hex_digits > text.size()) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  for (std::size_t offset = 0; offset < hex_digits; ++offset) {
    const auto digit = hex_value(text.at(position + offset));
    if (!digit.has_value()) {
      return std::nullopt;
    }
    value = value * hex_digit_base + *digit;
  }
  return value;
}

// Appends `code_point` to `out`, encoded as UTF-8.
void append_utf8(std::string& out, std::uint32_t code_point) {
  constexpr std::uint32_t ascii_limit = 0x7FU;
  constexpr std::uint32_t two_byte_limit = 0x7FFU;
  constexpr std::uint32_t three_byte_limit = 0xFFFFU;
  constexpr std::uint32_t continuation_mask = 0x3FU;
  constexpr std::uint32_t continuation_prefix = 0x80U;

  if (code_point <= ascii_limit) {
    out += static_cast<char>(code_point);
    return;
  }
  if (code_point <= two_byte_limit) {
    out += static_cast<char>(two_byte_lead | (code_point >> two_byte_shift));
    out += static_cast<char>(continuation_prefix | (code_point & continuation_mask));
    return;
  }
  if (code_point <= three_byte_limit) {
    out += static_cast<char>(three_byte_lead | (code_point >> three_byte_shift));
    out += static_cast<char>(continuation_prefix |
                             ((code_point >> two_byte_shift) & continuation_mask));
    out += static_cast<char>(continuation_prefix | (code_point & continuation_mask));
    return;
  }
  out += static_cast<char>(four_byte_lead | (code_point >> four_byte_shift));
  out += static_cast<char>(continuation_prefix |
                           ((code_point >> three_byte_shift) & continuation_mask));
  out +=
      static_cast<char>(continuation_prefix | ((code_point >> two_byte_shift) & continuation_mask));
  out += static_cast<char>(continuation_prefix | (code_point & continuation_mask));
}

// Parses a JSON string starting at `position` and returns its value and the
// position after the closing quote.
auto parse_string_at(std::string_view text, std::size_t position)
    -> std::optional<std::pair<std::string, std::size_t>> {
  constexpr char quote = '"';
  constexpr char backslash = '\\';
  constexpr std::uint32_t control_limit = 0x20U;
  constexpr std::uint32_t high_surrogate_min = 0xD800U;
  constexpr std::uint32_t high_surrogate_max = 0xDBFFU;
  constexpr std::uint32_t low_surrogate_min = 0xDC00U;
  constexpr std::uint32_t low_surrogate_max = 0xDFFFU;
  constexpr std::uint32_t surrogate_base = 0x10000U;

  if (position >= text.size() || text.at(position) != quote) {
    return std::nullopt;
  }

  std::string out;
  ++position;
  while (position < text.size()) {
    const char current = text.at(position);
    if (current == quote) {
      return std::pair{std::move(out), position + 1};
    }
    if (current == backslash) {
      ++position;
      if (position >= text.size()) {
        return std::nullopt;
      }
      const char escape = text.at(position);
      ++position;
      switch (escape) {
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case '/':
          out += '/';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          const auto first = read_hex4(text, position);
          if (!first.has_value()) {
            return std::nullopt;
          }
          position += 4;
          std::uint32_t code_point = *first;
          if (code_point >= high_surrogate_min && code_point <= high_surrogate_max) {
            if (position + 1 >= text.size() || text.at(position) != backslash ||
                text.at(position + 1) != 'u') {
              return std::nullopt;
            }
            position += 2;
            const auto second = read_hex4(text, position);
            if (!second.has_value() || *second < low_surrogate_min || *second > low_surrogate_max) {
              return std::nullopt;
            }
            position += 4;
            code_point = surrogate_base + ((code_point - high_surrogate_min) << surrogate_shift) +
                         (*second - low_surrogate_min);
          } else if (code_point >= low_surrogate_min && code_point <= low_surrogate_max) {
            return std::nullopt;
          }
          append_utf8(out, code_point);
          break;
        }
        default:
          return std::nullopt;
      }
      continue;
    }
    if (static_cast<std::uint32_t>(static_cast<unsigned char>(current)) < control_limit) {
      return std::nullopt;
    }
    out += current;
    ++position;
  }
  return std::nullopt;
}

// Skips over a complete JSON value and returns the next position.
auto skip_value(std::string_view text, std::size_t position) -> std::optional<std::size_t> {
  if (position >= text.size()) {
    return std::nullopt;
  }

  const char first = text.at(position);
  if (first == '"') {
    const auto parsed = parse_string_at(text, position);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    return parsed->second;
  }
  if (first == '{' || first == '[') {
    std::vector<char> closes;
    closes.push_back(first == '{' ? '}' : ']');
    ++position;
    while (position < text.size()) {
      const char current = text.at(position);
      if (current == '"') {
        const auto parsed = parse_string_at(text, position);
        if (!parsed.has_value()) {
          return std::nullopt;
        }
        position = parsed->second;
        continue;
      }
      if (current == '{' || current == '[') {
        closes.push_back(current == '{' ? '}' : ']');
        ++position;
        continue;
      }
      if (current == '}' || current == ']') {
        if (closes.empty() || current != closes.back()) {
          return std::nullopt;
        }
        closes.pop_back();
        ++position;
        if (closes.empty()) {
          return position;
        }
        continue;
      }
      ++position;
    }
    return std::nullopt;
  }
  if (text.substr(position).starts_with("true")) {
    return position + 4;
  }
  if (text.substr(position).starts_with("false")) {
    return position + false_length;
  }
  if (text.substr(position).starts_with("null")) {
    return position + 4;
  }

  const auto start = position;
  while (position < text.size()) {
    const char current = text.at(position);
    if (is_whitespace(current) || current == ',' || current == '}' || current == ']') {
      break;
    }
    ++position;
  }
  if (position == start) {
    return std::nullopt;
  }
  return position;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto field_value(std::string_view body, std::string_view field) -> std::optional<std::string_view> {
  constexpr char object_open = '{';
  constexpr char object_close = '}';
  constexpr char colon = ':';
  constexpr char comma = ',';

  std::size_t position = skip_whitespace(body, 0);
  if (position >= body.size() || body.at(position) != object_open) {
    return std::nullopt;
  }
  ++position;

  while (true) {
    position = skip_whitespace(body, position);
    if (position >= body.size() || body.at(position) == object_close) {
      return std::nullopt;
    }
    const auto key = parse_string_at(body, position);
    if (!key.has_value()) {
      return std::nullopt;
    }
    position = skip_whitespace(body, key->second);
    if (position >= body.size() || body.at(position) != colon) {
      return std::nullopt;
    }
    position = skip_whitespace(body, position + 1);
    const auto value_start = position;
    const auto value_end = skip_value(body, position);
    if (!value_end.has_value()) {
      return std::nullopt;
    }
    if (key->first == field) {
      const auto after_value = skip_whitespace(body, *value_end);
      if (after_value >= body.size()) {
        return std::nullopt;
      }
      const char delimiter = body.at(after_value);
      if (delimiter != ',' && delimiter != object_close) {
        return std::nullopt;
      }
      return body.substr(value_start, *value_end - value_start);
    }
    position = skip_whitespace(body, *value_end);
    if (position >= body.size() || body.at(position) != comma) {
      return std::nullopt;
    }
    ++position;
  }
}

auto parse_int(std::string_view text) -> std::optional<std::int64_t> {
  const auto start = skip_whitespace(text, 0);
  if (start >= text.size()) {
    return std::nullopt;
  }

  std::int64_t value = 0;
  const auto begin = std::next(text.begin(), static_cast<std::ptrdiff_t>(start));
  const auto result = std::from_chars(begin, text.end(), value);
  // 0 = no error. std::errc{} is avoided because clang-tidy flags it under libc++.
  if (static_cast<int>(result.ec) != 0 || result.ptr == begin) {
    return std::nullopt;
  }
  const auto end = static_cast<std::size_t>(result.ptr - text.begin());
  if (skip_whitespace(text, end) != text.size()) {
    return std::nullopt;
  }
  return value;
}

auto parse_string(std::string_view text) -> std::optional<std::string> {
  const auto start = skip_whitespace(text, 0);
  const auto parsed = parse_string_at(text, start);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  if (skip_whitespace(text, parsed->second) != text.size()) {
    return std::nullopt;
  }
  return parsed->first;
}

auto body_int(std::string_view body, std::string_view field) -> std::optional<std::int64_t> {
  const auto value = field_value(body, field);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_int(*value);
}

auto body_string(std::string_view body, std::string_view field) -> std::optional<std::string> {
  const auto value = field_value(body, field);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_string(*value);
}

}  // namespace kravidb::api
