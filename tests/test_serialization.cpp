#include <gtest/gtest.h>

import std;
import kravidb;
import api.json;
import api.serialization;

namespace {

using kravidb::index::InsertStats;
using kravidb::index::Key;
using kravidb::storage::StorageEngine;
using kravidb::storage::Tuple;

constexpr std::size_t page_size = 4096;
constexpr int degree = 2;
constexpr Key sample_key = 7;
constexpr Key absent_key = 9;
constexpr std::int64_t promoted_key = 7;
constexpr std::string_view sample_payload = "payload";
constexpr std::string_view sample_value = "value";
constexpr std::size_t first_page = 0;

// Serializa e indexa una tupla {clave, carga util} para poblar el motor.
void seed(StorageEngine& engine, Key key, std::string_view payload) {
  const Tuple tuple{std::vector<kravidb::storage::Field>{key, std::string{payload}}};
  const auto row_id = engine.records().insert(tuple.serialize());
  static_cast<void>(engine.btree().insert_with_stats(key, row_id));
}

TEST(JsonQuote, EscapesControlAndQuoteCharacters) {
  const auto quoted = kravidb::api::json_quote(R"(a"b\n)");
  EXPECT_EQ(quoted, R"("a\"b\\n")");
}

TEST(JsonTuple, ReadsKeyAndValueFromTheFirstFields) {
  const Tuple tuple{std::vector<kravidb::storage::Field>{sample_key, std::string{sample_value}}};
  EXPECT_EQ(kravidb::api::tuple_key(tuple), sample_key);
  EXPECT_EQ(kravidb::api::tuple_value(tuple), sample_value);
}

TEST(Serialization, EmitsNodeWithStableId) {
  StorageEngine engine{page_size, degree};
  seed(engine, sample_key, sample_payload);
  const auto* root = engine.btree().root();
  ASSERT_NE(root, nullptr);
  const auto json = kravidb::api::node_to_json(engine.records(), *root);
  EXPECT_TRUE(json.contains(R"("id":"node-0")"));
  EXPECT_TRUE(json.contains(R"("is_leaf":true)"));
}

TEST(Serialization, EmitsOneBasedPageNumbers) {
  StorageEngine engine{page_size, degree};
  seed(engine, sample_key, sample_payload);
  const auto json = kravidb::api::page_to_json(engine.records(), first_page);
  EXPECT_TRUE(json.contains(R"("page_id":1)"));
  EXPECT_TRUE(json.contains(R"("slot_count":1)"));
  EXPECT_TRUE(json.contains(R"("key":7)"));
}

TEST(Serialization, ReportsSearchHitAndMiss) {
  StorageEngine engine{page_size, degree};
  seed(engine, sample_key, sample_payload);
  const auto hit =
      kravidb::api::search_to_json(engine.records(), engine.btree(), engine.table(), sample_key);
  EXPECT_TRUE(hit.contains(R"("found":true)"));
  const auto miss =
      kravidb::api::search_to_json(engine.records(), engine.btree(), engine.table(), absent_key);
  EXPECT_TRUE(miss.contains(R"("found":false)"));
}

TEST(Serialization, ReportsInsertStats) {
  const InsertStats stats{
      .split_occurred = false,
      .promoted_key = promoted_key,
      .new_root_created = true,
  };
  const auto json = kravidb::api::insert_to_json(stats, kravidb::storage::RowID{0});
  EXPECT_TRUE(json.contains(R"("split_occurred":false)"));
  EXPECT_TRUE(json.contains(R"("new_root_created":true)"));
  EXPECT_TRUE(json.contains(R"("promoted_key":7)"));
}

}  // namespace
