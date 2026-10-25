module api.handlers;

import std;
import api.json;
import api.request;
import api.serialization;
import storage.engine;
import storage.schema;
import storage.table;
import storage.tuple;

namespace kravidb::api {

namespace {

// Longitud del campo `bio` de los registros semilla.
constexpr std::size_t seed_bio_length = 255;

// Construye una respuesta de error JSON.
auto error_response(int status, std::string_view message) -> ApiResponse {
  return ApiResponse{.status = status, .body = "{\"error\":" + json_quote(message) + "}"};
}

// Escribe un registro semilla en el motor.
void seed_row(storage::StorageEngine& engine, std::int64_t key, std::string_view name) {
  const auto payload = seed_payload(key, name);
  const storage::Tuple tuple{std::vector<storage::Field>{
      storage::Field{key},
      storage::Field{payload},
  }};
  engine.table().insert(tuple);
}

}  // namespace

auto users_schema() -> storage::Schema {
  return storage::Schema{std::vector<storage::ColumnDef>{
      storage::ColumnDef{.name = "key", .type = storage::FieldType::Int, .is_primary_key = true},
      storage::ColumnDef{
          .name = "payload", .type = storage::FieldType::Varchar, .is_primary_key = false},
  }};
}

auto seed_payload(std::int64_t key, std::string_view name) -> std::string {
  std::string payload = R"({"id":)" + std::to_string(key);
  payload += R"(,"table":"users","name":)";
  payload += json_quote(name);
  payload += R"(,"bio":")";
  payload += std::string(seed_bio_length, 'x');
  payload += R"("})";
  return payload;
}

auto make_engine(int degree) -> std::unique_ptr<storage::StorageEngine> {
  auto engine =
      std::make_unique<storage::StorageEngine>(storage::default_page_size, degree, users_schema());

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

auto rebuild_engine(std::unique_ptr<storage::StorageEngine>& engine, int degree) -> void {
  const std::vector<storage::Tuple> rows = engine->table().scan_all();
  auto fresh =
      std::make_unique<storage::StorageEngine>(storage::default_page_size, degree, users_schema());
  for (const auto& row : rows) {
    fresh->table().insert(row);
  }
  engine = std::move(fresh);
}

auto health_response() -> ApiResponse {
  return ApiResponse{.status = http_ok, .body = R"({"status":"ok"})"};
}

auto btree_response(const storage::StorageEngine& engine) -> ApiResponse {
  const auto* root = engine.btree().root();
  if (root == nullptr) {
    return ApiResponse{.status = http_ok, .body = "null"};
  }
  return ApiResponse{.status = http_ok, .body = node_to_json(engine.records(), *root)};
}

auto config_response(int degree) -> ApiResponse {
  return ApiResponse{
      .status = http_ok,
      .body = std::format(R"({{"degree":{},"page_size":{}}})", degree, storage::default_page_size)};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto change_degree(std::unique_ptr<storage::StorageEngine>& engine, int& current_degree,
                   std::string_view body) -> ApiResponse {
  const auto requested = body_int(body, "degree");
  if (!requested.has_value() || *requested < minimum_engine_degree ||
      *requested > maximum_engine_degree) {
    return error_response(http_bad_request,
                          std::format("grado invalido: usa un entero entre {} y {}",
                                      minimum_engine_degree, maximum_engine_degree));
  }
  try {
    rebuild_engine(engine, static_cast<int>(*requested));
    current_degree = static_cast<int>(*requested);
  } catch (const std::exception& error) {
    return error_response(http_internal_error, error.what());
  }
  return config_response(current_degree);
}

auto insert_response(storage::StorageEngine& engine, std::string_view body) -> ApiResponse {
  const auto key = body_int(body, "key");
  const auto payload = body_string(body, "payload");
  if (!key.has_value() || !payload.has_value()) {
    return error_response(http_bad_request, "cuerpo invalido: faltan key/payload");
  }
  try {
    if (engine.btree().search(*key).has_value()) {
      return error_response(http_conflict, "clave duplicada");
    }
    const storage::Tuple tuple{std::vector<storage::Field>{
        storage::Field{*key},
        storage::Field{*payload},
    }};
    const auto result = engine.table().insert(tuple);
    return ApiResponse{.status = http_ok,
                       .body = insert_to_json(result.index_stats, result.row_id)};
  } catch (const std::invalid_argument& error) {
    return error_response(http_bad_request, error.what());
  } catch (const std::exception& error) {
    return error_response(http_internal_error, error.what());
  }
}

auto search_response(const storage::StorageEngine& engine, std::string_view key_text)
    -> ApiResponse {
  const auto key = parse_int(key_text);
  if (!key.has_value()) {
    return error_response(http_bad_request, "parametro key invalido");
  }
  return ApiResponse{
      .status = http_ok,
      .body = search_to_json(engine.records(), engine.btree(), engine.table(), *key)};
}

auto page_response(const storage::StorageEngine& engine, std::string_view page_text)
    -> ApiResponse {
  const auto page_number = parse_int(page_text);
  if (!page_number.has_value() || *page_number < 1) {
    return error_response(http_bad_request, "identificador de pagina invalido");
  }
  const auto identifier = static_cast<storage::PageID>(*page_number - 1);
  if (identifier >= engine.records().page_count()) {
    return error_response(http_not_found, "pagina inexistente");
  }
  return ApiResponse{.status = http_ok, .body = page_to_json(engine.records(), identifier)};
}

auto reset_response(std::unique_ptr<storage::StorageEngine>& engine, int degree) -> ApiResponse {
  engine = make_engine(degree);
  return ApiResponse{.status = http_ok, .body = R"({"status":"ok"})"};
}

}  // namespace kravidb::api
