#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;

using namespace sql;

constexpr std::size_t byte_1 = 1;
constexpr std::size_t byte_2 = 2;
constexpr std::size_t byte_3 = 3;
constexpr std::size_t byte_4 = 4;
constexpr std::size_t byte_5 = 5;
constexpr std::size_t byte_6 = 6;
constexpr std::size_t byte_7 = 7;
constexpr std::size_t byte_8 = 8;
constexpr std::size_t byte_9 = 9;
constexpr std::size_t byte_10 = 10;
constexpr std::size_t byte_11 = 11;
constexpr std::size_t byte_12 = 12;
constexpr std::size_t byte_13 = 13;
constexpr std::size_t byte_14 = 14;
constexpr std::size_t byte_15 = 15;
constexpr std::size_t byte_17 = 17;
constexpr std::size_t byte_18 = 18;
constexpr std::size_t byte_19 = 19;
constexpr std::size_t byte_20 = 20;
constexpr std::size_t byte_21 = 21;
constexpr std::size_t byte_22 = 22;
constexpr std::size_t byte_25 = 25;
constexpr std::size_t byte_26 = 26;
constexpr std::size_t byte_27 = 27;
constexpr std::size_t byte_32 = 32;
constexpr std::size_t byte_38 = 38;
constexpr std::size_t byte_40 = 40;
constexpr std::size_t byte_42 = 42;
constexpr std::size_t byte_43 = 43;
constexpr std::size_t byte_45 = 45;
constexpr std::size_t byte_48 = 48;
constexpr std::size_t byte_52 = 52;
constexpr std::size_t byte_53 = 53;
constexpr std::size_t byte_60 = 60;
constexpr std::size_t byte_62 = 62;
constexpr std::size_t byte_63 = 63;
constexpr std::size_t byte_67 = 67;
constexpr std::size_t byte_68 = 68;
constexpr std::size_t byte_71 = 71;
constexpr std::size_t byte_72 = 72;
constexpr std::size_t byte_75 = 75;
constexpr std::size_t byte_76 = 76;
constexpr std::size_t byte_78 = 78;
constexpr std::size_t byte_79 = 79;
constexpr std::size_t byte_80 = 80;
constexpr std::size_t byte_81 = 81;
constexpr std::size_t byte_82 = 82;
constexpr std::size_t byte_83 = 83;

constexpr std::size_t max_identifier_bytes = 63;
constexpr std::size_t too_long_identifier_bytes = 64;
constexpr std::size_t large_number_digits = 100;

constexpr std::array<std::pair<std::string_view, Keyword>, 32> keyword_table{
    {
        {"add", Keyword::add},         {"alter", Keyword::alter},     {"and", Keyword::and_},
        {"bigint", Keyword::bigint},   {"column", Keyword::column},   {"create", Keyword::create},
        {"decimal", Keyword::decimal}, {"delete", Keyword::delete_},  {"drop", Keyword::drop},
        {"exists", Keyword::exists},   {"from", Keyword::from},       {"if", Keyword::if_},
        {"index", Keyword::index},     {"insert", Keyword::insert},   {"int8", Keyword::int8},
        {"into", Keyword::into},       {"is", Keyword::is},           {"key", Keyword::key},
        {"not", Keyword::not_},        {"null", Keyword::null},       {"numeric", Keyword::numeric},
        {"or", Keyword::or_},          {"primary", Keyword::primary}, {"select", Keyword::select},
        {"set", Keyword::set},         {"table", Keyword::table},     {"text", Keyword::text},
        {"unique", Keyword::unique},   {"update", Keyword::update},   {"values", Keyword::values},
        {"varchar", Keyword::varchar}, {"where", Keyword::where},
    },
};

[[nodiscard]] constexpr auto upper_ascii(char value) noexcept -> char {
  if (value >= 'a' && value <= 'z') {
    return static_cast<char>(value - 'a' + 'A');
  }
  return value;
}

[[nodiscard]] auto tokenize_ok(std::string_view text) -> std::vector<Token> {
  const auto result = tokenize(text);
  EXPECT_TRUE(result.has_value()) << "unexpected lexer error";
  if (!result.has_value()) {
    return {};
  }
  return result.value();
}

[[nodiscard]] auto tokenize_error(std::string_view text) -> std::optional<SqlError> {
  const auto result = tokenize(text);
  if (result.has_value()) {
    return std::nullopt;
  }
  return result.error();
}

auto expect_token(const Token& token, TokenKind kind, std::string_view text, std::size_t position,
                  Keyword keyword = Keyword::none) -> void {
  EXPECT_EQ(token.kind, kind);
  EXPECT_EQ(token.keyword, keyword);
  EXPECT_EQ(token.text, std::string{text});
  EXPECT_EQ(token.position.offset, position);
}

auto expect_error(std::string_view text, std::size_t position) -> void {
  const auto error = tokenize_error(text);
  ASSERT_TRUE(error.has_value()) << "expected a lexer error";
  EXPECT_EQ(error.value().sqlstate, "42601");
  EXPECT_EQ(error.value().position.offset, position);
}

TEST(SqlLexer, EmptyInputHasOnlyEndOfInput) {
  const auto tokens = tokenize_ok("");
  ASSERT_EQ(tokens.size(), 1U);
  expect_token(tokens.at(0), TokenKind::end_of_input, "", byte_1);
}

TEST(SqlLexer, WhitespaceOnlyHasOnlyEndOfInput) {
  const auto tokens = tokenize_ok(" \t\n");
  ASSERT_EQ(tokens.size(), 1U);
  expect_token(tokens.at(0), TokenKind::end_of_input, "", byte_4);
}

TEST(SqlLexer, EndOfInputFollowsInputBytes) {
  const auto select = tokenize_ok("SELECT");
  ASSERT_EQ(select.size(), 2U);
  expect_token(select.at(0), TokenKind::keyword, "select", byte_1, Keyword::select);
  expect_token(select.at(1), TokenKind::end_of_input, "", byte_7);

  const auto trailing_space = tokenize_ok("SELECT ");
  ASSERT_EQ(trailing_space.size(), 2U);
  expect_token(trailing_space.at(1), TokenKind::end_of_input, "", byte_8);
}

TEST(SqlLexer, InvalidInputReturnsErrorWithoutTokens) {
  const auto result = tokenize("SELECT @");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().sqlstate, "42601");
  EXPECT_EQ(result.error().position.offset, byte_8);
}

TEST(SqlLexer, TokenOwnsItsText) {
  std::vector<Token> tokens;
  {
    std::string input{"SeLeCt"};
    tokens = tokenize_ok(input);
  }

  ASSERT_EQ(tokens.size(), 2U);
  expect_token(tokens.at(0), TokenKind::keyword, "select", byte_1, Keyword::select);
}

TEST(SqlLexer, NonKeywordsHaveNoKeyword) {
  const auto tokens = tokenize_ok("users 42 'x' ,");
  ASSERT_EQ(tokens.size(), 5U);
  expect_token(tokens.at(0), TokenKind::identifier, "users", byte_1);
  expect_token(tokens.at(1), TokenKind::integer_literal, "42", byte_7);
  expect_token(tokens.at(2), TokenKind::string_literal, "x", byte_10);
  expect_token(tokens.at(3), TokenKind::comma, ",", byte_14);
  expect_token(tokens.at(4), TokenKind::end_of_input, "", byte_15);
}

TEST(SqlLexer, FoldsIdentifiersAndRecognizesKeywords) {
  const auto tokens = tokenize_ok("Key_1 _ _1 select1");
  ASSERT_EQ(tokens.size(), 5U);
  expect_token(tokens.at(0), TokenKind::identifier, "key_1", byte_1);
  expect_token(tokens.at(1), TokenKind::identifier, "_", byte_7);
  expect_token(tokens.at(2), TokenKind::identifier, "_1", byte_9);
  expect_token(tokens.at(3), TokenKind::identifier, "select1", byte_12);
  expect_token(tokens.at(4), TokenKind::end_of_input, "", byte_19);
}

TEST(SqlLexer, RecognizesEveryKeywordInEveryCase) {
  for (const auto& [word, keyword] : keyword_table) {
    const auto lower = tokenize_ok(word);
    ASSERT_EQ(lower.size(), 2U);
    expect_token(lower.at(0), TokenKind::keyword, word, byte_1, keyword);
    expect_token(lower.at(1), TokenKind::end_of_input, "", word.size() + 1);

    std::string upper{word};
    for (char& value : upper) {
      value = upper_ascii(value);
    }
    const auto upper_tokens = tokenize_ok(upper);
    ASSERT_EQ(upper_tokens.size(), 2U);
    expect_token(upper_tokens.at(0), TokenKind::keyword, word, byte_1, keyword);

    std::string mixed{word};
    for (std::size_t index = 0; index < mixed.size(); ++index) {
      if (index % 2 == 0) {
        mixed.at(index) = upper_ascii(mixed.at(index));
      }
    }
    const auto mixed_tokens = tokenize_ok(mixed);
    ASSERT_EQ(mixed_tokens.size(), 2U);
    expect_token(mixed_tokens.at(0), TokenKind::keyword, word, byte_1, keyword);
  }
}

TEST(SqlLexer, AcceptsIdentifierAtLengthLimit) {
  const std::string identifier(max_identifier_bytes, 'a');
  const auto tokens = tokenize_ok(identifier);
  ASSERT_EQ(tokens.size(), 2U);
  expect_token(tokens.at(0), TokenKind::identifier, identifier, byte_1);
  expect_token(tokens.at(1), TokenKind::end_of_input, "", max_identifier_bytes + 1);
}

TEST(SqlLexer, RejectsIdentifierAboveLengthLimit) {
  const std::string identifier(too_long_identifier_bytes, 'a');
  expect_error(identifier, byte_1);
}

TEST(SqlLexer, RejectsNonAsciiIdentifier) {
  expect_error("café", byte_4);
}

TEST(SqlLexer, ScansIntegerAndDecimalLiterals) {
  const auto tokens = tokenize_ok("-12 0 007 -12.34 0.5 12.30");
  ASSERT_EQ(tokens.size(), 7U);
  std::size_t index = 0;
  expect_token(tokens.at(index++), TokenKind::integer_literal, "-12", byte_1);
  expect_token(tokens.at(index++), TokenKind::integer_literal, "0", byte_5);
  expect_token(tokens.at(index++), TokenKind::integer_literal, "007", byte_7);
  expect_token(tokens.at(index++), TokenKind::decimal_literal, "-12.34", byte_11);
  expect_token(tokens.at(index++), TokenKind::decimal_literal, "0.5", byte_18);
  expect_token(tokens.at(index++), TokenKind::decimal_literal, "12.30", byte_22);
  expect_token(tokens.at(index++), TokenKind::end_of_input, "", byte_27);
}

TEST(SqlLexer, DoesNotValidateNumericRange) {
  const std::string huge_integer(large_number_digits, '9');
  const std::string huge_decimal = "1." + std::string(large_number_digits, '0');
  const auto tokens = tokenize_ok(huge_integer + " " + huge_decimal);
  ASSERT_EQ(tokens.size(), 3U);
  EXPECT_EQ(tokens.at(0).kind, TokenKind::integer_literal);
  EXPECT_EQ(tokens.at(0).text, huge_integer);
  EXPECT_EQ(tokens.at(1).kind, TokenKind::decimal_literal);
  EXPECT_EQ(tokens.at(1).text, huge_decimal);
  EXPECT_EQ(tokens.at(2).kind, TokenKind::end_of_input);
}

TEST(SqlLexer, RejectsMalformedNumbers) {
  const std::array<std::pair<std::string_view, std::size_t>, 8> cases{
      {
          {"1.", byte_2},
          {".5", byte_1},
          {"+1", byte_1},
          {"- 1", byte_1},
          {"123abc", byte_4},
          {"1e3", byte_2},
          {"1_000", byte_2},
          {"1.2.3", byte_4},
      },
  };

  for (const auto& [input, expected_position] : cases) {
    expect_error(input, expected_position);
  }
}

TEST(SqlLexer, SplitsAdjacentSignedNumbers) {
  const auto tokens = tokenize_ok("1-2");
  ASSERT_EQ(tokens.size(), 3U);
  expect_token(tokens.at(0), TokenKind::integer_literal, "1", byte_1);
  expect_token(tokens.at(1), TokenKind::integer_literal, "-2", byte_2);
  expect_token(tokens.at(2), TokenKind::end_of_input, "", byte_4);
}

TEST(SqlLexer, DecodesStringQuotes) {
  const auto tokens = tokenize_ok("'Ada' '' 'it''s'");
  ASSERT_EQ(tokens.size(), 4U);
  expect_token(tokens.at(0), TokenKind::string_literal, "Ada", byte_1);
  expect_token(tokens.at(1), TokenKind::string_literal, "", byte_7);
  expect_token(tokens.at(2), TokenKind::string_literal, "it's", byte_10);
  expect_token(tokens.at(3), TokenKind::end_of_input, "", byte_17);
}

TEST(SqlLexer, StringsKeepUtf8NewlinesAndBackslashes) {
  const auto tokens = tokenize_ok("'café' 'a\nb' 'a\\b'");
  ASSERT_EQ(tokens.size(), 4U);
  expect_token(tokens.at(0), TokenKind::string_literal, "café", byte_1);
  expect_token(tokens.at(1), TokenKind::string_literal, "a\nb", byte_9);
  expect_token(tokens.at(2), TokenKind::string_literal, "a\\b", byte_15);
  expect_token(tokens.at(3), TokenKind::end_of_input, "", byte_20);
}

TEST(SqlLexer, RejectsMalformedStrings) {
  expect_error("'abc", byte_1);
  expect_error("\"texto\"", byte_1);

  const std::string nul_input{"'a\0b'", 5};
  const auto error = tokenize_error(std::string_view{nul_input});
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error.value().sqlstate, "42601");
  EXPECT_EQ(error.value().position.offset, byte_3);
}

TEST(SqlLexer, ScansPunctuationAndComparisonOperators) {
  const std::array<std::pair<std::string_view, TokenKind>, 10> cases{
      {
          {",", TokenKind::comma},
          {"(", TokenKind::left_paren},
          {")", TokenKind::right_paren},
          {"*", TokenKind::star},
          {";", TokenKind::semicolon},
          {"=", TokenKind::equal},
          {"<", TokenKind::less},
          {"<=", TokenKind::less_equal},
          {">", TokenKind::greater},
          {">=", TokenKind::greater_equal},
      },
  };

  for (const auto& [input, kind] : cases) {
    const auto tokens = tokenize_ok(input);
    ASSERT_EQ(tokens.size(), 2U);
    expect_token(tokens.at(0), kind, input, byte_1);
  }

  const auto not_equal = tokenize_ok("<>");
  ASSERT_EQ(not_equal.size(), 2U);
  expect_token(not_equal.at(0), TokenKind::not_equal, "<>", byte_1);

  const auto bang_not_equal = tokenize_ok("!=");
  ASSERT_EQ(bang_not_equal.size(), 2U);
  expect_token(bang_not_equal.at(0), TokenKind::not_equal, "!=", byte_1);
}

TEST(SqlLexer, RejectsUnsupportedCharactersAndComments) {
  const std::array<std::string_view, 6> unsupported{
      {
          "@",
          "\"",
          "+",
          "/",
          "%",
          ".",
      },
  };

  for (const auto& input : unsupported) {
    expect_error(input, byte_1);
  }

  expect_error("!", byte_1);
  expect_error("-- comentario", byte_1);
  expect_error("/* */", byte_1);
}

TEST(SqlLexer, SplitsDoubleEqualsIntoTwoTokens) {
  const auto tokens = tokenize_ok("==");
  ASSERT_EQ(tokens.size(), 3U);
  expect_token(tokens.at(0), TokenKind::equal, "=", byte_1);
  expect_token(tokens.at(1), TokenKind::equal, "=", byte_2);
  expect_token(tokens.at(2), TokenKind::end_of_input, "", byte_3);
}

TEST(SqlLexer, TokenizesSelectStatement) {
  const auto tokens = tokenize_ok(
      "SELECT uid, payload FROM users WHERE uid >= 10 AND payload IS NOT "
      "NULL;");
  ASSERT_EQ(tokens.size(), 17U);
  std::size_t index = 0;
  expect_token(tokens.at(index++), TokenKind::keyword, "select", byte_1, Keyword::select);
  expect_token(tokens.at(index++), TokenKind::identifier, "uid", byte_8);
  expect_token(tokens.at(index++), TokenKind::comma, ",", byte_11);
  expect_token(tokens.at(index++), TokenKind::identifier, "payload", byte_13);
  expect_token(tokens.at(index++), TokenKind::keyword, "from", byte_21, Keyword::from);
  expect_token(tokens.at(index++), TokenKind::identifier, "users", byte_26);
  expect_token(tokens.at(index++), TokenKind::keyword, "where", byte_32, Keyword::where);
  expect_token(tokens.at(index++), TokenKind::identifier, "uid", byte_38);
  expect_token(tokens.at(index++), TokenKind::greater_equal, ">=", byte_42);
  expect_token(tokens.at(index++), TokenKind::integer_literal, "10", byte_45);
  expect_token(tokens.at(index++), TokenKind::keyword, "and", byte_48, Keyword::and_);
  expect_token(tokens.at(index++), TokenKind::identifier, "payload", byte_52);
  expect_token(tokens.at(index++), TokenKind::keyword, "is", byte_60, Keyword::is);
  expect_token(tokens.at(index++), TokenKind::keyword, "not", byte_63, Keyword::not_);
  expect_token(tokens.at(index++), TokenKind::keyword, "null", byte_67, Keyword::null);
  expect_token(tokens.at(index++), TokenKind::semicolon, ";", byte_71);
  expect_token(tokens.at(index++), TokenKind::end_of_input, "", byte_72);
}

TEST(SqlLexer, TokenizesCreateTableStatement) {
  const auto tokens = tokenize_ok(
      "CREATE TABLE users (uid BIGINT PRIMARY KEY, payload VARCHAR, saldo "
      "DECIMAL(12,2));");
  ASSERT_EQ(tokens.size(), 22U);
  std::size_t index = 0;
  expect_token(tokens.at(index++), TokenKind::keyword, "create", byte_1, Keyword::create);
  expect_token(tokens.at(index++), TokenKind::keyword, "table", byte_8, Keyword::table);
  expect_token(tokens.at(index++), TokenKind::identifier, "users", byte_14);
  expect_token(tokens.at(index++), TokenKind::left_paren, "(", byte_20);
  expect_token(tokens.at(index++), TokenKind::identifier, "uid", byte_21);
  expect_token(tokens.at(index++), TokenKind::keyword, "bigint", byte_25, Keyword::bigint);
  expect_token(tokens.at(index++), TokenKind::keyword, "primary", byte_32, Keyword::primary);
  expect_token(tokens.at(index++), TokenKind::keyword, "key", byte_40, Keyword::key);
  expect_token(tokens.at(index++), TokenKind::comma, ",", byte_43);
  expect_token(tokens.at(index++), TokenKind::identifier, "payload", byte_45);
  expect_token(tokens.at(index++), TokenKind::keyword, "varchar", byte_53, Keyword::varchar);
  expect_token(tokens.at(index++), TokenKind::comma, ",", byte_60);
  expect_token(tokens.at(index++), TokenKind::identifier, "saldo", byte_62);
  expect_token(tokens.at(index++), TokenKind::keyword, "decimal", byte_68, Keyword::decimal);
  expect_token(tokens.at(index++), TokenKind::left_paren, "(", byte_75);
  expect_token(tokens.at(index++), TokenKind::integer_literal, "12", byte_76);
  expect_token(tokens.at(index++), TokenKind::comma, ",", byte_78);
  expect_token(tokens.at(index++), TokenKind::integer_literal, "2", byte_79);
  expect_token(tokens.at(index++), TokenKind::right_paren, ")", byte_80);
  expect_token(tokens.at(index++), TokenKind::right_paren, ")", byte_81);
  expect_token(tokens.at(index++), TokenKind::semicolon, ";", byte_82);
  expect_token(tokens.at(index++), TokenKind::end_of_input, "", byte_83);
}

TEST(SqlLexer, TokenizesSelectStar) {
  const auto tokens = tokenize_ok("SELECT * FROM users;");
  ASSERT_EQ(tokens.size(), 6U);
  std::size_t index = 0;
  expect_token(tokens.at(index++), TokenKind::keyword, "select", byte_1, Keyword::select);
  expect_token(tokens.at(index++), TokenKind::star, "*", byte_8);
  expect_token(tokens.at(index++), TokenKind::keyword, "from", byte_10, Keyword::from);
  expect_token(tokens.at(index++), TokenKind::identifier, "users", byte_15);
  expect_token(tokens.at(index++), TokenKind::semicolon, ";", byte_20);
  expect_token(tokens.at(index++), TokenKind::end_of_input, "", byte_21);
}

TEST(SqlLexer, TokenizesMultipleStatementsLexically) {
  const auto tokens = tokenize_ok("SELECT 1; SELECT 2;");
  ASSERT_EQ(tokens.size(), 7U);
  std::size_t index = 0;
  expect_token(tokens.at(index++), TokenKind::keyword, "select", byte_1, Keyword::select);
  expect_token(tokens.at(index++), TokenKind::integer_literal, "1", byte_8);
  expect_token(tokens.at(index++), TokenKind::semicolon, ";", byte_9);
  expect_token(tokens.at(index++), TokenKind::keyword, "select", byte_11, Keyword::select);
  expect_token(tokens.at(index++), TokenKind::integer_literal, "2", byte_18);
  expect_token(tokens.at(index++), TokenKind::semicolon, ";", byte_19);
  expect_token(tokens.at(index++), TokenKind::end_of_input, "", byte_20);
}

TEST(SqlLexer, ReportsByteOffsetsNotCodePoints) {
  expect_error("'é' @", byte_6);
}

}  // namespace
