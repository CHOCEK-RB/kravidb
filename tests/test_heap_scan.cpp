#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using namespace kravidb::storage;

constexpr RelFileNode scan_relation{.tablespace = 0, .database = 1, .relation = 17000};
constexpr std::int64_t reopened_key = 7;

auto append_row(Page& page, BlockNumber block, const std::vector<std::optional<Field>>& fields,
                std::uint32_t xmax = 0) -> void {
  const auto tuple = HeapTuple::from_fields(
      fields,
      HeapTupleHeader{
          .t_xmin = 1, .t_xmax = xmax, .t_ctid = {.block = block, .offset = page.item_count()}});
  auto header = page.header();
  header.pd_upper = static_cast<std::uint16_t>(header.pd_upper - tuple.size());
  page.set_header(header);
  std::ranges::copy(tuple.bytes(), page.mutable_bytes().subspan(header.pd_upper).begin());
  ItemId item;
  item.set_offset(header.pd_upper);
  item.set_length(static_cast<std::uint16_t>(tuple.size()));
  item.set_flags(ItemIdFlag::normal);
  static_cast<void>(page.add_item(item));
  page.set_checksum();
}

TEST(HeapScan, ReadsMultiplePagesNullsDecimalsAndOwnedSlots) {
  MemoryPageSource source;
  NoopWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  Page first;
  const Decimal amount{.scale = 2, .unscaled = 1234};
  append_row(first, 0, {Field{std::int64_t{1}}, std::nullopt, Field{amount}});
  append_row(first, 0, {Field{std::int64_t{2}}, Field{std::string{"Ada"}}, std::nullopt});
  source.write(scan_relation, Fork::main, 0, first.bytes());
  source.write(scan_relation, Fork::main, 1, Page{}.bytes());
  Page last;
  append_row(last, 2, {Field{std::int64_t{3}}});
  source.write(scan_relation, Fork::main, 2, last.bytes());

  HeapScan scan{pool, source, scan_relation};
  const auto row = scan.next();
  ASSERT_TRUE(row);
  EXPECT_EQ(row->tid, (ItemPointerData{.block = 0, .offset = 0}));
  ASSERT_EQ(row->values.size(), 3U);
  EXPECT_FALSE(row->values.at(1));
  EXPECT_EQ(std::get<Decimal>(*row->values.at(2)), amount);
  ASSERT_TRUE(scan.next());
  const auto final_row = scan.next();
  ASSERT_TRUE(final_row);
  EXPECT_EQ(final_row->tid.block, 2U);
  EXPECT_FALSE(scan.next());
  EXPECT_FALSE(scan.next());
  EXPECT_EQ(std::get<std::int64_t>(*row->values.at(0)), 1);
  EXPECT_EQ(scan.stats().page_fetches, 3U);
  EXPECT_EQ(scan.stats().items_examined, 3U);
  EXPECT_EQ(scan.stats().rows_returned, 3U);
  EXPECT_EQ(pool.stats().misses, 3U);
  EXPECT_EQ(pool.stats().pinned_pages, 0U);
  EXPECT_EQ(pool.stats().writes, 0U);
}

TEST(HeapScan, EmptyRelationAndEmptyPageProduceNoRows) {
  MemoryPageSource source;
  NoopWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  HeapScan empty{pool, source, scan_relation};
  EXPECT_FALSE(empty.next());
  EXPECT_EQ(empty.stats().page_fetches, 0U);
  source.write(scan_relation, Fork::main, 0, Page{}.bytes());
  HeapScan scan{pool, source, scan_relation};
  EXPECT_FALSE(scan.next());
  EXPECT_EQ(scan.stats().page_fetches, 1U);
}

TEST(HeapScan, ReleasesPinWhenStoppedEarly) {
  MemoryPageSource source;
  NoopWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  Page page;
  append_row(page, 0, {Field{std::int64_t{1}}});
  source.write(scan_relation, Fork::main, 0, page.bytes());
  {
    HeapScan scan{pool, source, scan_relation};
    ASSERT_TRUE(scan.next());
    EXPECT_EQ(pool.stats().pinned_pages, 1U);
  }
  EXPECT_EQ(pool.stats().pinned_pages, 0U);
}

TEST(HeapScan, RejectsCorruptPageAndVersionedHeap) {
  MemoryPageSource source;
  NoopWal wal;
  Page broken;
  auto header = broken.header();
  header.pd_upper = 0;
  broken.set_header(header);
  source.write(scan_relation, Fork::main, 0, broken.bytes());
  {
    ClockSweepBufferPool pool{1, source, wal};
    HeapScan scan{pool, source, scan_relation};
    EXPECT_THROW(static_cast<void>(scan.next()), SqlStateError);
    EXPECT_EQ(pool.stats().pinned_pages, 0U);
  }
  Page versioned;
  append_row(versioned, 0, {Field{std::int64_t{1}}}, 2);
  source.write(scan_relation, Fork::main, 0, versioned.bytes());
  ClockSweepBufferPool pool{1, source, wal};
  HeapScan scan{pool, source, scan_relation};
  try {
    static_cast<void>(scan.next());
    FAIL() << "expected unsupported visibility error";
  } catch (const SqlStateError& error) {
    EXPECT_EQ(error.sqlstate(), "0A000");
  }
}

TEST(HeapScan, ReadsFileBackedHeapAfterReopen) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("kravidb-scan-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  {
    DirectoryPageSource writer{root};
    Page page;
    append_row(page, 0, {Field{reopened_key}, std::nullopt});
    writer.write(scan_relation, Fork::main, 0, page.bytes());
    writer.fsync_all();
  }
  {
    DirectoryPageSource source{root};
    NoopWal wal;
    ClockSweepBufferPool pool{1, source, wal};
    HeapScan scan{pool, source, scan_relation};
    const auto row = scan.next();
    ASSERT_TRUE(row);
    EXPECT_EQ(std::get<std::int64_t>(*row->values.at(0)), reopened_key);
    EXPECT_FALSE(row->values.at(1));
    EXPECT_FALSE(scan.next());
  }
  std::filesystem::remove_all(root);
}

}  // namespace
