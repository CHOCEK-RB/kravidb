module;

#include <httplib.h>

module api.server;

import std;
import api.handlers;

namespace {

// Candidatos para el directorio de build del frontend segun el directorio de trabajo.
inline constexpr std::array<std::string_view, 3> web_root_candidates = {
    "web/dist",
    "../../../../web/dist",
    "dist",
};

// Resuelve la ubicacion real de los archivos estaticos si alguna de las rutas existe.
auto resolve_web_root() -> std::optional<std::string> {
  for (const auto candidate : web_root_candidates) {
    if (std::filesystem::is_directory(candidate)) {
      return std::string{candidate};
    }
  }
  return std::nullopt;
}

// Escribe una traza del servidor en stdout. Un mutex evita que las lineas de
// los hilos del servidor se entremezclen (std::osyncstream no existe en
// libc++, que es la stdlib de CI) y se vacia el buffer para que las trazas
// aparezcan aunque la salida este redirigida.
void log_line(std::string_view message) {
  static std::mutex log_mutex;
  const std::scoped_lock lock{log_mutex};
  std::cout << "[api] " << message << '\n' << std::flush;
}

// Acepta solo los origenes del servidor de desarrollo de Vite.
auto is_local_origin(std::string_view origin) -> bool {
  return origin == "http://localhost" || origin.starts_with("http://localhost:") ||
         origin == "http://127.0.0.1" || origin.starts_with("http://127.0.0.1:");
}

// Refleja el origen local en las respuestas CORS; el frontend compilado se sirve
// desde el mismo origen y no necesita estas cabeceras.
void configure_cors(httplib::Server& server) {
  server.set_pre_routing_handler(
      [](const httplib::Request& request,
         httplib::Response& response) -> httplib::Server::HandlerResponse {
        const auto origin = request.get_header_value("Origin");
        if (is_local_origin(origin)) {
          response.set_header("Access-Control-Allow-Origin", origin);
          response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
          response.set_header("Access-Control-Allow-Headers", "Content-Type");
          response.set_header("Vary", "Origin");
        }
        return httplib::Server::HandlerResponse::Unhandled;
      });

  server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& response) -> void {
    response.status = kravidb::api::http_no_content;
  });
}

// Impide que dos instancias compartan el puerto: cpp-httplib activa
// SO_REUSEPORT por defecto, asi que un segundo proceso se reparte las
// peticiones con el primero y cada uno escribe su log y su motor por separado.
void configure_socket_options(httplib::Server& server) {
  server.set_socket_options([](socket_t sock) -> void {
    static_cast<void>(httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1));
#ifdef SO_REUSEPORT
    static_cast<void>(httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEPORT, 0));
#endif
  });
}

// Copia una respuesta logica de la API a la respuesta HTTP.
void write_response(httplib::Response& response, const kravidb::api::ApiResponse& result) {
  response.status = result.status;
  response.set_content(result.body, "application/json");
}

}  // namespace

namespace kravidb::api {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto run_server(int port, int degree, std::string_view bind_address) -> int {
  httplib::Server server;
  configure_cors(server);
  configure_socket_options(server);

  // Contador de peticiones atendidas, para confirmar que el cliente conecta.
  std::atomic<std::uint64_t> request_count{0};
  server.set_logger(
      [&request_count](const httplib::Request& request, const httplib::Response& response) -> void {
        const auto number = request_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const std::string_view kind = request.path.starts_with("/api/") ? "api" : "web";
        log_line(std::format("#{} {} {} {} -> {} ({} B)", number, kind, request.method,
                             request.target, response.status, response.body.size()));
      });

  int current_degree = degree;
  auto engine = make_engine(current_degree);
  std::mutex mutex;

  server.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& response) -> void {
    write_response(response, health_response());
  });

  server.Get(
      "/api/v1/config",
      [&current_degree, &mutex](const httplib::Request&, httplib::Response& response) -> void {
        const std::scoped_lock lock{mutex};
        write_response(response, config_response(current_degree));
      });

  server.Post("/api/v1/config",
              [&engine, &current_degree, &mutex](const httplib::Request& request,
                                                 httplib::Response& response) -> void {
                const std::scoped_lock lock{mutex};
                write_response(response, change_degree(engine, current_degree, request.body));
              });

  server.Get("/api/v1/btree",
             [&engine, &mutex](const httplib::Request&, httplib::Response& response) -> void {
               const std::scoped_lock lock{mutex};
               write_response(response, btree_response(*engine));
             });

  server.Post(
      "/api/v1/btree/insert",
      [&engine, &mutex](const httplib::Request& request, httplib::Response& response) -> void {
        const std::scoped_lock lock{mutex};
        write_response(response, insert_response(*engine, request.body));
      });

  server.Get(
      "/api/v1/btree/search",
      [&engine, &mutex](const httplib::Request& request, httplib::Response& response) -> void {
        const std::scoped_lock lock{mutex};
        write_response(response, search_response(*engine, request.get_param_value("key")));
      });

  server.Get(
      R"(/api/v1/page/(\d+))",
      [&engine, &mutex](const httplib::Request& request, httplib::Response& response) -> void {
        const std::scoped_lock lock{mutex};
        const auto page_text = request.matches.size() > 1 ? request.matches.str(1) : std::string{};
        write_response(response, page_response(*engine, page_text));
      });

  server.Post("/api/v1/reset",
              [&engine, &current_degree, &mutex](const httplib::Request&,
                                                 httplib::Response& response) -> void {
                const std::scoped_lock lock{mutex};
                write_response(response, reset_response(engine, current_degree));
              });

  const auto web_dir = resolve_web_root();
  if (web_dir.has_value()) {
    server.set_mount_point("/", *web_dir);
  }

  // Se reserva el puerto antes de anunciarlo: si ya hay otra instancia, cpp-httplib
  // (con SO_REUSEPORT desactivado) falla aqui y el arranque se aborta con un aviso.
  if (!server.bind_to_port(std::string{bind_address}, port)) {
    log_line(
        std::format("no se pudo escuchar en {}:{}: probablemente ya hay otra instancia "
                    "de kravidb_api corriendo (mata la anterior y reintenta)",
                    bind_address, port));
    return 1;
  }

  log_line(std::format("kravidb api listening on http://{}:{}", bind_address, port));
  if (web_dir.has_value()) {
    log_line(std::format("web visualizer mounted from {} (open http://{}:{} in a browser)",
                         *web_dir, bind_address, port));
  } else {
    log_line("web/dist not found: serving the api only");
    log_line(std::format("build it with 'VITE_USE_MOCK=false bun run build' in web/ and restart"));
  }
  return server.listen_after_bind() ? 0 : 1;
}

}  // namespace kravidb::api
