module index.btree;

import std;

namespace kravidb::index {

BTree::BTree(int degree) : degree_{degree}, root_{create_node(true)} {}

BTree::BTree(BTree&& other) noexcept
    : degree_{other.degree_},
      pool_{std::move(other.pool_)},
      root_{std::exchange(other.root_, nullptr)},
      last_search_stats_{other.last_search_stats_} {}

auto BTree::operator=(BTree&& other) noexcept -> BTree& {
  if (this != &other) {
    degree_ = other.degree_;
    pool_ = std::move(other.pool_);
    root_ = std::exchange(other.root_, nullptr);
    last_search_stats_ = other.last_search_stats_;
  }
  return *this;
}

void BTree::clear() {
  pool_.clear();
  root_ = nullptr;
  root_ = create_node(true);
  last_search_stats_ = SearchStats{};
}

auto BTree::empty() const noexcept -> bool {
  return root_ == nullptr || root_->is_empty();
}

auto BTree::height() const noexcept -> std::size_t {
  std::size_t levels = 0;
  for (const BTreeNode* node = root_; node != nullptr && !node->is_leaf();
       node = node->children().front()) {
    ++levels;
  }
  return levels;
}

auto BTree::search_with_stats(Key key) const -> SearchResult {
  SearchResult result{};
  const BTreeNode* node = root_;

  while (node != nullptr) {
    ++result.stats.node_accesses;

    const std::size_t index = node->lower_bound_index(key, result.stats.key_comparisons);

    if (index < node->key_count()) {
      ++result.stats.key_comparisons;
      if (node->keys().at(index) == key) {
        result.row_id = node->row_ids().at(index);
        return result;
      }
    }

    if (node->is_leaf() || index >= node->child_count()) {
      break;
    }
    node = node->children().at(index);
  }
  return result;
}

auto BTree::search(Key key) const -> std::optional<RowID> {
  const SearchResult result = search_with_stats(key);
  last_search_stats_ = result.stats;
  return result.row_id;
}

void BTree::insert(Key key, RowID row_id) {
  BTreeNode* old_root = root_;
  if (old_root->is_full()) {
    BTreeNode* new_root = create_node(false);
    root_ = new_root;
    new_root->children().push_back(old_root);
    split_child(new_root, 0, old_root);
    insert_non_full(new_root, key, row_id);
  } else {
    insert_non_full(old_root, key, row_id);
  }
}

void BTree::split_child(BTreeNode* parent, std::size_t index, BTreeNode* child) {
  BTreeNode* new_child = create_node(child->is_leaf());
  const auto min_degree = static_cast<std::size_t>(degree_);
  const auto split_offset = static_cast<std::ptrdiff_t>(min_degree);

  new_child->keys().assign(child->keys().begin() + split_offset, child->keys().end());
  new_child->row_ids().assign(child->row_ids().begin() + split_offset, child->row_ids().end());

  if (!child->is_leaf()) {
    new_child->children().assign(child->children().begin() + split_offset, child->children().end());
  }

  const Key promoted_key = child->keys().at(min_degree - 1);
  const RowID promoted_row_id = child->row_ids().at(min_degree - 1);

  child->keys().resize(min_degree - 1);
  child->row_ids().resize(min_degree - 1);
  if (!child->is_leaf()) {
    child->children().resize(min_degree);
  }

  const auto key_offset = static_cast<std::ptrdiff_t>(index);
  const auto child_offset = static_cast<std::ptrdiff_t>(index + 1);
  parent->children().insert(parent->children().begin() + child_offset, new_child);
  parent->keys().insert(parent->keys().begin() + key_offset, promoted_key);
  parent->row_ids().insert(parent->row_ids().begin() + key_offset, promoted_row_id);
}

void BTree::insert_non_full(BTreeNode* node, Key key, RowID row_id) {
  const auto position = std::ranges::upper_bound(node->keys(), key);
  auto index = static_cast<std::size_t>(std::distance(node->keys().begin(), position));

  if (node->is_leaf()) {
    const auto offset = static_cast<std::ptrdiff_t>(index);
    node->keys().insert(node->keys().begin() + offset, key);
    node->row_ids().insert(node->row_ids().begin() + offset, row_id);
    return;
  }

  BTreeNode* child = node->children().at(index);
  if (child->is_full()) {
    split_child(node, index, child);
    if (key > node->keys().at(index)) {
      ++index;
    }
  }
  insert_non_full(node->children().at(index), key, row_id);
}

auto BTree::create_node(bool leaf) -> BTreeNode* {
  pool_.push_back(std::make_unique<BTreeNode>(degree_, leaf));
  return pool_.back().get();
}

}  // namespace kravidb::index
