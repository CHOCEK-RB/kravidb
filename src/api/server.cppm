/// \file
/// \brief Servidor HTTP de la API de kravidb.

export module api.server;

import std;

export namespace kravidb::api {

/// \brief Puerto de escucha por defecto de la API.
inline constexpr int default_api_port = 8080;

/// \brief Arranca el servidor HTTP bloqueante en el puerto indicado.
/// \param port Puerto de escucha.
/// \return 0 si el servidor se detiene sin errores, 1 si no puede arrancar.
auto run_server(int port) -> int;

}  // namespace kravidb::api
