module storage.table;

import std;
import storage.row_id;

namespace kravidb::storage {

void Table::insert(const Tuple& tuple) {
  if (tuple.field_count() == 0) {
    throw std::invalid_argument{"Table: la tupla no puede estar vacia"};
  }

  if (!schema_.empty() && !schema_.validate(tuple)) {
    throw std::invalid_argument{"Table: la tupla no coincide con el esquema"};
  }

  const std::size_t pk_index = schema_.primary_key_index().value_or(0);
  const auto* key_ptr = std::get_if<std::int64_t>(&tuple.fields().at(pk_index));
  if (key_ptr == nullptr) {
    throw std::invalid_argument{"Table: el primer campo (PK) debe ser de tipo Int"};
  }

  const index::Key key = *key_ptr;
  const std::vector<std::byte> record = tuple.serialize();

  const RowID row_id = records_.insert(record);

  index_.insert(key, row_id);
}

auto Table::find_by_index(index::Key key) const -> std::optional<Tuple> {
  const std::optional<RowID> row_id_opt = index_.search(key);
  if (!row_id_opt) {
    return std::nullopt;
  }

  const std::optional<std::span<const std::byte>> record = records_.read(*row_id_opt);
  if (!record) {
    return std::nullopt;
  }

  return Tuple::deserialize(*record);
}

auto Table::scan_all() const -> std::vector<Tuple> {
  std::vector<Tuple> results;

  for (PageID page_id = 0; page_id < records_.page_count(); ++page_id) {
    const std::size_t slot_count = records_.slot_count(page_id);

    for (SlotID slot_id = 0; slot_id < slot_count; ++slot_id) {
      const std::optional<std::span<const std::byte>> record = records_.slot_at(page_id, slot_id);
      if (record) {
        std::optional<Tuple> tuple = Tuple::deserialize(*record);
        if (tuple) {
          results.push_back(std::move(*tuple));
        }
      }
    }
  }

  return results;
}

}  // namespace kravidb::storage
