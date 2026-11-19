#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

namespace sql = kravidb::sql;
namespace ll1 = sql::ll1;

/// \brief 1-based offset (as stored in SourcePosition) of a byte offset from find/rfind.
auto one_based(std::size_t offset) -> std::size_t {
  if (offset == std::string_view::npos) {
    throw std::invalid_argument{"needle not found in SQL text"};
  }
  return offset + 1;
}

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
    "SELECT * FROM t WHERE active = TRUE AND FALSE OR (TRUE IS NOT NULL)",
    "SELECT * FROM t WHERE true = false",
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
    "SELECT TRUE FROM t",
    "SELECT FALSE FROM t",
    "SELECT * FROM t WHERE a IS TRUE",
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
  const auto translated = sql::parse_select_ll1(tokens);
  ASSERT_EQ(actual.has_value(), reference.has_value());
  ASSERT_EQ(translated.has_value(), reference.has_value());
  if (!reference.has_value()) {
    EXPECT_EQ(validation.error().sqlstate, reference.error().sqlstate);
    EXPECT_EQ(validation.error().position, reference.error().position);
    EXPECT_EQ(actual.error().sqlstate, reference.error().sqlstate);
    EXPECT_EQ(actual.error().position, reference.error().position);
    // One engine: the parser, the SDT and the validator report the same error.
    EXPECT_EQ(actual.error(), translated.error());
    EXPECT_EQ(validation.error(), translated.error());
  } else {
    EXPECT_EQ(fingerprint(std::get<sql::SelectStmt>(*actual)),
              fingerprint(std::get<sql::SelectStmt>(*reference)));
    EXPECT_EQ(fingerprint(*translated), fingerprint(std::get<sql::SelectStmt>(*reference)));
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
  const std::array replacements{"TRUE", "FALSE", "x",   "SELECT", "FROM", "WHERE", "AND",
                                "OR",   "IS",    "NOT", "NULL",   "1",    "'a'",   "=",
                                ",",    "(",     ")",   "*",      ";"};
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

TEST(SqlSelectLl1, SdtAstMatchesRecursiveAstForGeneratedExpressions) {
  const std::array atoms{"a",
                         "1",
                         "-0.5",
                         "'it''s'",
                         "NULL",
                         "TRUE",
                         "FALSE",
                         "(a = 1 OR b = 2)",
                         "9223372036854775808"};
  const std::array suffixes{"",      " IS NULL", " IS NOT NULL", " = (1)", " <> b",
                            " != b", " < 1",     " <= 1",        " > 1",   " >= 1"};
  const std::array shapes{
      "SELECT a, b FROM t WHERE {}{} AND c = 3 AND d = 4 OR e = 5 OR f = 6;",
      "SELECT * FROM t WHERE x = 0 OR ({}{}) AND (y = 1 OR z = 2)",
  };
  for (const auto* shape : shapes) {
    for (const auto* atom : atoms) {
      for (const auto* suffix : suffixes) {
        const std::string text = std::vformat(shape, std::make_format_args(atom, suffix));
        SCOPED_TRACE(text);
        const auto tokens = tokens_of(text);
        const auto reference = sql::detail::parse_tokens_recursive_reference(tokens);
        const auto actual = sql::parse_tokens(tokens);
        const auto translated = sql::parse_select_ll1(tokens);
        ASSERT_TRUE(reference.has_value());
        ASSERT_TRUE(actual.has_value());
        ASSERT_TRUE(translated.has_value());
        const auto expected = fingerprint(std::get<sql::SelectStmt>(*reference));
        EXPECT_EQ(fingerprint(std::get<sql::SelectStmt>(*actual)), expected);
        EXPECT_EQ(fingerprint(*translated), expected);
      }
    }
  }
}

TEST(SqlSelectLl1, SdtHasOneActionPerProduction) {
  // parse_select_ll1 selects actions by production number; a grammar change
  // must update the action table (and GRAMMAR.md) together.
  EXPECT_EQ(ll1::select_grammar().size(), 38U);
  EXPECT_TRUE(sql::parse_select_ll1(tokens_of("SELECT * FROM t")).has_value());
}

TEST(SqlSelectLl1, SdtBuildsTargetsInSourceOrder) {
  const auto star = sql::parse_select_ll1(tokens_of("SELECT * FROM users;"));
  ASSERT_TRUE(star.has_value());
  EXPECT_TRUE(star->targets.empty());
  EXPECT_EQ(star->table.name, "users");
  EXPECT_EQ(star->where, nullptr);

  const std::string_view text = "SELECT c, a, b FROM t";
  const auto list = sql::parse_select_ll1(tokens_of(text));
  ASSERT_TRUE(list.has_value());
  ASSERT_EQ(list->targets.size(), 3U);
  EXPECT_EQ(list->targets.at(0).name, "c");
  EXPECT_EQ(list->targets.at(1).name, "a");
  EXPECT_EQ(list->targets.at(2).name, "b");
  EXPECT_EQ(list->targets.at(0).position.offset, one_based(text.find('c')));
  EXPECT_EQ(list->table.position.offset, one_based(text.rfind('t')));
}

TEST(SqlSelectLl1, SdtFoldsBooleanChainsLeftAndKeepsPrecedence) {
  // a OR b AND c OR d  ==  (a OR (b AND c)) OR d
  const std::string_view text = "SELECT * FROM t WHERE a OR b AND c OR d";
  const auto select = sql::parse_select_ll1(tokens_of(text));
  ASSERT_TRUE(select.has_value());
  const auto& outer = std::get<sql::BinaryExpr>(select->where->node);
  EXPECT_EQ(outer.op, sql::BinaryOp::or_);
  EXPECT_EQ(outer.position.offset, one_based(text.rfind("OR")));
  EXPECT_EQ(std::get<sql::ColumnRef>(outer.right->node).name.name, "d");
  const auto& inner = std::get<sql::BinaryExpr>(outer.left->node);
  EXPECT_EQ(inner.op, sql::BinaryOp::or_);
  EXPECT_EQ(inner.position.offset, one_based(text.find("OR")));
  EXPECT_EQ(std::get<sql::ColumnRef>(inner.left->node).name.name, "a");
  const auto& conjunction = std::get<sql::BinaryExpr>(inner.right->node);
  EXPECT_EQ(conjunction.op, sql::BinaryOp::and_);
  EXPECT_EQ(std::get<sql::ColumnRef>(conjunction.left->node).name.name, "b");
  EXPECT_EQ(std::get<sql::ColumnRef>(conjunction.right->node).name.name, "c");
}

TEST(SqlSelectLl1, SdtBuildsPredicatesLiteralsAndDropsParentheses) {
  const std::string_view text =
      "SELECT * FROM t WHERE ((a IS NOT NULL)) AND b >= 9223372036854775808 AND c = TRUE";
  const auto select = sql::parse_select_ll1(tokens_of(text));
  ASSERT_TRUE(select.has_value());
  const auto& second_and = std::get<sql::BinaryExpr>(select->where->node);
  const auto& first_and = std::get<sql::BinaryExpr>(second_and.left->node);

  const auto& null_check = std::get<sql::BinaryExpr>(first_and.left->node);
  EXPECT_EQ(null_check.op, sql::BinaryOp::is_not_null);
  EXPECT_EQ(null_check.position.offset, one_based(text.find("IS")));
  EXPECT_EQ(null_check.right, nullptr);
  EXPECT_EQ(std::get<sql::ColumnRef>(null_check.left->node).name.name, "a");

  const auto& big = std::get<sql::BinaryExpr>(first_and.right->node);
  EXPECT_EQ(big.op, sql::BinaryOp::ge);
  EXPECT_EQ(big.position.offset, one_based(text.find(">=")));
  const auto& big_value = std::get<sql::Literal>(big.right->node);
  EXPECT_EQ(std::get<sql::DecimalLiteral>(big_value.value).text, "9223372036854775808");

  const auto& boolean = std::get<sql::BinaryExpr>(second_and.right->node);
  EXPECT_TRUE(std::get<bool>(std::get<sql::Literal>(boolean.right->node).value));
}

TEST(SqlSelectLl1, TraceHidesSdtMarkersAndMatchesValidator) {
  const auto view = ll1::select_grammar();
  const std::vector<ll1::Production> grammar{view.begin(), view.end()};
  const auto tokens = tokens_of("SELECT a, b FROM t WHERE (a = 1 OR b IS NOT NULL) AND c <> 'x';");
  std::vector<ll1::TraceStep> parsed;
  std::vector<ll1::TraceStep> validated;
  ASSERT_TRUE(sql::parse_select_ll1(tokens, [&](const auto& step) { parsed.push_back(step); }));
  ASSERT_TRUE(sql::validate_select(tokens, [&](const auto& step) { validated.push_back(step); }));
  ASSERT_EQ(parsed.size(), validated.size());
  for (std::size_t index = 0; index < parsed.size(); ++index) {
    const auto& step = parsed.at(index);
    EXPECT_EQ(step.action, validated.at(index).action);
    EXPECT_EQ(step.token_index, validated.at(index).token_index);
    EXPECT_EQ(step.production, validated.at(index).production);
    EXPECT_EQ(step.stack, validated.at(index).stack);
    EXPECT_EQ(step.stack_size, step.stack.size()) << "step " << index;
    if (index + 1 == parsed.size()) {
      continue;
    }
    // With markers hidden the visible stack evolves exactly as in a pure LL(1)
    // recognizer: an expansion replaces the top by the right-hand side and a
    // match pops one terminal.
    const auto next = parsed.at(index + 1).stack.size();
    if (step.action == ll1::TraceAction::expand) {
      EXPECT_EQ(next + 1, step.stack.size() + grammar.at(*step.production).right.size())
          << "step " << index;
    } else if (step.action == ll1::TraceAction::match) {
      EXPECT_EQ(next + 1, step.stack.size()) << "step " << index;
    }
  }
  EXPECT_EQ(parsed.back().action, ll1::TraceAction::accept);
}

TEST(SqlSelectLl1, SdtErrorsKeepSqlstateMessageAndPosition) {
  const std::string_view text = "SELECT a FROM t WHERE a = 1 LIMIT 5";
  const auto tokens = tokens_of(text);
  std::vector<ll1::TraceStep> steps;
  const auto result =
      sql::parse_select_ll1(tokens, [&](const auto& step) { steps.push_back(step); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().sqlstate, sql::sqlstate::feature_not_supported);
  EXPECT_EQ(result.error().message, "LIMIT is not supported");
  EXPECT_EQ(result.error().position.offset, one_based(text.find("LIMIT")));
  ASSERT_FALSE(steps.empty());
  EXPECT_EQ(steps.back().action, ll1::TraceAction::error);
  EXPECT_EQ(steps.back().stack_size, steps.back().stack.size());

  const auto syntax = sql::parse_select_ll1(tokens_of("SELECT a FROM t WHERE a ="));
  ASSERT_FALSE(syntax.has_value());
  EXPECT_EQ(syntax.error().sqlstate, sql::sqlstate::syntax_error);
  EXPECT_TRUE(
      syntax.error().message.starts_with("syntax error at line 1, column 26 at end of input"))
      << syntax.error().message;
}

TEST(SqlSelectLl1, SdtBuildsLongBooleanChainsWithoutRecursion) {
  constexpr std::size_t conjunction_count = 10000;
  std::string text = "SELECT * FROM t WHERE a = 1";
  for (std::size_t count = 0; count < conjunction_count; ++count) {
    text += " AND a = 1";
  }
  auto parsed = sql::parse_tokens(tokens_of(text));
  ASSERT_TRUE(parsed.has_value());
  const auto& select = std::get<sql::SelectStmt>(*parsed);
  // Left fold: the spine of AND nodes runs down the left children.
  std::size_t spine = 0;
  const sql::Expr* node = select.where.get();
  while (const auto* binary = std::get_if<sql::BinaryExpr>(&node->node)) {
    if (binary->op != sql::BinaryOp::and_) {
      break;
    }
    EXPECT_EQ(std::get<sql::BinaryExpr>(binary->right->node).op, sql::BinaryOp::eq);
    node = binary->left.get();
    ++spine;
  }
  EXPECT_EQ(spine, conjunction_count);
  parsed = sql::parse_tokens(tokens_of("SELECT * FROM t"));  // destroys the deep tree
  EXPECT_TRUE(parsed.has_value());
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
