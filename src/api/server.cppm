/// \file
/// \brief Servidor HTTP de la API de kravidb.

export module api.server;

import std;
export import api.handlers;

export namespace kravidb::api {

/// \brief Puerto de escucha por defecto de la API.
inline constexpr int default_api_port = 8080;

/// \brief Arranca el servidor HTTP bloqueante en el puerto indicado.
/// \param port Puerto de escucha.
/// \param degree Grado inicial del arbol B.
/// \param bind_address Direccion de escucha; por defecto solo loopback.
/// \return 0 si el servidor se detiene sin errores, 1 si no puede arrancar.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto run_server(int port, int degree, std::string_view bind_address = "127.0.0.1") -> int;

}  // namespace kravidb::api
