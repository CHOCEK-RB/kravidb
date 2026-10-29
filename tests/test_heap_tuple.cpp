#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using kravidb::storage::ColumnDef;
using kravidb::storage::Decimal;
using kravidb::storage::Field;
using kravidb::storage::FieldType;
using kravidb::storage::heap_has_null;
using kravidb::storage::heap_has_varwidth;
using kravidb::storage::heap_natts_mask;
using kravidb::storage::HeapTuple;
using kravidb::storage::HeapTupleHeader;
using kravidb::storage::ItemPointerData;
using kravidb::storage::Schema;
using kravidb::storage::SqlStateError;
using kravidb::storage::Tuple;

static_assert(sizeof(HeapTupleHeader) == kravidb::storage::page_header_size);
static_assert(sizeof(ItemPointerData) == sizeof(std::uint32_t) + sizeof(std::uint16_t));

constexpr std::size_t xmin_offset{0};
constexpr std::size_t xmax_offset{4};
constexpr std::size_t cid_offset{8};
constexpr std::size_t ctid_block_offset{12};
constexpr std::size_t ctid_offset_offset{16};
constexpr std::size_t infomask2_offset{18};
constexpr std::size_t infomask_offset{20};
constexpr std::size_t hoff_offset{22};
constexpr std::size_t null_bitmap_offset{24};
constexpr std::uint32_t header_xmin{0x11223344};
constexpr std::uint32_t header_xmax{0x55667788};
constexpr std::uint32_t header_cid{0x99AABBCC};
constexpr std::uint32_t header_ctid_block{0xDDEEFF00};
constexpr std::uint16_t header_ctid_offset{0x1234};
constexpr std::uint16_t header_infomask2{0xF800};
constexpr std::uint16_t header_infomask{0xFFF0};
constexpr std::uint32_t tuple_xmin{7};
constexpr std::uint32_t tuple_xmax{11};
constexpr std::uint32_t tuple_cid{3};
constexpr std::uint32_t tuple_ctid_block{9};
constexpr std::uint16_t tuple_ctid_offset{4};
constexpr std::size_t expected_hoff_with_bitmap{32};
constexpr std::size_t expected_roundtrip_size{61};
constexpr std::uint8_t decimal_scale{2};
constexpr std::int64_t decimal_unscaled{1234};

template <typename T>
auto read_at(std::span<const std::byte> bytes, std::size_t offset) -> T {
  std::array<std::byte, sizeof(T)> raw{};
  std::ranges::copy(bytes.subspan(offset, sizeof(T)), raw.begin());
  return std::bit_cast<T>(raw);
}

auto byte_at(std::span<const std::byte> bytes, std::size_t offset) -> unsigned int {
  return std::to_integer<unsigned int>(bytes.subspan(offset, 1).front());
}

TEST(HeapTuple, HeaderLayoutIsStable) {
  HeapTupleHeader header{};
  header.t_xmin = header_xmin;
  header.t_xmax = header_xmax;
  header.t_cid = header_cid;
  header.t_ctid = ItemPointerData{.block = header_ctid_block, .offset = header_ctid_offset};
  header.t_infomask2 = header_infomask2;
  header.t_infomask = header_infomask;

  const std::vector<std::optional<Field>> no_fields;
  const auto tuple = HeapTuple::from_fields(no_fields, header);
  const auto bytes = tuple.bytes();
  const auto stored = tuple.header();

  EXPECT_EQ(read_at<std::uint32_t>(bytes, xmin_offset), header_xmin);
  EXPECT_EQ(read_at<std::uint32_t>(bytes, xmax_offset), header_xmax);
  EXPECT_EQ(read_at<std::uint32_t>(bytes, cid_offset), header_cid);
  EXPECT_EQ(read_at<std::uint32_t>(bytes, ctid_block_offset), header_ctid_block);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, ctid_offset_offset), header_ctid_offset);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, infomask2_offset), header_infomask2);
  EXPECT_EQ(read_at<std::uint16_t>(bytes, infomask_offset), header_infomask);
  EXPECT_EQ(read_at<std::uint8_t>(bytes, hoff_offset), kravidb::storage::page_header_size);
  EXPECT_EQ(tuple.size(), kravidb::storage::page_header_size);
  EXPECT_EQ(stored.t_infomask2, header_infomask2);
  EXPECT_EQ(stored.t_infomask, header_infomask);
  EXPECT_EQ(stored.t_ctid,
            (ItemPointerData{.block = header_ctid_block, .offset = header_ctid_offset}));
}

TEST(HeapTuple, RoundTripsFieldsAndNullBitmap) {
  const Decimal decimal{.scale = decimal_scale, .unscaled = decimal_unscaled};
  const std::vector<std::optional<Field>> fields{
      Field{std::int64_t{42}},
      std::nullopt,
      Field{std::string{"hello"}},
      Field{decimal},
  };
  HeapTupleHeader header{};
  header.t_xmin = tuple_xmin;
  header.t_xmax = tuple_xmax;
  header.t_cid = tuple_cid;
  header.t_ctid = ItemPointerData{.block = tuple_ctid_block, .offset = tuple_ctid_offset};

  const auto tuple = HeapTuple::from_fields(fields, header);
  const auto stored = tuple.header();

  EXPECT_EQ(tuple.field_count(), 4U);
  EXPECT_TRUE(tuple.is_null(1));
  EXPECT_FALSE(tuple.is_null(0));
  EXPECT_FALSE(tuple.is_null(2));
  EXPECT_FALSE(tuple.is_null(3));
  EXPECT_EQ(stored.t_xmin, tuple_xmin);
  EXPECT_EQ(stored.t_xmax, tuple_xmax);
  EXPECT_EQ(stored.t_cid, tuple_cid);
  EXPECT_EQ(stored.t_ctid,
            (ItemPointerData{.block = tuple_ctid_block, .offset = tuple_ctid_offset}));
  EXPECT_EQ(stored.t_infomask2 & heap_natts_mask, 4U);
  EXPECT_NE(stored.t_infomask & heap_has_null, 0U);
  EXPECT_NE(stored.t_infomask & heap_has_varwidth, 0U);
  EXPECT_EQ(stored.t_hoff, expected_hoff_with_bitmap);
  EXPECT_EQ(tuple.size(), expected_roundtrip_size);
  EXPECT_EQ(byte_at(tuple.bytes(), null_bitmap_offset), 0b1101U);
  EXPECT_TRUE(tuple.fits_in_page());
  EXPECT_EQ(tuple.fields(), fields);

  const auto restored = HeapTuple::from_bytes(tuple.bytes());
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(*restored, tuple);
  EXPECT_EQ(restored->fields(), fields);
  EXPECT_EQ(std::get<Decimal>(restored->fields().at(3).value()), decimal);
}

TEST(HeapTuple, RoundTripsWithoutNulls) {
  const Decimal decimal{.scale = decimal_scale, .unscaled = decimal_unscaled};
  const std::vector<std::optional<Field>> fields{
      Field{std::int64_t{0}},
      Field{std::string{}},
      Field{decimal},
  };

  const auto tuple = HeapTuple::from_fields(fields);

  EXPECT_EQ(tuple.header().t_hoff, kravidb::storage::page_header_size);
  EXPECT_EQ(tuple.header().t_infomask & heap_has_null, 0U);
  EXPECT_EQ(tuple.fields(), fields);

  const auto restored = HeapTuple::from_bytes(tuple.bytes());
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->fields(), fields);
}

TEST(HeapTuple, NullIsDistinctFromZeroAndEmptyString) {
  const std::vector<std::optional<Field>> fields{
      std::nullopt,
      Field{std::int64_t{0}},
      Field{std::string{}},
  };

  const auto tuple = HeapTuple::from_fields(fields);
  const auto decoded = tuple.fields();

  EXPECT_TRUE(tuple.is_null(0));
  EXPECT_FALSE(tuple.is_null(1));
  EXPECT_FALSE(tuple.is_null(2));
  ASSERT_EQ(decoded.size(), 3U);
  EXPECT_FALSE(decoded.at(0).has_value());
  EXPECT_EQ(std::get<std::int64_t>(decoded.at(1).value()), 0);
  EXPECT_EQ(std::get<std::string>(decoded.at(2).value()), "");
  EXPECT_EQ(byte_at(tuple.bytes(), null_bitmap_offset), 0b0110U);
  EXPECT_EQ(tuple.header().t_hoff, expected_hoff_with_bitmap);
}

TEST(HeapTuple, RejectsTupleLargerThanPage) {
  const std::string huge(HeapTuple::max_size, 'x');
  const std::vector<std::optional<Field>> fields{Field{std::string{huge}}};

  try {
    const auto tuple = HeapTuple::from_fields(fields);
    static_cast<void>(tuple);
    FAIL() << "expected SqlStateError";
  } catch (const SqlStateError& error) {
    EXPECT_EQ(error.sqlstate(), "54000");
  }
}

TEST(Schema, AcceptsDecimalColumn) {
  const Schema schema{std::vector<ColumnDef>{
      ColumnDef{.name = "amount", .type = FieldType::Decimal, .is_primary_key = false},
  }};
  const Tuple tuple{std::vector<Field>{
      Field{Decimal{.scale = decimal_scale, .unscaled = decimal_unscaled}},
  }};

  EXPECT_TRUE(schema.validate(tuple));
}

TEST(Tuple, DecimalRoundTrips) {
  const Decimal decimal{.scale = decimal_scale, .unscaled = decimal_unscaled};
  const Tuple tuple{std::vector<Field>{Field{decimal}}};

  const auto restored = Tuple::deserialize(tuple.serialize());

  ASSERT_TRUE(restored.has_value());
  ASSERT_EQ(restored->field_count(), 1U);
  EXPECT_EQ(std::get<Decimal>(restored->fields().at(0)), decimal);
}

}  // namespace
