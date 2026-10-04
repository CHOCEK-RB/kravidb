#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using kravidb::index::BTree;
using kravidb::index::Key;
using kravidb::storage::decode_row_id;
using kravidb::storage::default_page_size;
using kravidb::storage::encode_row_id;
using kravidb::storage::Field;
using kravidb::storage::PageID;
using kravidb::storage::PageManager;
using kravidb::storage::RowID;
using kravidb::storage::RowLocation;
using kravidb::storage::SlotID;
using kravidb::storage::StorageEngine;
using kravidb::storage::Table;
using kravidb::storage::Tuple;

// Pagina diminuta para forzar el salto a una pagina nueva con pocos registros.
constexpr std::size_t tiny_page = 64;

[[nodiscard]] auto bytes_of(std::string_view text) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  out.reserve(text.size());
  for (const char character : text) {
    out.push_back(static_cast<std::byte>(character));
  }
  return out;
}

[[nodiscard]] auto key_of(const Tuple& tuple) -> Key {
  const auto* key = std::get_if<std::int64_t>(&tuple.fields().front());
  if (key == nullptr) {
    throw std::invalid_argument{"la tupla no tiene una PK entera"};
  }
  return *key;
}

// ---------------------------------------------------------------------------
// RowID
// ---------------------------------------------------------------------------

TEST(RowId, RoundTripsLocations) {
  constexpr RowLocation location{.page_id = 7, .slot_id = 42};
  EXPECT_EQ(decode_row_id(encode_row_id(location)), location);
}

TEST(RowId, PacksPageInHighBitsAndSlotInLowBits) {
  constexpr RowLocation location{.page_id = 1, .slot_id = 0};
  const RowID encoded = encode_row_id(location);
  EXPECT_EQ(encoded >> kravidb::storage::slot_bits, RowID{1});
  EXPECT_EQ(encoded & kravidb::storage::slot_mask, RowID{0});
}

TEST(RowId, HandlesExtremeValues) {
  constexpr RowLocation location{
      .page_id = std::numeric_limits<std::uint32_t>::max(),
      .slot_id = std::numeric_limits<std::uint32_t>::max(),
  };
  EXPECT_EQ(decode_row_id(encode_row_id(location)), location);
}

// ---------------------------------------------------------------------------
// Tuple
// ---------------------------------------------------------------------------

TEST(Tuple, RoundTripsIntAndVarchar) {
  const Tuple tuple{std::vector<Field>{std::int64_t{-1}, std::string{"hola"}}};
  const auto buffer = tuple.serialize();

  EXPECT_EQ(buffer.size(), tuple.serialized_size());

  const auto restored = Tuple::deserialize(buffer);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(*restored, tuple);
}

TEST(Tuple, RoundTripsEmptyTupleAndEmptyVarchar) {
  const Tuple empty{};
  const auto restored_empty = Tuple::deserialize(empty.serialize());
  ASSERT_TRUE(restored_empty.has_value());
  EXPECT_EQ(restored_empty->field_count(), 0U);

  const Tuple with_empty_text{std::vector<Field>{std::string{}}};
  const auto restored_text = Tuple::deserialize(with_empty_text.serialize());
  ASSERT_TRUE(restored_text.has_value());
  EXPECT_EQ(*restored_text, with_empty_text);
}

TEST(Tuple, RoundTripsExtremeIntegers) {
  const Tuple tuple{std::vector<Field>{
      std::numeric_limits<std::int64_t>::min(),
      std::numeric_limits<std::int64_t>::max(),
  }};
  const auto restored = Tuple::deserialize(tuple.serialize());
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(*restored, tuple);
}

TEST(Tuple, SerializedSizeMatchesLayout) {
  const auto text = std::string{"abc"};
  const Tuple tuple{std::vector<Field>{std::int64_t{-1}, text}};

  const std::size_t count_prefix = sizeof(std::uint32_t);
  const std::size_t int_field = sizeof(std::uint8_t) + sizeof(std::int64_t);
  const std::size_t varchar_field = sizeof(std::uint8_t) + sizeof(std::uint32_t) + text.size();

  EXPECT_EQ(tuple.serialized_size(), count_prefix + int_field + varchar_field);
  EXPECT_EQ(tuple.serialize().size(), tuple.serialized_size());
}

TEST(Tuple, DeserializeRejectsEveryTruncation) {
  const Tuple tuple{std::vector<Field>{std::int64_t{-1}, std::string{"abcdef"}}};
  const auto buffer = tuple.serialize();

  for (std::size_t length = 0; length < buffer.size(); ++length) {
    const auto truncated = Tuple::deserialize(std::span<const std::byte>(buffer).first(length));
    EXPECT_FALSE(truncated.has_value()) << "longitud truncada: " << length;
  }
}

TEST(Tuple, DeserializeRejectsUnknownFieldType) {
  const Tuple tuple{std::vector<Field>{std::int64_t{-1}}};
  auto buffer = tuple.serialize();

  constexpr std::size_t type_offset = sizeof(std::uint32_t);
  constexpr auto unknown_field_type = std::byte{9};
  ASSERT_GT(buffer.size(), type_offset);
  buffer.at(type_offset) = unknown_field_type;

  EXPECT_FALSE(Tuple::deserialize(buffer).has_value());
}

TEST(Tuple, DeserializeIgnoresTrailingBytes) {
  const Tuple tuple{std::vector<Field>{std::int64_t{-1}}};
  auto buffer = tuple.serialize();
  buffer.push_back(std::byte{0});

  const auto restored = Tuple::deserialize(buffer);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(*restored, tuple);
}

// ---------------------------------------------------------------------------
// PageManager
// ---------------------------------------------------------------------------

TEST(PageManager, DefaultsToConfiguredPageSize) {
  const PageManager store{};
  EXPECT_EQ(store.page_size(), default_page_size);
  EXPECT_EQ(store.page_count(), 0U);
}

TEST(PageManager, InsertAndReadRoundTrip) {
  PageManager store{tiny_page};
  const auto payload = bytes_of("registro");

  const RowID row = store.insert(payload);
  const RowLocation location = decode_row_id(row);

  EXPECT_EQ(location.page_id, 0U);
  EXPECT_EQ(location.slot_id, 0U);
  EXPECT_EQ(store.page_count(), 1U);

  const auto read = store.read(row);
  ASSERT_TRUE(read.has_value());
  EXPECT_TRUE(std::ranges::equal(*read, payload));
}

TEST(PageManager, OpensNewPageWhenCurrentIsFull) {
  PageManager store{tiny_page};
  const auto payload = bytes_of("0123456789");
  constexpr std::size_t count = 8;

  std::vector<RowID> rows;
  rows.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    rows.push_back(store.insert(payload));
  }

  EXPECT_GE(store.page_count(), 2U);
  for (const RowID row : rows) {
    const auto read = store.read(row);
    ASSERT_TRUE(read.has_value());
    EXPECT_TRUE(std::ranges::equal(*read, payload));
  }
}

TEST(PageManager, RejectsRecordLargerThanPage) {
  PageManager store{tiny_page};
  const auto oversized = bytes_of(std::string(tiny_page, 'x'));

  EXPECT_THROW((void)store.insert(oversized), std::length_error);
  EXPECT_EQ(store.page_count(), 0U);
}

TEST(PageManager, SlotAtMatchesReadWithoutCountingRead) {
  PageManager store{tiny_page};
  const auto payload = bytes_of("abc");
  const RowID row = store.insert(payload);
  const RowLocation location = decode_row_id(row);

  store.reset_stats();
  const auto via_slot = store.slot_at(location.page_id, location.slot_id);
  ASSERT_TRUE(via_slot.has_value());
  EXPECT_TRUE(std::ranges::equal(*via_slot, payload));
  EXPECT_EQ(store.stats().page_reads, 0U);

  const auto via_read = store.read(row);
  ASSERT_TRUE(via_read.has_value());
  EXPECT_TRUE(std::ranges::equal(*via_read, payload));
  EXPECT_EQ(store.stats().page_reads, 1U);
}

TEST(PageManager, SlotCountAndInvalidAccess) {
  PageManager store{tiny_page};
  const auto payload = bytes_of("abc");
  const RowID first = store.insert(payload);
  (void)store.insert(payload);

  const RowLocation location = decode_row_id(first);
  const std::size_t slots = store.slot_count(location.page_id);
  EXPECT_EQ(slots, 2U);
  EXPECT_EQ(store.slot_count(std::numeric_limits<PageID>::max()), 0U);

  store.reset_stats();
  const auto beyond_last = store.slot_at(location.page_id, static_cast<SlotID>(slots));
  EXPECT_FALSE(beyond_last.has_value());
  EXPECT_EQ(store.stats().page_reads, 0U);
}

TEST(PageManager, ReadInvalidPageDoesNotCountRead) {
  PageManager store{};
  store.reset_stats();

  const RowID row = encode_row_id(RowLocation{.page_id = 0, .slot_id = 0});
  EXPECT_FALSE(store.read(row).has_value());
  EXPECT_EQ(store.stats().page_reads, 0U);
}

TEST(PageManager, TracksWritesAndReads) {
  PageManager store{tiny_page};
  EXPECT_EQ(store.stats().page_writes, 0U);

  const RowID row = store.insert(bytes_of("dato"));
  EXPECT_EQ(store.stats().page_writes, 1U);

  (void)store.read(row);
  EXPECT_EQ(store.stats().page_reads, 1U);

  store.reset_stats();
  EXPECT_EQ(store.stats().page_reads, 0U);
  EXPECT_EQ(store.stats().page_writes, 0U);
}

TEST(PageManager, ContiguousBufferSlottedLayout) {
  PageManager store{tiny_page};
  const auto payload1 = bytes_of("primero");
  const auto payload2 = bytes_of("segundo");

  const RowID row1 = store.insert(payload1);
  const RowID row2 = store.insert(payload2);

  const auto read1 = store.read(row1);
  const auto read2 = store.read(row2);
  ASSERT_TRUE(read1.has_value());
  ASSERT_TRUE(read2.has_value());

  // Las tuplas crecen hacia atras desde el final del buffer contiguo:
  // el primer registro esta en una direccion de memoria superior al segundo.
  EXPECT_GT(read1->data(), read2->data());
  EXPECT_TRUE(std::ranges::equal(*read1, payload1));
  EXPECT_TRUE(std::ranges::equal(*read2, payload2));
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------

TEST(Table, InsertThenFindByIndex) {
  PageManager records{};
  BTree index{};
  Table table{records, index};

  const Tuple tuple{std::vector<Field>{std::int64_t{1}, std::string{"uno"}}};
  table.insert(tuple);

  const auto found = table.find_by_index(1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(*found, tuple);
  EXPECT_FALSE(table.find_by_index(2).has_value());
}

TEST(Table, InsertRejectsEmptyTuple) {
  PageManager records{};
  BTree index{};
  Table table{records, index};

  EXPECT_THROW(table.insert(Tuple{}), std::invalid_argument);
}

TEST(Table, InsertRejectsNonIntPrimaryKey) {
  PageManager records{};
  BTree index{};
  Table table{records, index};

  const Tuple tuple{std::vector<Field>{std::string{"clave"}}};
  EXPECT_THROW(table.insert(tuple), std::invalid_argument);
}

TEST(Table, ScanAllReturnsEveryTupleInInsertionOrder) {
  PageManager records{tiny_page};
  BTree index{};
  Table table{records, index};
  constexpr std::size_t count = 10;

  for (std::size_t index = 0; index < count; ++index) {
    table.insert(Tuple{std::vector<Field>{
        static_cast<std::int64_t>(index),
        std::string{"v"},
    }});
  }

  EXPECT_GT(records.page_count(), 1U);

  const auto all = table.scan_all();
  ASSERT_EQ(all.size(), count);
  for (std::size_t index = 0; index < count; ++index) {
    EXPECT_EQ(key_of(all.at(index)), static_cast<Key>(index));
  }
}

TEST(Table, ScanAllDoesNotCountPageReads) {
  PageManager records{};
  BTree index{};
  Table table{records, index};
  table.insert(Tuple{std::vector<Field>{std::int64_t{1}, std::string{"uno"}}});

  records.reset_stats();
  (void)table.scan_all();
  EXPECT_EQ(records.stats().page_reads, 0U);
}

// ---------------------------------------------------------------------------
// StorageEngine (integracion #9: index scan a traves de Table)
// ---------------------------------------------------------------------------

TEST(StorageEngine, EndToEndViaTableAndIndex) {
  StorageEngine engine{};
  constexpr std::size_t count = 100;

  for (std::size_t index = 0; index < count; ++index) {
    engine.table().insert(Tuple{std::vector<Field>{
        static_cast<std::int64_t>(index),
        std::string{"fila"},
    }});
  }

  for (std::size_t index = 0; index < count; ++index) {
    const auto found = engine.table().find_by_index(static_cast<Key>(index));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(key_of(*found), static_cast<Key>(index));
  }

  EXPECT_FALSE(engine.table().find_by_index(static_cast<Key>(count)).has_value());
  EXPECT_EQ(engine.table().scan_all().size(), count);
}

TEST(StorageEngine, IndexResolvesToStoredRecord) {
  StorageEngine engine{};
  const Tuple expected{std::vector<Field>{std::int64_t{1}, std::string{"uno"}}};
  engine.table().insert(expected);

  const auto row = engine.index().search(1);
  ASSERT_TRUE(row.has_value());

  const auto stored = engine.records().read(*row);
  ASSERT_TRUE(stored.has_value());

  const auto restored = Tuple::deserialize(*stored);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(*restored, expected);
}

}  // namespace
