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
using kravidb::storage::pd_has_free_lines;
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
constexpr std::size_t lsn_offset{0};
constexpr std::size_t checksum_offset{8};
constexpr std::size_t flags_offset{10};
constexpr std::size_t lower_offset{12};
constexpr std::size_t upper_offset{14};
constexpr std::size_t special_offset{16};
constexpr std::size_t pagesize_version_offset{18};
constexpr std::size_t prune_xid_offset{20};
constexpr std::uint64_t header_lsn{0x1122334455667788};
constexpr std::uint16_t header_checksum{0x1234};
constexpr std::uint16_t header_flags{0xABCD};
constexpr std::uint16_t header_lower{0x0024};
constexpr std::uint16_t header_upper{0x1F00};
constexpr std::uint16_t header_special{0x2000};
constexpr std::uint16_t header_pagesize_version{0x2001};
constexpr std::uint32_t header_prune_xid{0xDEADBEEF};
constexpr std::uint16_t free_lines_flag{0x0002};

template <typename T>
auto read_at(std::span<const std::byte> bytes, std::size_t offset) -> T {
  std::array<std::byte, sizeof(T)> raw{};
  std::ranges::copy(bytes.subspan(offset, sizeof(T)), raw.begin());
  return std::bit_cast<T>(raw);
}

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

TEST(Page, HeaderBytesMatchDocumentedOffsets) {
  Page page;
  auto header = page.header();
  header.pd_lsn = header_lsn;
  header.pd_checksum = header_checksum;
  header.pd_flags = header_flags;
  header.pd_lower = header_lower;
  header.pd_upper = header_upper;
  header.pd_special = header_special;
  header.pd_pagesize_version = header_pagesize_version;
  header.pd_prune_xid = header_prune_xid;
  page.set_header(header);

  const auto bytes = page.bytes();

  EXPECT_EQ(read_at<std::uint64_t>(bytes, lsn_offset), header_lsn);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, checksum_offset), header_checksum);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, flags_offset), header_flags);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, lower_offset), header_lower);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, upper_offset), header_upper);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, special_offset), header_special);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, pagesize_version_offset), header_pagesize_version);
  EXPECT_EQ(read_at<std::uint32_t>(bytes, prune_xid_offset), header_prune_xid);
}

TEST(Page, FreeLinesFlagRoundTrips) {
  Page page;
  auto header = page.header();
  header.pd_flags = pd_has_free_lines;
  page.set_header(header);
  page.set_checksum();

  const auto restored = Page::from_bytes(page.bytes());

  EXPECT_EQ(pd_has_free_lines, free_lines_flag);
  EXPECT_EQ(restored.header().pd_flags, pd_has_free_lines);
  EXPECT_TRUE(restored.validate());
}

}  // namespace
