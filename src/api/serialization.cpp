module api.serialization;

import std;
import storage.record_store;
import storage.row_id;
import storage.table;
import storage.tuple;
import index.btree;
import index.btree_node;
import index.index;
import api.json;

namespace kravidb::api {

namespace {

// Tamano de la cabecera de pagina, en bytes (igual que el mock del frontend).
constexpr std::size_t page_header_bytes = 64;

// Tamano de una ranura de directorio, en bytes.
constexpr std::size_t slot_bytes = sizeof(std::uint32_t);

// Base del LSN sintetico: no hay WAL todavia, se deriva del identificador de pagina.
constexpr std::int64_t lsn_base = 1000;

// Nanosegundos que tiene un microsegundo (el contrato expresa Full Scan en micros).
constexpr std::int64_t nanoseconds_per_microsecond = 1000;

// Nombre del enum de comparacion tal como lo espera el frontend.
auto comparison_name(index::SearchPathStep::Comparison comparison) -> std::string_view {
  switch (comparison) {
    case index::SearchPathStep::Comparison::Equal:
      return "equal";
    case index::SearchPathStep::Comparison::Greater:
      return "greater";
    case index::SearchPathStep::Comparison::Less:
      return "less";
  }
  return "less";
}

// Lee la carga util legible de la tupla ubicada en `row_id`, si existe.
auto record_value(const storage::RecordStore& records, storage::RowID row_id) -> std::string {
  const auto location = storage::decode_row_id(row_id);
  const auto bytes = records.slot_at(location.page_id, location.slot_id);
  if (!bytes.has_value()) {
    return {};
  }
  const auto tuple = storage::Tuple::deserialize(*bytes);
  if (!tuple.has_value()) {
    return {};
  }
  return tuple_value(*tuple);
}

}  // namespace

auto node_to_json(const storage::RecordStore& records, const index::BTreeNode& node)
    -> std::string {
  std::string json = "{\"id\":" + json_quote("node-" + std::to_string(node.id()));
  json += ",\"is_leaf\":";
  json += node.is_leaf() ? "true" : "false";

  json += ",\"keys\":[";
  const auto& keys = node.keys();
  const auto& row_ids = node.row_ids();
  for (std::size_t index = 0; index < keys.size(); ++index) {
    const auto location = storage::decode_row_id(row_ids.at(index));
    if (index > 0) {
      json += ',';
    }
    json += "{\"key\":" + std::to_string(keys.at(index));
    json += ",\"value\":" + json_quote(record_value(records, row_ids.at(index)));
    json += ",\"page_id\":" + std::to_string(location.page_id + 1);
    json += ",\"slot_id\":" + std::to_string(location.slot_id) + "}";
  }
  json += "],\"children\":[";

  const auto& children = node.children();
  for (std::size_t index = 0; index < children.size(); ++index) {
    if (index > 0) {
      json += ',';
    }
    json += node_to_json(records, *children.at(index));
  }
  json += "]}";
  return json;
}

auto page_to_json(const storage::RecordStore& records, storage::PageID page_id) -> std::string {
  const std::size_t slot_count = records.slot_count(page_id);
  const std::size_t page_size = records.page_size();

  struct Slot {
    std::size_t size{0};
    std::string key{"0"};
    std::string data;
  };

  std::vector<Slot> slots;
  slots.reserve(slot_count);
  std::size_t total_bytes = 0;
  for (std::size_t index = 0; index < slot_count; ++index) {
    Slot slot;
    if (const auto bytes = records.slot_at(page_id, static_cast<storage::SlotID>(index));
        bytes.has_value()) {
      slot.size = bytes->size();
      if (const auto tuple = storage::Tuple::deserialize(*bytes); tuple.has_value()) {
        slot.key = std::to_string(tuple_key(*tuple));
        slot.data = tuple_value(*tuple);
      }
      total_bytes += slot.size;
    }
    slots.push_back(std::move(slot));
  }

  const std::size_t free_space_end = page_size > total_bytes ? page_size - total_bytes : 0;
  const std::size_t free_space_offset = page_header_bytes + (slot_count * slot_bytes);
  const std::size_t free_bytes =
      free_space_end > free_space_offset ? free_space_end - free_space_offset : 0;

  const std::size_t display_page = page_id + 1;
  std::string json = "{\"page_id\":" + std::to_string(display_page);
  json += R"(,"header":{"page_id":)" + std::to_string(display_page);
  json += ",\"lsn\":" + std::to_string(lsn_base + static_cast<std::int64_t>(display_page));
  json += ",\"slot_count\":" + std::to_string(slot_count);
  json += ",\"free_space_offset\":" + std::to_string(free_space_offset);
  json += ",\"free_space_end\":" + std::to_string(free_space_end) + "}";

  json += ",\"slots\":[";
  std::size_t running = 0;
  for (std::size_t index = 0; index < slots.size(); ++index) {
    running += slots.at(index).size;
    const std::size_t offset = page_size > running ? page_size - running : 0;
    if (index > 0) {
      json += ',';
    }
    json += "{\"slot_id\":" + std::to_string(index);
    json += ",\"offset\":" + std::to_string(offset);
    json += ",\"length\":" + std::to_string(slots.at(index).size);
    json += ",\"is_deleted\":false}";
  }

  json += "],\"tuples\":[";
  running = 0;
  for (std::size_t index = 0; index < slots.size(); ++index) {
    running += slots.at(index).size;
    const std::size_t offset = page_size > running ? page_size - running : 0;
    if (index > 0) {
      json += ',';
    }
    json += "{\"slot_id\":" + std::to_string(index);
    json += ",\"offset\":" + std::to_string(offset);
    json += ",\"key\":" + slots.at(index).key;
    json += ",\"data\":" + json_quote(slots.at(index).data);
    json += ",\"size_bytes\":" + std::to_string(slots.at(index).size) + "}";
  }

  json += "],\"total_bytes\":" + std::to_string(total_bytes);
  json += ",\"free_bytes\":" + std::to_string(free_bytes) + "}";
  return json;
}

auto search_to_json(const storage::RecordStore& records, const index::BTree& tree,
                    const storage::Table& table, index::Key key) -> std::string {
  using clock = std::chrono::steady_clock;
  using nanoseconds = std::chrono::nanoseconds;

  const auto index_start = clock::now();
  const auto trace = tree.search_trace(key);
  const auto index_end = clock::now();
  const auto index_ns = std::chrono::duration_cast<nanoseconds>(index_end - index_start).count();

  const auto full_start = clock::now();
  const auto scanned = table.scan_all();
  const auto full_end = clock::now();
  const auto full_us = std::chrono::duration_cast<nanoseconds>(full_end - full_start).count() /
                       nanoseconds_per_microsecond;

  std::string json = "{\"key\":" + std::to_string(key);
  json += ",\"found\":";
  json += trace.row_id.has_value() ? "true" : "false";

  if (trace.row_id.has_value()) {
    const auto location = storage::decode_row_id(*trace.row_id);
    json += R"(,"target":{"key":)" + std::to_string(key);
    json += ",\"value\":" + json_quote(record_value(records, *trace.row_id));
    json += ",\"page_id\":" + std::to_string(location.page_id + 1);
    json += ",\"slot_id\":" + std::to_string(location.slot_id) + "}";
  }

  json += ",\"path\":[";
  for (std::size_t index = 0; index < trace.path.size(); ++index) {
    const auto& step = trace.path.at(index);
    if (index > 0) {
      json += ',';
    }
    json += "{\"node_id\":" + json_quote("node-" + std::to_string(step.node_id));
    json += ",\"key_index_checked\":" + std::to_string(step.key_index);
    json += ",\"comparison\":" + json_quote(comparison_name(step.comparison)) + "}";
  }

  json += R"(],"index_scan":{"latency_ns":)" + std::to_string(index_ns);
  json += ",\"page_ios\":" + std::to_string(trace.stats.node_accesses);
  json += ",\"nodes_visited\":" + std::to_string(trace.stats.node_accesses) + "}";

  json += R"(,"full_scan":{"latency_ns":)" + std::to_string(full_us);
  json += ",\"page_ios\":" + std::to_string(records.page_count());
  json += ",\"tuples_scanned\":" + std::to_string(scanned.size()) + "}}";
  return json;
}

auto insert_to_json(const index::InsertStats& stats, storage::RowID row_id) -> std::string {
  const auto location = storage::decode_row_id(row_id);
  std::string json = "{\"split_occurred\":";
  json += stats.split_occurred ? "true" : "false";
  if (stats.promoted_key.has_value()) {
    json += ",\"promoted_key\":" + std::to_string(*stats.promoted_key);
  }
  json += ",\"new_root_created\":";
  json += stats.new_root_created ? "true" : "false";
  json += ",\"target_page_id\":" + std::to_string(location.page_id + 1);
  json += ",\"target_slot_id\":" + std::to_string(location.slot_id) + "}";
  return json;
}

}  // namespace kravidb::api
