#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using kravidb::storage::ItemId;
using kravidb::storage::ItemIdFlag;
using kravidb::storage::Page;
using kravidb::storage::page_layout_version;
using kravidb::storage::PageHeader;
using kravidb::storage::pd_all_visible;
using kravidb::storage::pd_page_full;

constexpr std::uint16_t sample_offset{0x1234};
constexpr std::uint16_t sample_length{0x0ABC};
constexpr std::uint16_t tuple_offset{8184};
constexpr std::uint16_t tuple_length{8};
constexpr std::size_t corrupted_byte{4096};
constexpr std::byte corruption{0xFF};
constexpr std::uint32_t flags_shift{15};
constexpr std::uint32_t length_shift{17};
constexpr std::uint32_t prune_xid{7};
constexpr std::size_t header_size{24};
constexpr std::size_t item_id_size{4};
constexpr std::size_t page_size{8192};

TEST(Page, EmptyPagePassesValidation) {
  const Page page;
  const auto header = page.header();

  EXPECT_EQ(header.pd_lsn, 0U);
  EXPECT_EQ(header.pd_checksum, page.checksum());
  EXPECT_EQ(header.pd_flags, 0U);
  EXPECT_EQ(header.pd_lower, header_size);
  EXPECT_EQ(header.pd_upper, page_size);
  EXPECT_EQ(header.pd_special, page_size);
  EXPECT_EQ(header.pd_pagesize_version, page_size | page_layout_version);
  EXPECT_EQ(header.pd_prune_xid, 0U);
  EXPECT_EQ(page.item_count(), 0U);
  EXPECT_EQ(page.free_space(), page_size - header_size);
  EXPECT_TRUE(page.verify_checksum());
  EXPECT_TRUE(page.validate());
}

TEST(Page, LayoutSizesAreFixed) {
  EXPECT_EQ(sizeof(PageHeader), header_size);
  EXPECT_EQ(sizeof(ItemId), item_id_size);
  EXPECT_EQ(sizeof(Page), page_size);
  EXPECT_EQ(Page::size, page_size);
}

TEST(ItemId, PacksOffsetFlagsAndLength) {
  ItemId item;
  item.set_offset(sample_offset);
  item.set_flags(ItemIdFlag::normal);
  item.set_length(sample_length);

  EXPECT_EQ(item.offset(), sample_offset);
  EXPECT_EQ(item.flags(), ItemIdFlag::normal);
  EXPECT_EQ(item.length(), sample_length);
  EXPECT_EQ(ItemId::from_raw(item.raw()).offset(), sample_offset);
  EXPECT_EQ(ItemId::from_raw(item.raw()).flags(), ItemIdFlag::normal);
  EXPECT_EQ(ItemId::from_raw(item.raw()).length(), sample_length);

  const auto expected = static_cast<std::uint32_t>(sample_offset) |
                        (static_cast<std::uint32_t>(ItemIdFlag::normal) << flags_shift) |
                        (static_cast<std::uint32_t>(sample_length) << length_shift);
  EXPECT_EQ(item.raw(), expected);
}

TEST(Page, AddItemGrowsTheLineTable) {
  Page page;
  auto header = page.header();
  header.pd_upper = tuple_offset;
  page.set_header(header);

  ItemId item;
  item.set_offset(tuple_offset);
  item.set_flags(ItemIdFlag::normal);
  item.set_length(tuple_length);

  const auto slot = page.add_item(item);

  EXPECT_EQ(slot, 0U);
  EXPECT_EQ(page.item_count(), 1U);
  EXPECT_EQ(page.header().pd_lower, header_size + item_id_size);
  EXPECT_EQ(page.item(0).offset(), tuple_offset);
  EXPECT_EQ(page.item(0).flags(), ItemIdFlag::normal);
  EXPECT_EQ(page.item(0).length(), tuple_length);
  EXPECT_EQ(page.free_space(), tuple_offset - (header_size + item_id_size));
  EXPECT_THROW(static_cast<void>(page.item(1)), std::out_of_range);

  page.set_checksum();
  EXPECT_TRUE(page.validate());
}

TEST(Page, SerializationRoundTripsByteByByte) {
  Page page;
  auto header = page.header();
  header.pd_flags = static_cast<std::uint16_t>(pd_all_visible | pd_page_full);
  header.pd_prune_xid = prune_xid;
  header.pd_upper = tuple_offset;
  page.set_header(header);

  ItemId item;
  item.set_offset(tuple_offset);
  item.set_flags(ItemIdFlag::normal);
  item.set_length(tuple_length);
  page.add_item(item);
  page.set_checksum();

  const auto restored = Page::from_bytes(page.bytes());

  EXPECT_TRUE(std::ranges::equal(restored.bytes(), page.bytes()));
  EXPECT_EQ(restored.header().pd_flags, static_cast<std::uint16_t>(pd_all_visible | pd_page_full));
  EXPECT_EQ(restored.header().pd_prune_xid, prune_xid);
  EXPECT_EQ(restored.header().pd_checksum, page.header().pd_checksum);
  EXPECT_TRUE(restored.validate());
}

TEST(Page, ChecksumDetectsCorruption) {
  Page page;
  EXPECT_TRUE(page.validate());

  std::ranges::fill(page.mutable_bytes().subspan(corrupted_byte, 1), corruption);

  EXPECT_FALSE(page.verify_checksum());
  EXPECT_FALSE(page.validate());
}

}  // namespace
