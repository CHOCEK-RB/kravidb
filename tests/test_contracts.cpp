#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using kravidb::index::MemoryBTree;
using kravidb::storage::AttributeDesc;
using kravidb::storage::Fork;
using kravidb::storage::invalid_oid;
using kravidb::storage::LSN;
using kravidb::storage::MemoryPageSource;
using kravidb::storage::NoopTransactionManager;
using kravidb::storage::NoopWal;
using kravidb::storage::Oid;
using kravidb::storage::physical_page_size;
using kravidb::storage::SchemaCatalog;
using kravidb::storage::Snapshot;
using kravidb::storage::TableDef;
using kravidb::storage::Transaction;
using kravidb::storage::WalRecord;
using kravidb::storage::Xid;

constexpr std::byte first_marker{0xAB};
constexpr std::byte last_marker{0xCD};
constexpr std::byte fill_marker{0xFF};
constexpr std::uint32_t written_block = 7;
constexpr std::uint32_t unwritten_block = 3;
constexpr std::int64_t index_key = 7;
constexpr std::uint64_t index_row = 42;

TEST(MemoryPageSource, RoundTripsTheSameBytes) {
  MemoryPageSource source;
  std::array<std::byte, physical_page_size> written{};
  written.at(0) = first_marker;
  written.at(physical_page_size - 1) = last_marker;
  source.write(Fork::main, written_block, written);

  std::array<std::byte, physical_page_size> read{};
  source.read(Fork::main, written_block, read);

  EXPECT_EQ(read, written);
}

TEST(MemoryPageSource, ReadsUnwrittenBlocksAsZeroes) {
  MemoryPageSource source;
  std::array<std::byte, physical_page_size> page{};
  page.fill(fill_marker);
  source.read(Fork::main, unwritten_block, page);
  EXPECT_TRUE(std::ranges::all_of(page, [](std::byte value) { return value == std::byte{0}; }));
}

TEST(NoopWal, HandsOutIncreasingLsnWithoutStoringRecords) {
  NoopWal wal;
  const LSN first = wal.insert(WalRecord{});
  const LSN second = wal.insert(WalRecord{});
  EXPECT_LT(first, second);
  EXPECT_FALSE(wal.needs_flush(second));
  EXPECT_THROW(static_cast<void>(wal.read(first)), std::logic_error);
}

TEST(NoopTransactionManager, AssignsIncreasingXidsAndSnapshots) {
  NoopTransactionManager manager;
  const Xid first = manager.begin_statement();
  const Xid second = manager.begin_statement();
  EXPECT_LT(first, second);

  const Snapshot snapshot = manager.snapshot_for(second);
  EXPECT_EQ(snapshot.xmin, second);
  EXPECT_EQ(snapshot.xmax, second + 1);
  EXPECT_TRUE(snapshot.xip.empty());
}

TEST(SchemaCatalog, CreatesAndLooksUpTables) {
  SchemaCatalog catalog;
  const TableDef definition{
      .name = "users",
      .attributes = {AttributeDesc{.name = "key"}, AttributeDesc{.name = "payload"}},
      .primary_key = "key",
  };
  Transaction transaction{.xid = 1};
  const Oid oid = catalog.create_table(transaction, definition);
  EXPECT_NE(oid, invalid_oid);

  const auto relation = catalog.lookup_relation(transaction, "users");
  ASSERT_TRUE(relation.has_value());
  EXPECT_EQ(relation->oid, oid);
  ASSERT_EQ(relation->attributes.size(), 2U);
  EXPECT_EQ(relation->attributes.at(0).name, "key");
  EXPECT_EQ(relation->attributes.at(0).attnum, 1);
  EXPECT_EQ(relation->attributes.at(1).attnum, 2);
  EXPECT_FALSE(catalog.lookup_relation(transaction, "missing").has_value());
  EXPECT_FALSE(catalog.lookup_attribute(transaction, oid, "missing").has_value());
}

TEST(SchemaCatalog, CreatesIndexesOnExistingRelations) {
  SchemaCatalog catalog;
  Transaction transaction{.xid = 1};
  const TableDef definition{
      .name = "users",
      .attributes = {AttributeDesc{.name = "key"}},
      .primary_key = "key",
  };
  const Oid relation = catalog.create_table(transaction, definition);

  const Oid index = catalog.create_index(
      transaction, {.relation = relation, .name = "users_pkey", .column = "key", .unique = true});
  EXPECT_NE(index, invalid_oid);

  const auto stored = catalog.lookup_relation(transaction, "users");
  ASSERT_TRUE(stored.has_value());
  ASSERT_EQ(stored->indexes.size(), 1U);
  EXPECT_EQ(stored->indexes.at(0), index);

  EXPECT_THROW(static_cast<void>(catalog.create_index(
                   transaction, {.relation = 999, .name = "orphan", .column = "key"})),
               std::invalid_argument);
}

TEST(MemoryBTree, ServesAsTheInMemoryIndex) {
  MemoryBTree tree{2};
  tree.insert(index_key, index_row);
  const auto found = tree.search(index_key);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(*found, index_row);
  EXPECT_FALSE(tree.search(index_key + 1).has_value());
}

}  // namespace
