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

/// \brief Lee el grado del arbol desde la variable de entorno KRAVIDB_DEGREE.
/// \return El grado indicado, o el grado por defecto si no es valido.
auto parse_degree() -> int {
  const char* raw = std::getenv("KRAVIDB_DEGREE");
  if (raw == nullptr) {
    return kravidb::api::default_engine_degree;
  }
  const std::string_view text{raw};
  int parsed = 0;
  const auto result = std::from_chars(text.begin(), text.end(), parsed);
  if (result.ec != std::errc{} || parsed < kravidb::api::minimum_engine_degree ||
      parsed > kravidb::api::maximum_engine_degree) {
    return kravidb::api::default_engine_degree;
  }
  return parsed;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  return kravidb::api::run_server(parse_port(argc, argv), parse_degree());
}
