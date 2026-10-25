/// \file
/// \brief Handlers puros de la API, independientes del transporte HTTP.

export module api.handlers;

import std;
export import storage.engine;
import storage.schema;
import storage.table;
import storage.tuple;

export namespace kravidb::api {

/// \brief Grado del arbol B con el que arranca la API.
inline constexpr int default_engine_degree = 2;

/// \brief Grado minimo admitido para el arbol B.
inline constexpr int minimum_engine_degree = 2;

/// \brief Grado maximo admitido para el arbol B.
inline constexpr int maximum_engine_degree = 64;

/// \brief Codigo HTTP de una respuesta correcta.
inline constexpr int http_ok = 200;

/// \brief Codigo HTTP de una respuesta sin contenido.
inline constexpr int http_no_content = 204;

/// \brief Codigo HTTP de una peticion invalida.
inline constexpr int http_bad_request = 400;

/// \brief Codigo HTTP de un recurso inexistente.
inline constexpr int http_not_found = 404;

/// \brief Codigo HTTP de una clave primaria duplicada.
inline constexpr int http_conflict = 409;

/// \brief Codigo HTTP de un error interno.
inline constexpr int http_internal_error = 500;

/// \brief Respuesta logica de una ruta: estado HTTP y cuerpo JSON.
struct ApiResponse {
  /// \brief Codigo HTTP de la respuesta.
  int status{http_ok};

  /// \brief Cuerpo JSON de la respuesta.
  std::string body;
};

/// \brief Crea el motor sembrado que usa la API.
/// \param degree Grado minimo del arbol B.
/// \return Un motor nuevo con las filas de demostracion y el esquema de usuarios.
[[nodiscard]] auto make_engine(int degree) -> std::unique_ptr<storage::StorageEngine>;

/// \brief Reconstruye el motor con otro grado, preservando sus tuplas.
/// \param engine Motor a reemplazar.
/// \param degree Grado minimo del arbol B.
/// \note Los `RowID` cambian porque las paginas son append-only.
auto rebuild_engine(std::unique_ptr<storage::StorageEngine>& engine, int degree) -> void;

/// \brief Esquema que expone la API: clave entera y payload de texto.
/// \return El esquema `(key Int PK, payload Varchar)`.
[[nodiscard]] auto users_schema() -> storage::Schema;

/// \brief Construye la carga util JSON de un registro semilla.
/// \param key Clave primaria del registro.
/// \param name Nombre que se incorpora al payload.
/// \return El payload JSON con el nombre escapado.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto seed_payload(std::int64_t key, std::string_view name) -> std::string;

/// \brief Respuesta de `GET /api/v1/health`.
/// \return Estado correcto con `{"status":"ok"}`.
[[nodiscard]] auto health_response() -> ApiResponse;

/// \brief Respuesta de `GET /api/v1/btree`.
/// \param engine Motor del que se serializa el arbol.
/// \return El arbol completo, o `null` si no tiene raiz.
[[nodiscard]] auto btree_response(const storage::StorageEngine& engine) -> ApiResponse;

/// \brief Respuesta de `GET /api/v1/config`.
/// \param degree Grado actual del arbol.
/// \return La configuracion activa.
[[nodiscard]] auto config_response(int degree) -> ApiResponse;

/// \brief Respuesta de `POST /api/v1/config`.
/// \param engine Motor a reconstruir.
/// \param current_degree Grado activo, actualizado si el cambio es valido.
/// \param body Cuerpo JSON con el campo `degree`.
/// \return La configuracion nueva, o un error 400/500.
[[nodiscard]] auto change_degree(std::unique_ptr<storage::StorageEngine>& engine,
                                 int& current_degree, std::string_view body) -> ApiResponse;

/// \brief Respuesta de `POST /api/v1/btree/insert`.
/// \param engine Motor donde se inserta.
/// \param body Cuerpo JSON con los campos `key` y `payload`.
/// \return Las estadisticas de la insercion, o un error 400/409/500.
[[nodiscard]] auto insert_response(storage::StorageEngine& engine, std::string_view body)
    -> ApiResponse;

/// \brief Respuesta de `GET /api/v1/btree/search`.
/// \param engine Motor sobre el que se busca.
/// \param key_text Clave recibida como texto.
/// \return La traza de busqueda, o un error 400.
[[nodiscard]] auto search_response(const storage::StorageEngine& engine, std::string_view key_text)
    -> ApiResponse;

/// \brief Respuesta de `GET /api/v1/page/:id`.
/// \param engine Motor del que se lee la pagina.
/// \param page_text Numero de pagina en base 1.
/// \return El volcado de la pagina, o un error 400/404.
[[nodiscard]] auto page_response(const storage::StorageEngine& engine, std::string_view page_text)
    -> ApiResponse;

/// \brief Respuesta de `POST /api/v1/reset`.
/// \param engine Motor a reemplazar.
/// \param degree Grado del motor nuevo.
/// \return `{"status":"ok"}` con el motor re-sembrado.
[[nodiscard]] auto reset_response(std::unique_ptr<storage::StorageEngine>& engine, int degree)
    -> ApiResponse;

}  // namespace kravidb::api
