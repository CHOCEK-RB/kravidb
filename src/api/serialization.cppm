/// \file
/// \brief Serializers from the kravidb engine to the frontend JSON contract.

export module api.serialization;

import std;
import storage.record_store;
import storage.row_id;
import storage.table;
import storage.tuple;
import index.btree;
import index.btree_node;
import index.index;
import api.json;

export namespace kravidb::api {

/// \brief Serializes a B-tree node and its whole subtree.
/// \param records Store to read each key payload from.
/// \param node Root node of the subtree to serialize.
/// \return The node in the frontend `BTreeNode` format.
[[nodiscard]] auto node_to_json(const storage::RecordStore& records, const index::BTreeNode& node)
    -> std::string;

/// \brief Serializes a slotted page.
/// \param records Store to read the slots from.
/// \param page_id Page identifier.
/// \return The page in the frontend `SlottedPageData` format.
[[nodiscard]] auto page_to_json(const storage::RecordStore& records, storage::PageID page_id)
    -> std::string;

/// \brief Serializes the search metrics (Index Scan versus Full Scan).
/// \param records Store to read the found tuple from.
/// \param tree Index to measure the descent on.
/// \param table Table to measure the full scan on.
/// \param key Key to search for.
/// \return The metrics in the frontend `SearchMetrics` format.
[[nodiscard]] auto search_to_json(const storage::RecordStore& records, const index::BTree& tree,
                                  const storage::Table& table, index::Key key) -> std::string;

/// \brief Serializes the result of an insertion.
/// \param stats Stats of the insertion into the index.
/// \param row_id Location of the newly written tuple.
/// \return The result in the frontend `InsertResult` format.
[[nodiscard]] auto insert_to_json(const index::InsertStats& stats, storage::RowID row_id)
    -> std::string;

}  // namespace kravidb::api
