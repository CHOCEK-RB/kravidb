/// \file
/// \brief Punto de entrada del servidor HTTP de kravidb.

import std;
import api.server;

namespace {

/// \brief Lee el puerto desde el primer argumento de linea de comandos.
/// \param argc Numero de argumentos del proceso.
/// \param argv Argumentos del proceso.
/// \return El puerto indicado, o el puerto por defecto si no se puede interpretar.
auto parse_port(int argc, char** argv) -> int {
  const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
  bool is_program_name = true;
  for (const char* argument : args) {
    if (is_program_name) {
      is_program_name = false;
      continue;
    }
    const std::string_view text{argument};
    int parsed = 0;
    const auto result = std::from_chars(text.begin(), text.end(), parsed);
    if (result.ec == std::errc{} && parsed > 0) {
      return parsed;
    }
    break;
  }
  return kravidb::api::default_api_port;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  return kravidb::api::run_server(parse_port(argc, argv));
}
