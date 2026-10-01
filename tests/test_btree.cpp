#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using kravidb::index::BTree;
using kravidb::index::BTreeNode;
using kravidb::index::Key;
using kravidb::index::RowID;

// El grado minimo permitido: fuerza divisiones en hojas y nodos internos.
constexpr int minimum_degree = 2;
constexpr int medium_degree = 4;
// El grado por defecto del motor: caso realista de baja altura.
constexpr int wide_degree = kravidb::index::recommended_degree;

constexpr Key arbitrary_key = 42;
constexpr RowID arbitrary_row = 7;

constexpr std::size_t ten_thousand = 10'000;
constexpr std::size_t hundred_thousand = 100'000;
constexpr std::uint64_t random_seed = 20261001ULL;

// Recorre el arbol in-order y acumula las claves en `out`.
void collect_keys(const BTreeNode* node, std::vector<Key>& out) {
  if (node == nullptr) {
    return;
  }
  const auto& keys = node->keys();
  if (node->is_leaf()) {
    out.insert(out.end(), keys.begin(), keys.end());
    return;
  }
  const auto& children = node->children();
  for (std::size_t index = 0; index < keys.size(); ++index) {
    collect_keys(children.at(index), out);
    out.push_back(keys.at(index));
  }
  collect_keys(children.at(keys.size()), out);
}

[[nodiscard]] auto in_order_keys(const BTree& tree) -> std::vector<Key> {
  std::vector<Key> keys;
  collect_keys(tree.root(), keys);
  return keys;
}

// Verifica de forma recursiva todas las invariantes estructurales del B-Tree.
void expect_invariants(const BTreeNode* node, int degree, bool is_root) {
  ASSERT_NE(node, nullptr) << "nodo nulo alcanzado durante el recorrido";

  EXPECT_EQ(node->min_degree(), degree);
  EXPECT_TRUE(node->has_valid_shape());
  EXPECT_LE(node->key_count(), node->max_keys());
  EXPECT_EQ(node->key_count(), node->row_ids().size());

  if (!is_root) {
    EXPECT_GE(node->key_count(), node->min_keys());
  }

  const auto& keys = node->keys();
  for (std::size_t index = 0; index + 1 < keys.size(); ++index) {
    EXPECT_LE(keys.at(index), keys.at(index + 1));
  }

  if (!node->is_leaf()) {
    EXPECT_EQ(node->child_count(), node->key_count() + 1);
    for (const BTreeNode* child : node->children()) {
      expect_invariants(child, degree, false);
    }
  }
}

// Cota superior teorica de la altura para `count` claves y grado `degree`:
// 2 * t^h - 1 es el minimo de claves en un arbol de altura h.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto height_upper_bound(std::size_t count, int degree) -> std::size_t {
  const auto degree_size = static_cast<std::size_t>(degree);
  std::size_t height = 0;
  std::size_t power = 1;
  while ((2 * power) - 1 < count) {
    power *= degree_size;
    ++height;
  }
  return height;
}

// Inserta `count` claves en orden aleatorio (semilla fija), verifica invariantes,
// que todas las claves son recuperables y que la altura respeta la cota teorica.
void run_massive_insertion(std::size_t count, int degree) {
  BTree tree{degree};

  std::vector<Key> keys(count);
  std::ranges::iota(keys, Key{0});
  // La semilla fija hace la prueba reproducible; el orden no necesita ser seguro.
  // NOLINTNEXTLINE(bugprone-random-generator-seed, cert-msc32-c, cert-msc51-cpp)
  std::mt19937_64 generator{random_seed};
  std::ranges::shuffle(keys, generator);

  for (const Key key : keys) {
    tree.insert(key, static_cast<RowID>(key) + 1);
  }

  EXPECT_FALSE(tree.empty());
  expect_invariants(tree.root(), degree, true);
  EXPECT_LE(tree.height(), height_upper_bound(count, degree));

  const auto ordered = in_order_keys(tree);
  ASSERT_EQ(ordered.size(), count);

  std::vector<Key> expected(count);
  std::ranges::iota(expected, Key{0});
  EXPECT_TRUE(std::ranges::equal(ordered, expected));

  for (const Key key : keys) {
    const auto found = tree.search(key);
    ASSERT_TRUE(found.has_value()) << "clave no encontrada: " << key;
    EXPECT_EQ(*found, static_cast<RowID>(key) + 1);
  }
}

TEST(BTreeConstruction, ExposesDegreeAndRoot) {
  const BTree tree{medium_degree};
  EXPECT_EQ(tree.min_degree(), medium_degree);
  ASSERT_NE(tree.root(), nullptr);
  EXPECT_EQ(tree.root()->min_degree(), medium_degree);
  EXPECT_TRUE(tree.root()->is_leaf());
}

TEST(BTreeConstruction, RejectsDegreeBelowMinimum) {
  EXPECT_THROW(BTree{1}, std::invalid_argument);
}

TEST(BTreeEmpty, StartsEmptyAndSearchMisses) {
  const BTree tree{minimum_degree};
  EXPECT_TRUE(tree.empty());
  EXPECT_EQ(tree.height(), 0U);
  ASSERT_NE(tree.root(), nullptr);
  EXPECT_TRUE(tree.root()->is_leaf());
  EXPECT_EQ(tree.root()->key_count(), 0U);
  EXPECT_FALSE(tree.search(arbitrary_key).has_value());
  expect_invariants(tree.root(), minimum_degree, true);
}

TEST(BTreeSingle, InsertThenSearch) {
  BTree tree{minimum_degree};
  tree.insert(arbitrary_key, arbitrary_row);

  EXPECT_FALSE(tree.empty());
  EXPECT_EQ(tree.height(), 0U);

  const auto found = tree.search(arbitrary_key);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(*found, arbitrary_row);

  EXPECT_FALSE(tree.search(arbitrary_key + 1).has_value());
  expect_invariants(tree.root(), minimum_degree, true);
}

TEST(BTreeSplit, LeafSplitKeepsInvariants) {
  BTree tree{minimum_degree};
  constexpr std::size_t count = 5;

  for (std::size_t index = 0; index < count; ++index) {
    tree.insert(static_cast<Key>(index), static_cast<RowID>(index));
  }

  EXPECT_GT(tree.height(), 0U);
  expect_invariants(tree.root(), minimum_degree, true);

  const auto ordered = in_order_keys(tree);
  ASSERT_EQ(ordered.size(), count);
  for (std::size_t index = 0; index < count; ++index) {
    EXPECT_EQ(ordered.at(index), static_cast<Key>(index));
    const auto found = tree.search(static_cast<Key>(index));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, static_cast<RowID>(index));
  }
}

TEST(BTreeSplit, MultipleInternalSplits) {
  BTree tree{minimum_degree};
  constexpr std::size_t count = 100;

  for (std::size_t index = 0; index < count; ++index) {
    tree.insert(static_cast<Key>(index), static_cast<RowID>(index));
  }

  EXPECT_GE(tree.height(), 2U);
  EXPECT_GT(tree.root()->key_count(), 0U);
  expect_invariants(tree.root(), minimum_degree, true);

  const auto ordered = in_order_keys(tree);
  ASSERT_EQ(ordered.size(), count);
  EXPECT_TRUE(std::ranges::is_sorted(ordered));
}

TEST(BTreeEdge, ExtremeKeys) {
  BTree tree{medium_degree};
  const std::array<Key, 5> keys{
      std::numeric_limits<Key>::min(),     std::numeric_limits<Key>::min() + 1, 0,
      std::numeric_limits<Key>::max() - 1, std::numeric_limits<Key>::max(),
  };

  for (std::size_t index = 0; index < keys.size(); ++index) {
    tree.insert(keys.at(index), static_cast<RowID>(index) + 1);
  }

  expect_invariants(tree.root(), medium_degree, true);

  const auto ordered = in_order_keys(tree);
  EXPECT_TRUE(std::ranges::is_sorted(ordered));
  EXPECT_EQ(ordered.size(), keys.size());

  for (std::size_t index = 0; index < keys.size(); ++index) {
    const auto found = tree.search(keys.at(index));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, static_cast<RowID>(index) + 1);
  }
}

TEST(BTreeEdge, DuplicateKeysAreStoredStructurally) {
  BTree tree{minimum_degree};
  constexpr std::size_t count = 8;

  for (std::size_t index = 0; index < count; ++index) {
    tree.insert(arbitrary_key, static_cast<RowID>(index) + 1);
  }

  expect_invariants(tree.root(), minimum_degree, true);

  const auto ordered = in_order_keys(tree);
  EXPECT_EQ(ordered.size(), count);
  EXPECT_TRUE(std::ranges::all_of(ordered, [](Key key) { return key == arbitrary_key; }));

  // El contrato no define cual duplicado devuelve `search`, solo que sea uno valido.
  const auto found = tree.search(arbitrary_key);
  ASSERT_TRUE(found.has_value());
  EXPECT_GE(*found, RowID{1});
  EXPECT_LE(*found, static_cast<RowID>(count));
}

TEST(BTreeStats, RecordsNodeAccessesAndComparisons) {
  BTree tree{minimum_degree};
  constexpr std::size_t count = 50;

  for (std::size_t index = 0; index < count; ++index) {
    tree.insert(static_cast<Key>(index), static_cast<RowID>(index));
  }
  ASSERT_GT(tree.height(), 0U);

  tree.reset_search_stats();
  const auto hit = tree.search_with_stats(static_cast<Key>(count - 1));
  ASSERT_TRUE(hit.row_id.has_value());
  EXPECT_GT(hit.stats.node_accesses, 0U);
  EXPECT_GT(hit.stats.key_comparisons, 0U);

  // `search` usa el mismo algoritmo y debe exponer las mismas metricas.
  tree.reset_search_stats();
  const auto via_search = tree.search(static_cast<Key>(count - 1));
  ASSERT_TRUE(via_search.has_value());
  EXPECT_EQ(tree.last_search_stats().node_accesses, hit.stats.node_accesses);
  EXPECT_EQ(tree.last_search_stats().key_comparisons, hit.stats.key_comparisons);

  tree.reset_search_stats();
  EXPECT_FALSE(tree.search(static_cast<Key>(count)).has_value());
  EXPECT_GT(tree.last_search_stats().node_accesses, 0U);

  tree.reset_search_stats();
  EXPECT_EQ(tree.last_search_stats().node_accesses, 0U);
  EXPECT_EQ(tree.last_search_stats().key_comparisons, 0U);
}

TEST(BTreeLifecycle, ClearResetsToEmptyTree) {
  BTree tree{minimum_degree};
  constexpr std::size_t count = 20;

  for (std::size_t index = 0; index < count; ++index) {
    tree.insert(static_cast<Key>(index), static_cast<RowID>(index));
  }
  ASSERT_FALSE(tree.empty());

  tree.clear();

  EXPECT_TRUE(tree.empty());
  EXPECT_EQ(tree.height(), 0U);
  ASSERT_NE(tree.root(), nullptr);
  EXPECT_EQ(tree.root()->key_count(), 0U);
  EXPECT_FALSE(tree.search(0).has_value());
  expect_invariants(tree.root(), minimum_degree, true);
}

TEST(BTreeLifecycle, MoveTransfersOwnership) {
  BTree source{minimum_degree};
  constexpr std::size_t count = 20;

  for (std::size_t index = 0; index < count; ++index) {
    source.insert(static_cast<Key>(index), static_cast<RowID>(index));
  }

  BTree moved{std::move(source)};

  EXPECT_FALSE(moved.empty());
  expect_invariants(moved.root(), minimum_degree, true);
  EXPECT_EQ(in_order_keys(moved).size(), count);
  for (std::size_t index = 0; index < count; ++index) {
    EXPECT_TRUE(moved.search(static_cast<Key>(index)).has_value());
  }

  // Se inspecciona a proposito el estado valido pero no especificado del origen.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_TRUE(source.empty());
  EXPECT_EQ(source.root(), nullptr);
}

TEST(BTreeMassive, TenThousandKeys) {
  run_massive_insertion(ten_thousand, minimum_degree);
  run_massive_insertion(ten_thousand, wide_degree);
}

TEST(BTreeMassive, HundredThousandKeys) {
  run_massive_insertion(hundred_thousand, minimum_degree);
  run_massive_insertion(hundred_thousand, wide_degree);
}

}  // namespace
