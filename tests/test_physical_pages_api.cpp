#include <gtest/gtest.h>

import std;
import kravidb;
import api.pages;

namespace {

using namespace kravidb::storage;
using namespace kravidb::api;

constexpr RelFileNode page_relation{.tablespace = 0, .database = 1, .relation = 17000};
constexpr LSN real_lsn = 987654321;

auto stored_page() -> Page {
  Page page;
  auto header = page.header();
  header.pd_lsn = real_lsn;
  header.pd_upper = static_cast<std::uint16_t>(Page::size - sizeof(std::int64_t));
  page.set_header(header);
  ItemId item;
  item.set_offset(header.pd_upper);
  item.set_length(sizeof(std::int64_t));
  item.set_flags(ItemIdFlag::normal);
  static_cast<void>(page.add_item(item));
  page.set_checksum();
  return page;
}

TEST(PhysicalPageApi, ReturnsActualHeaderItemsAndFullHex) {
  MemoryPageSource source;
  NoopWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  const auto page = stored_page();
  source.write(page_relation, Fork::main, 0, page.bytes());
  const auto response = physical_page_response(pool, source, "17000", "0");
  ASSERT_EQ(response.status, http_ok);
  EXPECT_NE(response.body.find("\"pd_lsn\":" + std::to_string(real_lsn)), std::string::npos);
  EXPECT_NE(response.body.find("\"pd_lower\":" + std::to_string(page.header().pd_lower)),
            std::string::npos);
  EXPECT_NE(response.body.find("\"offset\":" + std::to_string(page.item(0).offset())),
            std::string::npos);
  EXPECT_NE(response.body.find("\"length\":8"), std::string::npos);
  EXPECT_NE(response.body.find("\"flags\":1"), std::string::npos);
  constexpr std::string_view hex_prefix{"\"raw_hex\":\""};
  const auto start = response.body.find(hex_prefix);
  ASSERT_NE(start, std::string::npos);
  EXPECT_EQ(response.body.find('"', start + hex_prefix.size()) - start - hex_prefix.size(),
            Page::size * 2);
  EXPECT_EQ(pool.stats().pinned_pages, 0U);
  EXPECT_EQ(pool.stats().writes, 0U);
}

TEST(PhysicalPageApi, RejectsMissingPagesAndMalformedIds) {
  MemoryPageSource source;
  NoopWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  EXPECT_EQ(physical_page_response(pool, source, "17000", "0").status, http_not_found);
  source.write(page_relation, Fork::main, 0, Page{}.bytes());
  EXPECT_EQ(physical_page_response(pool, source, "17000", "1").status, http_not_found);
  for (const auto invalid : {"", "-1", "1abc", "../17000", "4294967296"}) {
    EXPECT_EQ(physical_page_response(pool, source, invalid, "0").status, http_bad_request);
    EXPECT_EQ(physical_page_response(pool, source, "17000", invalid).status, http_bad_request);
  }
}

TEST(PhysicalPageApi, CorruptionIsAnErrorNotSyntheticData) {
  MemoryPageSource source;
  NoopWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  auto page = stored_page();
  auto header = page.header();
  ++header.pd_lsn;  // Do not update checksum.
  page.set_header(header);
  source.write(page_relation, Fork::main, 0, page.bytes());
  const auto response = physical_page_response(pool, source, "17000", "0");
  EXPECT_EQ(response.status, http_internal_error);
  EXPECT_NE(response.body.find("XX001"), std::string::npos);
  EXPECT_EQ(pool.stats().pinned_pages, 0U);
}

TEST(PhysicalPageApi, MetadataNamesRealModeDirectoryAndPageSize) {
  const auto metadata = page_service_metadata(std::filesystem::path{"published"});
  EXPECT_NE(metadata.find("\"mode\":\"readonly_pages\""), std::string::npos);
  EXPECT_NE(metadata.find("\"data_dir\":\"published\""), std::string::npos);
  EXPECT_NE(metadata.find("\"page_size\":8192"), std::string::npos);
}

TEST(PhysicalPageApi, LegacyMemoryPagesDoNotInventLsn) {
  const auto engine = make_engine(default_engine_degree);
  const auto response = page_response(*engine, "1");
  ASSERT_EQ(response.status, http_ok);
  EXPECT_NE(response.body.find("\"lsn\":0"), std::string::npos);
}

}  // namespace
