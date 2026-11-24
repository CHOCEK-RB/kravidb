/// \file
/// \brief Read-only inspection of physical 8 KiB relation pages.

export module api.pages;

import std;
export import api.handlers;
export import storage.buffer_pool;

export namespace kravidb::api {

/// \brief Serializes the real header, line pointers and bytes of a valid page.
[[nodiscard]] auto physical_page_to_json(const storage::Page& page, storage::RelFileNode relation,
                                         storage::BlockNumber block) -> std::string;

/// \brief Reads a zero-based block of the numeric relation requested by the client.
///
/// The owner must keep the published relation immutable while it is inspected.
/// Inputs are numeric IDs, never filesystem paths. Missing blocks are 404,
/// malformed IDs are 400, and invalid checksums/layouts are 500 with XX001.
[[nodiscard]] auto physical_page_response(storage::BufferPool& pool,
                                          const storage::PageSource& source,
                                          std::string_view relation_text,
                                          std::string_view block_text) -> ApiResponse;

/// \brief Read-only page-inspection health/config metadata, with no demo data.
[[nodiscard]] auto page_service_metadata(const std::filesystem::path& root) -> std::string;

}  // namespace kravidb::api
