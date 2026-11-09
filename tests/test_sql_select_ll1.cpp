#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;
namespace ll1 = sql::ll1;

auto tokens_of(std::string_view text) -> std::vector<sql::Token> {
  auto tokens = sql::tokenize(text);
  if (!tokens.has_value()) {
    throw std::invalid_argument{tokens.error().message};
  }
  return std::move(tokens.value());
}

TEST(SqlSelectLl1, TableIsConflictFreeAndDistinguishesKeywords) {
  const auto view = ll1::select_grammar();
  const std::vector<ll1::Production> grammar{view.begin(), view.end()};
  const auto table = ll1::build_table(grammar);
  ASSERT_TRUE(table.has_value());
  EXPECT_FALSE(table->empty());
  using N = ll1::Nonterminal;
  using T = ll1::Terminal;
  const auto select = table->find(
      {N::statement, T{.kind = sql::TokenKind::keyword, .keyword = sql::Keyword::select}});
  ASSERT_NE(select, table->end());
  EXPECT_EQ(grammar.at(select->second).left, N::statement);
  EXPECT_EQ(table->find(
                {N::statement, T{.kind = sql::TokenKind::keyword, .keyword = sql::Keyword::from}}),
            table->end());

  const auto epsilon = table->find(
      {N::targets_tail, T{.kind = sql::TokenKind::keyword, .keyword = sql::Keyword::from}});
  ASSERT_NE(epsilon, table->end());
  EXPECT_TRUE(grammar.at(epsilon->second).right.empty());
}

TEST(SqlSelectLl1, ReportsFirstFirstAndFirstFollowConflicts) {
  const auto grammar = ll1::select_grammar();
  std::vector<ll1::Production> duplicate{grammar.begin(), grammar.end()};
  duplicate.push_back(grammar.front());
  const auto first_first = ll1::build_table(duplicate);
  ASSERT_FALSE(first_first.has_value());
  EXPECT_EQ(first_first.error().existing_production, 0U);
  EXPECT_EQ(first_first.error().incoming_production, duplicate.size() - 1);

  std::vector<ll1::Production> conflicting{grammar.begin(), grammar.end()};
  conflicting.push_back(
      {.left = ll1::Nonterminal::targets_tail,
       .right = {ll1::Terminal{.kind = sql::TokenKind::keyword, .keyword = sql::Keyword::from}}});
  const auto first_follow = ll1::build_table(conflicting);
  ASSERT_FALSE(first_follow.has_value());
  EXPECT_EQ(first_follow.error().cell.first, ll1::Nonterminal::targets_tail);
  EXPECT_EQ(first_follow.error().cell.second.keyword, sql::Keyword::from);
}

TEST(SqlSelectLl1, TraceShowsExpansionsMatchesEpsilonAndAcceptance) {
  const auto tokens = tokens_of("SELECT * FROM users;");
  const auto view = ll1::select_grammar();
  const std::vector<ll1::Production> grammar{view.begin(), view.end()};
  std::vector<ll1::TraceStep> steps;
  ASSERT_TRUE(sql::validate_select(tokens, [&](const auto& step) { steps.push_back(step); }));
  ASSERT_FALSE(steps.empty());
  EXPECT_EQ(steps.front().action, ll1::TraceAction::expand);
  EXPECT_EQ(steps.front().token_index, 0U);
  EXPECT_EQ(steps.front().stack_size, 1U);
  ASSERT_EQ(steps.front().stack.size(), 1U);
  EXPECT_EQ(std::get<ll1::Nonterminal>(steps.front().stack.back()), ll1::Nonterminal::statement);
  ASSERT_TRUE(steps.front().cell.has_value());
  EXPECT_EQ(steps.front().cell->first, ll1::Nonterminal::statement);
  EXPECT_EQ(steps.front().cell->second.kind, sql::TokenKind::keyword);
  EXPECT_EQ(steps.front().cell->second.keyword, sql::Keyword::select);
  EXPECT_EQ(steps.back().action, ll1::TraceAction::accept);
  EXPECT_EQ(steps.back().stack_size, 0U);
  EXPECT_TRUE(steps.back().stack.empty());
  EXPECT_FALSE(steps.back().cell.has_value());
  EXPECT_EQ(steps.back().token_index, tokens.size());
  std::size_t matches = 0;
  bool epsilon = false;
  for (const auto& step : steps) {
    if (step.action == ll1::TraceAction::match) {
      EXPECT_EQ(step.token_index, matches++);
      EXPECT_FALSE(step.production.has_value());
    }
    if (step.production.has_value()) {
      epsilon = epsilon || grammar.at(*step.production).right.empty();
    }
  }
  EXPECT_EQ(matches, tokens.size());
  EXPECT_TRUE(epsilon);
}

TEST(SqlSelectLl1, RejectionStopsBeforeMatchingInvalidTokenAndPropagatesToParser) {
  const auto tokens = tokens_of("SELECT a FROM t WHERE a = ;");
  std::vector<ll1::TraceStep> steps;
  const auto validation =
      sql::validate_select(tokens, [&](const auto& step) { steps.push_back(step); });
  ASSERT_FALSE(validation.has_value());
  ASSERT_FALSE(steps.empty());
  EXPECT_EQ(steps.back().action, ll1::TraceAction::error);
  EXPECT_EQ(tokens.at(steps.back().token_index).kind, sql::TokenKind::semicolon);
  EXPECT_FALSE(std::ranges::any_of(
      steps, [](const auto& step) { return step.action == ll1::TraceAction::accept; }));
  const auto parsed = sql::parse_tokens(tokens);
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error(), validation.error());
}

TEST(SqlSelectLl1, RejectsMalformedTokenStreamsAndNonSelectInput) {
  EXPECT_FALSE(sql::validate_select(std::span<const sql::Token>{}));
  auto missing_eof = tokens_of("SELECT * FROM t");
  missing_eof.pop_back();
  EXPECT_FALSE(sql::validate_select(missing_eof));
  auto embedded_eof = tokens_of("SELECT * FROM t");
  embedded_eof.push_back(embedded_eof.back());
  EXPECT_FALSE(sql::validate_select(embedded_eof));
  EXPECT_FALSE(sql::validate_select(tokens_of("INSERT INTO t VALUES (1)")));
  EXPECT_FALSE(sql::validate_select(tokens_of(";")));
  EXPECT_FALSE(sql::validate_select(tokens_of("")));
}

/// \brief Explicit acceptance/rejection corpus; semantic failures remain syntax-valid.
const std::array accepted{
    "SELECT * FROM t",
    "SELECT id, name FROM users;",
    "select ID from Users where Age >= 18;",
    "SELECT distinct, all FROM t",
    "SELECT join, order, count FROM limit",
    "SELECT * FROM t WHERE a = 1 OR b = 2 AND c = 3",
    "SELECT * FROM t WHERE (a = 1 OR b = 2) AND c = 3",
    "SELECT * FROM t WHERE (a) = (1)",
    "SELECT * FROM t WHERE (a = 1) IS NULL",
    "SELECT * FROM t WHERE a IS NULL OR b IS NOT NULL",
    "SELECT * FROM t WHERE 1 < a AND a = b OR 1 = 1",
    "SELECT * FROM t WHERE a = -0.5 AND b = 'it''s'",
    "SELECT * FROM t WHERE a = 9223372036854775808",
    "SELECT * FROM t WHERE a = NULL",
    "SELECT * FROM t WHERE 'yes'",
    "SELECT * FROM t WHERE qty AND id = 1",
    "SELECT * FROM missing WHERE unknown",
    "SELECT * FROM t WHERE ((a = 1));  \n",
};

const std::array rejected{
    "SELECT",
    "SELECT a",
    "SELECT FROM t",
    "SELECT a FROM",
    "SELECT a, FROM t",
    "SELECT a FROM where",
    "SELECT a FROM t WHERE",
    "SELECT a FROM t WHERE a =",
    "SELECT a FROM t WHERE a = 1 AND",
    "SELECT a FROM t WHERE a = 1 OR",
    "SELECT a FROM t WHERE a = 1 = 2",
    "SELECT a FROM t WHERE a IS 5",
    "SELECT a FROM t WHERE a IS NOT 5",
    "SELECT a FROM t WHERE (a = 1",
    "SELECT a FROM t WHERE a = 1)",
    "SELECT a FROM t WHERE ()",
    "SELECT a FROM t WHERE a = from",
    "SELECT * FROM a; SELECT * FROM b",
    "SELECT * FROM a;;",
    "SELECT * FROM a SELECT * FROM b",
    "SELECT * FROM a JOIN b",
    "SELECT * FROM a INNER JOIN b",
    "SELECT * FROM a LEFT JOIN b",
    "SELECT * FROM t ORDER BY a",
    "SELECT * FROM t WHERE a = 1 LIMIT 5",
    "SELECT * FROM t GROUP BY a",
    "SELECT * FROM t HAVING a = 1",
    "SELECT * FROM t OFFSET 1",
    "SELECT * FROM t FETCH FIRST 1",
    "SELECT * FROM t UNION SELECT * FROM t",
    "SELECT * FROM t INTERSECT SELECT * FROM t",
    "SELECT * FROM t EXCEPT SELECT * FROM t",
    "SELECT count(*) FROM t",
    "SELECT a, sum(b) FROM t",
    "SELECT DISTINCT a FROM t",
    "SELECT ALL * FROM t",
    "SELECT a AS b FROM t",
    "SELECT * AS a FROM t",
    "SELECT * FROM a, b",
    "SELECT * FROM a AS b",
    "SELECT * FROM (SELECT * FROM t)",
    "SELECT * FROM t WHERE (SELECT a FROM u) = 1",
    "SELECT * FROM t WHERE NOT a = 1",
    "SELECT * FROM t WHERE a NOT IN (1)",
    "SELECT * FROM t WHERE a IN (1, 2)",
    "SELECT * FROM t WHERE a LIKE 'a'",
    "SELECT * FROM t WHERE a ILIKE 'a'",
    "SELECT * FROM t WHERE a BETWEEN 1 AND 2",
    "SELECT * FROM t WHERE a SIMILAR TO 'x'",
    "SELECT * FROM t WHERE max(a) > 1",
    "SELECT a, * FROM t",
    "SELECT *, a FROM t",
    "SELECT 1 FROM t",
    "SELECT 1.5 FROM t",
    "SELECT 'a' FROM t",
    "SELECT NULL FROM t",
    "SELECT * FROM t WHERE (a = 1 LIMIT 5)",
};

TEST(SqlSelectLl1, AcceptsAndRejectsTheSupportedLanguage) {
  for (const auto* text : accepted) {
    const auto result = sql::validate_select(tokens_of(text));
    EXPECT_TRUE(result.has_value()) << text;
  }
  for (const auto* text : rejected) {
    EXPECT_FALSE(sql::validate_select(tokens_of(text)).has_value()) << text;
  }
}

auto fingerprint(const sql::SelectStmt& select) -> std::string;

auto compare_with_reference(std::string_view text) -> void {
  SCOPED_TRACE(text);
  const auto tokens = tokens_of(text);
  const auto reference = sql::detail::parse_tokens_recursive_reference(tokens);
  const auto validation = sql::validate_select(tokens);
  ASSERT_EQ(validation.has_value(), reference.has_value());
  const auto actual = sql::parse_tokens(tokens);
  ASSERT_EQ(actual.has_value(), reference.has_value());
  if (!reference.has_value()) {
    EXPECT_EQ(validation.error().sqlstate, reference.error().sqlstate);
    EXPECT_EQ(validation.error().position, reference.error().position);
    EXPECT_EQ(actual.error().sqlstate, reference.error().sqlstate);
    EXPECT_EQ(actual.error().position, reference.error().position);
  } else {
    EXPECT_EQ(fingerprint(std::get<sql::SelectStmt>(*actual)),
              fingerprint(std::get<sql::SelectStmt>(*reference)));
  }
}

TEST(SqlSelectLl1, AgreesWithIndependentRecursiveParserOnCorpusAndDepthBoundaries) {
  for (const auto* text : accepted) {
    compare_with_reference(text);
  }
  for (const auto* text : rejected) {
    compare_with_reference(text);
  }
  for (const std::size_t depth : {0U, 64U, 255U, 256U, 257U, 1000U}) {
    compare_with_reference(std::format("SELECT * FROM t WHERE {}a = 1{}", std::string(depth, '('),
                                       std::string(depth, ')')));
  }
}

TEST(SqlSelectLl1, AgreesWithReferenceOnSingleTokenMutations) {
  const std::array seeds{
      "SELECT a, b FROM t WHERE (a = 1 OR b IS NOT NULL) AND c >= 2;",
      "SELECT * FROM t WHERE a = (b IS NULL)",
  };
  const std::array replacements{"x", "SELECT", "FROM", "WHERE", "AND", "OR", "IS", "NOT", "NULL",
                                "1", "'a'",    "=",    ",",     "(",   ")",  "*",  ";"};
  for (const auto* seed : seeds) {
    const auto original = tokens_of(seed);
    // Keep SELECT and the final EOF: this test compares SELECT recognizers,
    // not statement dispatch or the legacy parser's embedded-EOF behavior.
    for (std::size_t offset = 1; offset + 1 < original.size(); ++offset) {
      auto removed = original;
      removed.erase(std::next(removed.begin(), static_cast<std::ptrdiff_t>(offset)));
      EXPECT_EQ(sql::validate_select(removed).has_value(),
                sql::detail::parse_tokens_recursive_reference(removed).has_value())
          << seed << " deletion at " << offset;
      for (const auto* replacement : replacements) {
        auto mutated = original;
        auto token = tokens_of(replacement).front();
        token.position = mutated.at(offset).position;
        mutated.at(offset) = std::move(token);
        EXPECT_EQ(sql::validate_select(mutated).has_value(),
                  sql::detail::parse_tokens_recursive_reference(mutated).has_value())
            << seed << " replacement at " << offset << " with " << replacement;
      }
    }
  }
}

TEST(SqlSelectLl1, ValidatesLongBooleanChainsWithAnExplicitStack) {
  constexpr std::size_t conjunction_count = 10000;
  std::string text = "SELECT * FROM t WHERE a = 1";
  for (std::size_t count = 0; count < conjunction_count; ++count) {
    text += " AND a = 1";
  }
  EXPECT_TRUE(sql::validate_select(tokens_of(text)).has_value());
}

/// \brief A flat fingerprint checks AST structure, associativity, values and positions.
auto fingerprint(const sql::SelectStmt& select) -> std::string {
  std::string result = std::format("{}@{}", select.table.name, select.table.position.offset);
  for (const auto& target : select.targets) {
    result += std::format(" {}@{}", target.name, target.position.offset);
  }
  std::vector<const sql::Expr*> pending{select.where.get()};
  while (!pending.empty()) {
    const auto* expr = pending.back();
    pending.pop_back();
    if (expr == nullptr) {
      result += " #";
      continue;
    }
    std::visit(
        [&](const auto& node) {
          using Node = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<Node, sql::ColumnRef>) {
            result += std::format(" col:{}@{}", node.name.name, node.name.position.offset);
          } else if constexpr (std::is_same_v<Node, sql::Literal>) {
            result += std::format(" lit:{}@{}:", node.value.index(), node.position.offset);
            std::visit(
                [&](const auto& value) {
                  using Value = std::decay_t<decltype(value)>;
                  if constexpr (std::is_same_v<Value, sql::DecimalLiteral>) {
                    result += value.text;
                  } else if constexpr (!std::is_same_v<Value, sql::NullLiteral>) {
                    result += std::format("{}", value);
                  }
                },
                node.value);
          } else {
            result += std::format(" op:{}@{}", static_cast<int>(node.op), node.position.offset);
            pending.push_back(node.right.get());
            pending.push_back(node.left.get());
          }
        },
        expr->node);
  }
  return result;
}

TEST(SqlSelectLl1, IterativeAstMatchesRecursiveAstForGeneratedExpressions) {
  const std::array atoms{
      "a", "1", "-0.5", "'it''s'", "NULL", "(a = 1 OR b = 2)", "9223372036854775808"};
  const std::array suffixes{"",      " IS NULL", " IS NOT NULL", " = (1)", " <> b",
                            " != b", " < 1",     " <= 1",        " > 1",   " >= 1"};
  for (const auto* atom : atoms) {
    for (const auto* suffix : suffixes) {
      const std::string text = std::format(
          "SELECT a, b FROM t WHERE {}{} AND c = 3 AND d = 4 OR e = 5 OR f = 6;", atom, suffix);
      SCOPED_TRACE(text);
      const auto tokens = tokens_of(text);
      const auto reference = sql::detail::parse_tokens_recursive_reference(tokens);
      const auto actual = sql::parse_tokens(tokens);
      ASSERT_TRUE(reference.has_value());
      ASSERT_TRUE(actual.has_value());
      EXPECT_EQ(fingerprint(std::get<sql::SelectStmt>(*actual)),
                fingerprint(std::get<sql::SelectStmt>(*reference)));
    }
  }
}

TEST(SqlSelectLl1, DmlAndDdlStillUseTheirOriginalParser) {
  const std::array statements{
      "INSERT INTO t VALUES (1)",    "UPDATE t SET a = 1 WHERE (b = 2 OR c IS NULL)",
      "DELETE FROM t WHERE (b = 2)", "CREATE TABLE t (id BIGINT PRIMARY KEY)",
      "DROP TABLE IF EXISTS t",      "ALTER TABLE t ADD COLUMN a TEXT",
      "INSERT INTO t VALUES (1,)",   "UPDATE t SET WHERE a = 1",
      "CREATE TABLE t ()",
  };
  for (const auto* text : statements) {
    SCOPED_TRACE(text);
    const auto tokens = tokens_of(text);
    const auto reference = sql::detail::parse_tokens_recursive_reference(tokens);
    const auto actual = sql::parse_tokens(tokens);
    ASSERT_EQ(actual.has_value(), reference.has_value());
    if (actual.has_value()) {
      EXPECT_EQ(actual->index(), reference->index());
    } else {
      EXPECT_EQ(actual.error(), reference.error());
    }
  }
}

}  // namespace
