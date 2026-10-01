export module storage.table;

import std;
export import storage.page_manager;
export import storage.tuple;
export import index.btree;

export namespace kravidb::storage {

class Table {
  public:
  Table() = default;

  void insert(const Tuple& tuple) {
    if (tuple.field_count() == 0) {
      throw std::invalid_argument("Table: la tupla no puede estar vacia");
    }

    const auto* key_ptr = std::get_if<std::int64_t>(&tuple.fields().front());
    if (!key_ptr) {
      throw std::invalid_argument("Table: el primer campo (PK) debe ser de tipo Int");
    }

    const index::Key key = *key_ptr;
    const std::vector<std::byte> record = tuple.serialize();

    const RowID row_id = page_manager_.insert(record);

    btree_.insert(key, row_id);
  }

  [[nodiscard]] std::optional<Tuple> find_by_index(index::Key key) {
    const std::optional<RowID> row_id_opt = btree_.search(key);
    if (!row_id_opt) {
      return std::nullopt;
    }

    const std::optional<std::span<const std::byte>> record = page_manager_.read(*row_id_opt);
    if (!record) {
      return std::nullopt;
    }

    return Tuple::deserialize(*record);
  }

  template <typename Predicate>
  [[nodiscard]] std::vector<Tuple> scan_full(Predicate predicate) const {
    std::vector<Tuple> results;

    for (PageID page_id = 0; page_id < page_manager_.page_count(); ++page_id) {
      const Page& page = page_manager_.page(page_id);
      
      for (SlotID slot_id = 0; slot_id < page.slot_count(); ++slot_id) {
        const std::optional<std::span<const std::byte>> record = page.slot(slot_id);
        if (record) {
          std::optional<Tuple> tuple = Tuple::deserialize(*record);
          if (tuple && predicate(*tuple)) {
            results.push_back(std::move(*tuple));
          }
        }
      }
    }

    return results;
  }

  [[nodiscard]] PageManager& page_manager() noexcept { return page_manager_; }
  [[nodiscard]] const PageManager& page_manager() const noexcept { return page_manager_; }

  [[nodiscard]] index::BTree& index() noexcept { return btree_; }
  [[nodiscard]] const index::BTree& index() const noexcept { return btree_; }

  private:
  PageManager page_manager_;
  index::BTree btree_;
};

}  // namespace kravidb::storage
