module api.pages;

import std;
import api.json;
import api.request;

namespace kravidb::api {
namespace {

auto page_error(int status, std::string_view state, std::string_view message) -> ApiResponse {
  return {
      .status = status,
      .body = "{\"sqlstate\":" + json_quote(state) + ",\"message\":" + json_quote(message) + "}"};
}

auto page_hex(const storage::Page& page) -> std::string {
  constexpr std::string_view digits{"0123456789abcdef"};
  constexpr unsigned int nibble_bits = 4;
  constexpr unsigned int nibble_mask = 0x0FU;
  std::string hex;
  hex.reserve(storage::Page::size * 2);
  for (const auto byte : page.bytes()) {
    const auto value = std::to_integer<unsigned int>(byte);
    hex.push_back(digits.at(value >> nibble_bits));
    hex.push_back(digits.at(value & nibble_mask));
  }
  return hex;
}

}  // namespace

auto physical_page_to_json(const storage::Page& page, storage::RelFileNode relation,
                           storage::BlockNumber block) -> std::string {
  if (!page.validate()) {
    // Imported exception inheritance is not visible to some clang-tidy versions.
    // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
    throw std::invalid_argument{"physical page checksum or layout is invalid"};
  }
  const auto header = page.header();
  std::string json = std::format(
      R"({{"relation":{},"block":{},"page_size":{},"header":{{"pd_lsn":{},"pd_checksum":{},"pd_flags":{},"pd_lower":{},"pd_upper":{},"pd_special":{},"pd_pagesize_version":{},"pd_prune_xid":{}}},"items":[)",
      relation.relation, block, storage::Page::size, header.pd_lsn, header.pd_checksum,
      header.pd_flags, header.pd_lower, header.pd_upper, header.pd_special,
      header.pd_pagesize_version, header.pd_prune_xid);
  for (std::uint16_t slot = 0; slot < page.item_count(); ++slot) {
    if (slot != 0) {
      json += ',';
    }
    const auto item = page.item(slot);
    json += std::format(R"({{"slot":{},"offset":{},"length":{},"flags":{}}})", slot, item.offset(),
                        item.length(), static_cast<unsigned int>(item.flags()));
  }
  json += std::format(R"(],"free_bytes":{},"raw_hex":{}}})", page.free_space(),
                      json_quote(page_hex(page)));
  return json;
}

// These two text IDs mirror the HTTP relation parameter and block path.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
auto physical_page_response(storage::BufferPool& pool, const storage::PageSource& source,
                            std::string_view relation_text, std::string_view block_text)
    -> ApiResponse {
  const auto relation_id = parse_int(relation_text);
  const auto block_id = parse_int(block_text);
  if (!relation_id || *relation_id <= 0 ||
      *relation_id > std::numeric_limits<std::uint32_t>::max() || !block_id || *block_id < 0 ||
      *block_id > std::numeric_limits<storage::BlockNumber>::max()) {
    return page_error(http_bad_request, "22023", "invalid relation or block id");
  }
  const storage::RelFileNode relation{
      .tablespace = 0, .database = 1, .relation = static_cast<std::uint32_t>(*relation_id)};
  const auto block = static_cast<storage::BlockNumber>(*block_id);
  try {
    if (block >= source.block_count(relation, storage::Fork::main)) {
      return page_error(http_not_found, "P0002", "relation or page does not exist");
    }
    auto guard =
        pool.read(storage::BufferTag{.rel = relation, .fork = storage::Fork::main, .block = block});
    guard->lock_shared();
    if (!guard->page().validate()) {
      return page_error(http_internal_error, "XX001",
                        "physical page checksum or layout is invalid");
    }
    return {.status = http_ok, .body = physical_page_to_json(guard->page(), relation, block)};
  } catch (const std::exception& error) {
    return page_error(http_internal_error, "58030", error.what());
  }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

auto page_service_metadata(const std::filesystem::path& root) -> std::string {
  return std::format(R"({{"status":"ok","mode":"readonly_pages","data_dir":{},"page_size":{}}})",
                     json_quote(root.string()), storage::Page::size);
}

}  // namespace kravidb::api
