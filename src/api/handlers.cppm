/// \file
/// \brief Pure API handlers, independent of the HTTP transport.

export module api.handlers;

import std;
export import storage.engine;
import storage.schema;
import storage.table;
import storage.tuple;

export namespace kravidb::api {

/// \brief B-tree degree the API starts with.
/// \details It starts at 2 so the visualizer shows splits; the index uses
/// `index::recommended_degree` (64) by default.
inline constexpr int default_engine_degree = 2;

/// \brief Minimum degree accepted for the B-tree.
inline constexpr int minimum_engine_degree = 2;

/// \brief Maximum degree accepted for the B-tree.
inline constexpr int maximum_engine_degree = 64;

/// \brief HTTP code of a successful response.
inline constexpr int http_ok = 200;

/// \brief HTTP code of a no-content response.
inline constexpr int http_no_content = 204;

/// \brief HTTP code of an invalid request.
inline constexpr int http_bad_request = 400;

/// \brief HTTP code of a not-found resource.
inline constexpr int http_not_found = 404;

/// \brief HTTP code of a duplicate primary key.
inline constexpr int http_conflict = 409;

/// \brief HTTP code of an internal error.
inline constexpr int http_internal_error = 500;

/// \brief Logical response of a route: HTTP status and JSON body.
struct ApiResponse {
  /// \brief HTTP code of the response.
  int status{http_ok};

  /// \brief JSON body of the response.
  std::string body;
};

/// \brief Creates the seeded engine the API uses.
/// \param degree Minimum B-tree degree.
/// \return A new engine with the demo rows and the users schema.
[[nodiscard]] auto make_engine(int degree) -> std::unique_ptr<storage::StorageEngine>;

/// \brief Rebuilds the engine with another degree, preserving its tuples.
/// \param engine Engine to replace.
/// \param degree Minimum B-tree degree.
/// \note `RowID`s change because pages are append-only.
auto rebuild_engine(std::unique_ptr<storage::StorageEngine>& engine, int degree) -> void;

/// \brief Schema the API exposes: integer key and text payload.
/// \return The `(key Int PK, payload Varchar)` schema.
[[nodiscard]] auto users_schema() -> storage::Schema;

/// \brief Builds the JSON payload of a seed record.
/// \param key Primary key of the record.
/// \param name Name added to the payload.
/// \return The JSON payload with the name escaped.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto seed_payload(std::int64_t key, std::string_view name) -> std::string;

/// \brief Response of `GET /api/v1/health`.
/// \return Successful status with `{"status":"ok"}`.
[[nodiscard]] auto health_response() -> ApiResponse;

/// \brief Response of `GET /api/v1/btree`.
/// \param engine Engine whose tree is serialized.
/// \return The whole tree, or `null` if it has no root.
[[nodiscard]] auto btree_response(const storage::StorageEngine& engine) -> ApiResponse;

/// \brief Response of `GET /api/v1/config`.
/// \param degree Current tree degree.
/// \return The active configuration.
[[nodiscard]] auto config_response(int degree) -> ApiResponse;

/// \brief Response of `POST /api/v1/config`.
/// \param engine Engine to rebuild.
/// \param current_degree Active degree, updated when the change is valid.
/// \param body JSON body with the `degree` field.
/// \return The new configuration, or a 400/500 error.
[[nodiscard]] auto change_degree(std::unique_ptr<storage::StorageEngine>& engine,
                                 int& current_degree, std::string_view body) -> ApiResponse;

/// \brief Response of `POST /api/v1/btree/insert`.
/// \param engine Engine the record is inserted into.
/// \param body JSON body with the `key` and `payload` fields.
/// \return The insertion stats, or a 400/409/500 error.
[[nodiscard]] auto insert_response(storage::StorageEngine& engine, std::string_view body)
    -> ApiResponse;

/// \brief Response of `GET /api/v1/btree/search`.
/// \param engine Engine the search runs on.
/// \param key_text Key received as text.
/// \return The search trace, or a 400 error.
[[nodiscard]] auto search_response(const storage::StorageEngine& engine, std::string_view key_text)
    -> ApiResponse;

/// \brief Response of `GET /api/v1/page/:id`.
/// \param engine Engine the page is read from.
/// \param page_text Page number in base 1.
/// \return The page dump, or a 400/404 error.
[[nodiscard]] auto page_response(const storage::StorageEngine& engine, std::string_view page_text)
    -> ApiResponse;

/// \brief Response of `POST /api/v1/reset`.
/// \param engine Engine to replace.
/// \param degree Degree of the new engine.
/// \return `{"status":"ok"}` with the engine seeded again.
[[nodiscard]] auto reset_response(std::unique_ptr<storage::StorageEngine>& engine, int degree)
    -> ApiResponse;

}  // namespace kravidb::api
