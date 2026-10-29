/// \file
/// \brief HTTP server of the kravidb API.

export module api.server;

import std;
export import api.handlers;

export namespace kravidb::api {

/// \brief Default listen port of the API.
inline constexpr int default_api_port = 8080;

/// \brief Starts the blocking HTTP server on the given port.
/// \param port Listen port.
/// \param degree Initial B-tree degree.
/// \param bind_address Listen address; loopback only by default.
/// \return 0 when the server stops without errors, 1 when it cannot start.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto run_server(int port, int degree, std::string_view bind_address = "127.0.0.1") -> int;

}  // namespace kravidb::api
