/// \file
/// \brief Servidor HTTP de la API de kravidb.

export module api.server;

import std;

export namespace kravidb::api {

/// \brief Puerto de escucha por defecto de la API.
inline constexpr int default_api_port = 8080;

/// \brief Grado del arbol B con el que arranca la API.
inline constexpr int default_engine_degree = 2;

/// \brief Grado minimo admitido para el arbol B.
inline constexpr int minimum_engine_degree = 2;

/// \brief Grado maximo admitido para el arbol B.
inline constexpr int maximum_engine_degree = 64;

/// \brief Arranca el servidor HTTP bloqueante en el puerto indicado.
/// \param port Puerto de escucha.
/// \param degree Grado inicial del arbol B.
/// \return 0 si el servidor se detiene sin errores, 1 si no puede arrancar.
auto run_server(int port, int degree) -> int;

}  // namespace kravidb::api
