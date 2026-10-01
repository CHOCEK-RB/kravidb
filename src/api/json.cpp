module api.json;

import std;
import storage.tuple;

namespace kravidb::api {

namespace {

// Por debajo de este valor los caracteres de control se escapan como \u00XX.
constexpr unsigned char control_limit = 0x20U;

}  // namespace

auto json_escape(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (const char raw : text) {
    const auto byte = static_cast<unsigned char>(raw);
    switch (raw) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if (byte < control_limit) {
          out += std::format("\\u{:04x}", static_cast<unsigned>(byte));
        } else {
          out += raw;
        }
        break;
    }
  }
  return out;
}

auto json_quote(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size() + 2);
  out += '"';
  out += json_escape(text);
  out += '"';
  return out;
}

auto tuple_key(const storage::Tuple& tuple) -> std::int64_t {
  const auto& fields = tuple.fields();
  if (fields.empty()) {
    return 0;
  }
  const auto* value = std::get_if<std::int64_t>(&fields.front());
  return value != nullptr ? *value : 0;
}

auto tuple_value(const storage::Tuple& tuple) -> std::string {
  for (const auto& field : tuple.fields()) {
    if (const auto* text = std::get_if<std::string>(&field); text != nullptr) {
      return *text;
    }
  }
  return std::to_string(tuple_key(tuple));
}

}  // namespace kravidb::api
