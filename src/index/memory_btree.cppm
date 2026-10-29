/// \file
/// \brief In-memory index adapter.

export module index.memory_btree;

import std;
export import index.btree;

export namespace kravidb::index {

/// \brief In-memory index used by tests and by the current engine.
///
/// The CLRS B-tree is the only implementation today; it becomes the test
/// double once the durable B-tree exists. The alias keeps one implementation
/// instead of duplicating the logic.
using MemoryBTree = BTree;

}  // namespace kravidb::index
