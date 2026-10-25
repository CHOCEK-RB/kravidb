#include <gtest/gtest.h>

import std;
import kravidb;
import api.request;
import api.handlers;

namespace {

using kravidb::api::btree_response;
using kravidb::api::change_degree;
using kravidb::api::config_response;
using kravidb::api::field_value;
using kravidb::api::health_response;
using kravidb::api::http_bad_request;
using kravidb::api::http_conflict;
using kravidb::api::http_not_found;
using kravidb::api::http_ok;
using kravidb::api::insert_response;
using kravidb::api::make_engine;
using kravidb::api::page_response;
using kravidb::api::parse_int;
using kravidb::api::parse_string;
using kravidb::api::reset_response;
using kravidb::api::search_response;
using kravidb::api::seed_payload;

constexpr std::int64_t sample_key = 42;

TEST(JsonRequest, ReadsTopLevelKeyIgnoringStringsAndNestedObjects) {
  const auto value = field_value(R"({"payload":"\"key\":99","meta":{"key":7},"key":42})", "key");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "42");
}

TEST(JsonRequest, DoesNotMatchKeyAsSubstring) {
  EXPECT_FALSE(field_value(R"({"skey":1})", "key").has_value());
  EXPECT_FALSE(field_value(R"({"payload":"\"key\":99"})", "key").has_value());
}

TEST(JsonRequest, AllowsWhitespaceAroundMembers) {
  const auto value = field_value(" { \"key\" :\n 42 , \"payload\" : \"x\" } ", "key");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "42");
}

TEST(JsonRequest, RejectsMalformedObjects) {
  EXPECT_FALSE(field_value("", "key").has_value());
  EXPECT_FALSE(field_value(R"({"key" 42})", "key").has_value());
  EXPECT_FALSE(field_value(R"({"key":42)", "key").has_value());
}

TEST(JsonInt, ParsesCompleteSignedValues) {
  EXPECT_EQ(parse_int("42"), 42);
  EXPECT_EQ(parse_int(" -7 "), -7);
  EXPECT_EQ(parse_int("-9223372036854775808"), std::numeric_limits<std::int64_t>::min());
}

TEST(JsonInt, RejectsTrailingTextEmptyAndOverflow) {
  EXPECT_FALSE(parse_int("12abc").has_value());
  EXPECT_FALSE(parse_int("").has_value());
  EXPECT_FALSE(parse_int("   ").has_value());
  EXPECT_FALSE(parse_int("1.5").has_value());
  EXPECT_FALSE(parse_int("12 34").has_value());
  EXPECT_FALSE(parse_int("9223372036854775808").has_value());
}

TEST(JsonString, DecodesEscapes) {
  const auto value = parse_string(R"("a\"b\\c\/d\b\f\n\r\t")");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "a\"b\\c/d\b\f\n\r\t");
}

TEST(JsonString, DecodesUnicodeEscapes) {
  const auto value = parse_string(R"("\u0041\u00e9\u20ac")");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "A\xC3\xA9\xE2\x82\xAC");
}

TEST(JsonString, DecodesSurrogatePairs) {
  const auto value = parse_string(R"("\uD83D\uDE00")");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "\xF0\x9F\x98\x80");
}

TEST(JsonString, RejectsInvalidEscapesControlsAndUnclosedStrings) {
  EXPECT_FALSE(parse_string(R"("\uD800")").has_value());
  EXPECT_FALSE(parse_string(R"("\uDC00")").has_value());
  EXPECT_FALSE(parse_string(R"("\uZZZZ")").has_value());
  EXPECT_FALSE(parse_string(R"("a\qb")").has_value());
  EXPECT_FALSE(parse_string(std::string{"\"a\nb\""}).has_value());
  EXPECT_FALSE(parse_string("\"abc").has_value());
  EXPECT_FALSE(parse_string(R"("a"x)").has_value());
}

TEST(ApiSeed, EscapesNameInPayload) {
  const auto payload = seed_payload(7, R"(Ada "Lovelace" \ )");
  const auto raw_name = field_value(payload, "name");
  ASSERT_TRUE(raw_name.has_value());
  const auto name = parse_string(*raw_name);
  ASSERT_TRUE(name.has_value());
  EXPECT_EQ(*name, R"(Ada "Lovelace" \ )");
}

TEST(ApiHandlers, ReportsHealthAndConfig) {
  const auto health = health_response();
  EXPECT_EQ(health.status, http_ok);
  EXPECT_EQ(health.body, R"({"status":"ok"})");

  const auto config = config_response(4);
  EXPECT_EQ(config.status, http_ok);
  EXPECT_EQ(config.body, R"({"degree":4,"page_size":4096})");
}

TEST(ApiHandlers, SerializesTheTree) {
  const auto engine = make_engine(2);
  const auto response = btree_response(*engine);
  EXPECT_EQ(response.status, http_ok);
  EXPECT_TRUE(response.body.contains(R"("id":"node-0")"));
}

TEST(ApiHandlers, InsertsAndSearchesARow) {
  auto engine = make_engine(2);
  const auto pages_before = engine->records().page_count();

  const auto inserted = insert_response(*engine, R"({"key":42,"payload":"demo"})");
  EXPECT_EQ(inserted.status, http_ok);
  EXPECT_TRUE(inserted.body.contains(R"("target_slot_id")"));
  EXPECT_EQ(engine->records().page_count(), pages_before);
  EXPECT_TRUE(engine->table().find_by_index(sample_key).has_value());

  const auto found = search_response(*engine, "42");
  EXPECT_EQ(found.status, http_ok);
  EXPECT_TRUE(found.body.contains(R"("found":true)"));

  const auto missing = search_response(*engine, "999");
  EXPECT_EQ(missing.status, http_ok);
  EXPECT_TRUE(missing.body.contains(R"("found":false)"));
}

TEST(ApiHandlers, RejectsMalformedInsertBodies) {
  auto engine = make_engine(2);

  const auto trailing = insert_response(*engine, R"({"key":12abc,"payload":"x"})");
  EXPECT_EQ(trailing.status, http_bad_request);
  EXPECT_FALSE(engine->table().find_by_index(12).has_value());

  const auto nested_key = insert_response(*engine, R"({"payload":"\"key\":99"})");
  EXPECT_EQ(nested_key.status, http_bad_request);
  EXPECT_FALSE(engine->table().find_by_index(99).has_value());
}

TEST(ApiHandlers, RejectsDuplicatePrimaryKeysWithoutWriting) {
  auto engine = make_engine(2);
  ASSERT_EQ(insert_response(*engine, R"({"key":42,"payload":"demo"})").status, http_ok);
  const auto rows_before = engine->table().scan_all().size();

  const auto duplicate = insert_response(*engine, R"({"key":42,"payload":"otro"})");
  EXPECT_EQ(duplicate.status, http_conflict);
  EXPECT_EQ(engine->table().scan_all().size(), rows_before);
}

TEST(ApiHandlers, RejectsInvalidSearchKeys) {
  const auto engine = make_engine(2);
  EXPECT_EQ(search_response(*engine, "12abc").status, http_bad_request);
}

TEST(ApiHandlers, ServesExistingPagesAndRejectsMissingOnes) {
  const auto engine = make_engine(2);

  const auto page = page_response(*engine, "1");
  EXPECT_EQ(page.status, http_ok);
  EXPECT_TRUE(page.body.contains(R"("page_id":1)"));

  EXPECT_EQ(page_response(*engine, "999").status, http_not_found);
  EXPECT_EQ(page_response(*engine, "0").status, http_bad_request);
  EXPECT_EQ(page_response(*engine, "abc").status, http_bad_request);
}

TEST(ApiHandlers, ChangesDegreePreservingRows) {
  auto engine = make_engine(2);
  int degree = 2;

  const auto invalid = change_degree(engine, degree, R"({"degree":1})");
  EXPECT_EQ(invalid.status, http_bad_request);
  EXPECT_EQ(degree, 2);

  const auto malformed = change_degree(engine, degree, R"({"degree":4x})");
  EXPECT_EQ(malformed.status, http_bad_request);

  const auto changed = change_degree(engine, degree, R"({"degree":4})");
  EXPECT_EQ(changed.status, http_ok);
  EXPECT_EQ(degree, 4);
  EXPECT_EQ(engine->table().scan_all().size(), 7);
  EXPECT_TRUE(engine->table().find_by_index(10).has_value());
}

TEST(ApiHandlers, ResetsToTheSeedRows) {
  auto engine = make_engine(2);
  ASSERT_EQ(insert_response(*engine, R"({"key":42,"payload":"demo"})").status, http_ok);
  EXPECT_EQ(engine->table().scan_all().size(), 8);

  const auto reset = reset_response(engine, 2);
  EXPECT_EQ(reset.status, http_ok);
  EXPECT_EQ(engine->table().scan_all().size(), 7);
  EXPECT_FALSE(engine->table().find_by_index(sample_key).has_value());
}

}  // namespace
