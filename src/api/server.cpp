/// \file
/// \brief Implementacion del servidor HTTP de la API.

module;

#include <httplib.h>

module api.server;

import std;
import kravidb;

namespace kravidb::api {

namespace {

/// \brief Codigo HTTP 204 (sin contenido) devuelto en el preflight de CORS.
inline constexpr int http_no_content = 204;

/// \brief Aplica a todas las respuestas las cabeceras de CORS.
/// \param server Servidor sobre el que registrar las cabeceras por defecto.
void configure_cors(httplib::Server& server) {
  server.set_default_headers({
      {"Access-Control-Allow-Origin", "*"},
      {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
      {"Access-Control-Allow-Headers", "Content-Type"},
  });

  server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& response) -> void {
    response.status = http_no_content;
  });
}

}  // namespace

auto run_server(int port) -> int {
  httplib::Server server;
  configure_cors(server);

  server.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& response) -> void {
    response.set_content(R"({"status":"ok"})", "application/json");
  });

  return server.listen("0.0.0.0", port) ? 0 : 1;
}

}  // namespace kravidb::api
