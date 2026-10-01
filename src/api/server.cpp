module;

#include <httplib.h>

module api.server;

import std;
import kravidb;
import api.json;
import api.serialization;

namespace {

// Codigos HTTP usados por la API.
inline constexpr int http_no_content = 204;
inline constexpr int http_bad_request = 400;
inline constexpr int http_not_found = 404;
inline constexpr int http_internal_error = 500;

// Longitud del campo `bio` de los registros semilla.
inline constexpr std::size_t seed_bio_length = 255;

// Pagina servida desde el directorio de build del frontend, si existe.
constexpr std::string_view web_root = "web/dist";

// Escribe una traza del servidor en stdout. Un mutex evita que las lineas de
// los hilos del servidor se entremezclen (std::osyncstream no existe en
// libc++, que es la stdlib de CI) y se vacia el buffer para que las trazas
// aparezcan aunque la salida este redirigida.
void log_line(std::string_view message) {
  static std::mutex log_mutex;
  const std::scoped_lock lock{log_mutex};
  std::cout << "[api] " << message << '\n' << std::flush;
}

// Aplica a todas las respuestas las cabeceras de CORS.
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

// Omite los espacios iniciales de `text`.
auto skip_whitespace(std::string_view text) -> std::size_t {
  std::size_t position = 0;
  while (position < text.size() && (text.at(position) == ' ' || text.at(position) == '\t')) {
    ++position;
  }
  return position;
}

// Devuelve el valor crudo del campo JSON `field`, ya recortado por la izquierda.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto field_value(std::string_view body, std::string_view field) -> std::optional<std::string_view> {
  const std::string needle = "\"" + std::string{field} + "\"";
  const auto key_position = body.find(needle);
  if (key_position == std::string_view::npos) {
    return std::nullopt;
  }
  const auto colon = body.find(':', key_position + needle.size());
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  return body.substr(colon + 1);
}

// Interpreta un entero JSON sin signo a partir de su texto.
auto parse_int(std::string_view text) -> std::optional<std::int64_t> {
  const auto start = skip_whitespace(text);
  std::int64_t value = 0;
  const auto span = text.substr(start);
  const auto result = std::from_chars(span.begin(), span.end(), value);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

// Interpreta una cadena JSON (con comillas) y deshace sus escapes.
auto parse_string(std::string_view text) -> std::optional<std::string> {
  const auto start = skip_whitespace(text);
  if (start >= text.size() || text.at(start) != '"') {
    return std::nullopt;
  }
  std::string out;
  std::size_t position = start + 1;
  while (position < text.size()) {
    const char current = text.at(position);
    if (current == '\\') {
      ++position;
      if (position >= text.size()) {
        return std::nullopt;
      }
      switch (text.at(position)) {
        case 'n':
          out += '\n';
          break;
        case 't':
          out += '\t';
          break;
        case 'r':
          out += '\r';
          break;
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case '/':
          out += '/';
          break;
        default:
          out += text.at(position);
          break;
      }
      ++position;
      continue;
    }
    if (current == '"') {
      return out;
    }
    out += current;
    ++position;
  }
  return std::nullopt;
}

// Lee el campo entero `field` del cuerpo JSON.
auto body_int(std::string_view body, std::string_view field) -> std::optional<std::int64_t> {
  const auto value = field_value(body, field);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_int(*value);
}

// Lee el campo de texto `field` del cuerpo JSON.
auto body_string(std::string_view body, std::string_view field) -> std::optional<std::string> {
  const auto value = field_value(body, field);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_string(*value);
}

// Construye la carga util JSON de un registro semilla, igual que el mock del frontend.
auto seed_payload(std::int64_t key, std::string_view name) -> std::string {
  std::string payload = R"({"id":)" + std::to_string(key);
  payload += R"(,"table":"users","name":")";
  payload += name;
  payload += R"(","bio":")";
  payload += std::string(seed_bio_length, 'x');
  payload += R"("})";
  return payload;
}

// Escribe un registro y lo indexa por su clave primaria.
void seed_row(kravidb::storage::StorageEngine& engine, std::int64_t key, std::string_view name) {
  const auto payload = seed_payload(key, name);
  const kravidb::storage::Tuple tuple{std::vector<kravidb::storage::Field>{
      kravidb::storage::Field{key},
      kravidb::storage::Field{payload},
  }};
  const auto bytes = tuple.serialize();
  const auto row_id = engine.records().insert(bytes);
  engine.btree().insert_with_stats(key, row_id);
}

// Serializa el grado y el tamano de pagina del motor activo.
auto config_to_json(int degree) -> std::string {
  return std::format(R"({{"degree":{},"page_size":{}}})", degree,
                     kravidb::storage::default_page_size);
}

// Crea el motor con el grado indicado y lo siembra con las mismas filas que el mock.
auto make_engine(int degree) -> std::unique_ptr<kravidb::storage::StorageEngine> {
  auto engine = std::make_unique<kravidb::storage::StorageEngine>(
      kravidb::storage::default_page_size, degree);

  struct SeedRow {
    std::int64_t key;
    std::string_view name;
  };
  const std::vector<SeedRow> rows = {
      {.key = 10, .name = "Ada Lovelace"},    {.key = 20, .name = "Alan Turing"},
      {.key = 5, .name = "Grace Hopper"},     {.key = 15, .name = "Linus Torvalds"},
      {.key = 25, .name = "Dennis Ritchie"},  {.key = 30, .name = "Ken Thompson"},
      {.key = 35, .name = "Edsger Dijkstra"},
  };
  for (const auto& row : rows) {
    seed_row(*engine, row.key, row.name);
  }
  return engine;
}

// Reconstruye el motor con otro grado, preservando las tuplas existentes.
// Los RowID cambian (las paginas son append-only) pero el mapeo queda consistente.
void rebuild_engine(std::unique_ptr<kravidb::storage::StorageEngine>& engine, int degree) {
  const std::vector<kravidb::storage::Tuple> rows = engine->table().scan_all();
  auto fresh = std::make_unique<kravidb::storage::StorageEngine>(
      kravidb::storage::default_page_size, degree);
  for (const auto& row : rows) {
    fresh->table().insert(row);
  }
  engine = std::move(fresh);
}

// Responde con un error JSON minimo.
void respond_error(httplib::Response& response, int status, std::string_view message) {
  response.status = status;
  response.set_content("{\"error\":" + kravidb::api::json_quote(message) + "}", "application/json");
}

}  // namespace

namespace kravidb::api {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto run_server(int port, int degree) -> int {
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
    response.set_content(R"({"status":"ok"})", "application/json");
  });

  server.Get(
      "/api/v1/config",
      [&current_degree, &mutex](const httplib::Request&, httplib::Response& response) -> void {
        const std::scoped_lock lock{mutex};
        response.set_content(config_to_json(current_degree), "application/json");
      });

  server.Post("/api/v1/config",
              [&engine, &current_degree, &mutex](const httplib::Request& request,
                                                 httplib::Response& response) -> void {
                const auto requested = body_int(request.body, "degree");
                if (!requested.has_value() || *requested < minimum_engine_degree ||
                    *requested > maximum_engine_degree) {
                  respond_error(response, http_bad_request,
                                std::format("grado invalido: usa un entero entre {} y {}",
                                            minimum_engine_degree, maximum_engine_degree));
                  return;
                }
                const std::scoped_lock lock{mutex};
                try {
                  rebuild_engine(engine, static_cast<int>(*requested));
                  current_degree = static_cast<int>(*requested);
                  log_line(std::format("arbol reconstruido con grado {}", current_degree));
                  response.set_content(config_to_json(current_degree), "application/json");
                } catch (const std::exception& error) {
                  respond_error(response, http_internal_error, error.what());
                }
              });

  server.Get("/api/v1/btree",
             [&engine, &mutex](const httplib::Request&, httplib::Response& response) -> void {
               const std::scoped_lock lock{mutex};
               const auto* root = engine->btree().root();
               if (root == nullptr) {
                 response.set_content("null", "application/json");
                 return;
               }
               response.set_content(kravidb::api::node_to_json(engine->records(), *root),
                                    "application/json");
             });

  server.Post(
      "/api/v1/btree/insert",
      [&engine, &mutex](const httplib::Request& request, httplib::Response& response) -> void {
        const auto key = body_int(request.body, "key");
        const auto payload = body_string(request.body, "payload");
        if (!key.has_value() || !payload.has_value()) {
          respond_error(response, http_bad_request, "cuerpo invalido: faltan key/payload");
          return;
        }
        const std::scoped_lock lock{mutex};
        try {
          if (const auto existing = engine->btree().search(*key); existing.has_value()) {
            const index::InsertStats stats;
            response.set_content(kravidb::api::insert_to_json(stats, *existing),
                                 "application/json");
            return;
          }
          const kravidb::storage::Tuple tuple{std::vector<kravidb::storage::Field>{
              kravidb::storage::Field{*key},
              kravidb::storage::Field{*payload},
          }};
          const auto bytes = tuple.serialize();
          const auto row_id = engine->records().insert(bytes);
          const auto stats = engine->btree().insert_with_stats(*key, row_id);
          response.set_content(kravidb::api::insert_to_json(stats, row_id), "application/json");
        } catch (const std::exception& error) {
          respond_error(response, http_internal_error, error.what());
        }
      });

  server.Get(
      "/api/v1/btree/search",
      [&engine, &mutex](const httplib::Request& request, httplib::Response& response) -> void {
        const auto key = parse_int(request.get_param_value("key"));
        if (!key.has_value()) {
          respond_error(response, http_bad_request, "parametro key invalido");
          return;
        }
        const std::scoped_lock lock{mutex};
        response.set_content(
            kravidb::api::search_to_json(engine->records(), engine->btree(), engine->table(), *key),
            "application/json");
      });

  server.Get(
      R"(/api/v1/page/(\d+))",
      [&engine, &mutex](const httplib::Request& request, httplib::Response& response) -> void {
        const auto id_text = request.matches.size() > 1 ? request.matches.str(1) : std::string{};
        const auto page_number = parse_int(id_text);
        if (!page_number.has_value() || *page_number < 1) {
          respond_error(response, http_bad_request, "identificador de pagina invalido");
          return;
        }
        const std::scoped_lock lock{mutex};
        const auto identifier = static_cast<kravidb::storage::PageID>(*page_number - 1);
        if (identifier >= engine->records().page_count()) {
          respond_error(response, http_not_found, "pagina inexistente");
          return;
        }
        response.set_content(kravidb::api::page_to_json(engine->records(), identifier),
                             "application/json");
      });

  server.Post("/api/v1/reset",
              [&engine, &current_degree, &mutex](const httplib::Request&,
                                                 httplib::Response& response) -> void {
                const std::scoped_lock lock{mutex};
                engine = make_engine(current_degree);
                response.set_content(R"({"status":"ok"})", "application/json");
              });

  const bool has_web_root = std::filesystem::is_directory(web_root);
  if (has_web_root) {
    server.set_mount_point("/", std::string{web_root});
  }

  // Se reserva el puerto antes de anunciarlo: si ya hay otra instancia, cpp-httplib
  // (con SO_REUSEPORT desactivado) falla aqui y el arranque se aborta con un aviso.
  if (!server.bind_to_port("0.0.0.0", port)) {
    log_line(
        std::format("no se pudo escuchar en el puerto {}: probablemente ya hay otra instancia "
                    "de kravidb_api corriendo (mata la anterior y reintenta)",
                    port));
    return 1;
  }

  log_line(std::format("kravidb api listening on http://0.0.0.0:{}", port));
  if (has_web_root) {
    log_line(std::format("web visualizer mounted from {} (open http://localhost:{} in a browser)",
                         web_root, port));
  } else {
    log_line("web/dist not found: serving the api only");
    log_line(std::format("build it with 'VITE_USE_MOCK=false bun run build' in web/ and restart"));
  }
  return server.listen_after_bind() ? 0 : 1;
}

}  // namespace kravidb::api
