/// \file
/// \brief SQL parser: SELECT through the LL(1) engine with SDT, the rest by recursive descent.
///
/// \details SELECT statements are parsed and translated to `SelectStmt` in a
/// single pass by `parse_select_ll1` (see `sql.select_ll1`); DML and DDL use
/// the recursive-descent parser. Grammar accepted for SELECT:
///
/// ```text
/// statement   := select_stmt [ ';' ] end_of_input
/// select_stmt := SELECT target_list FROM identifier [ WHERE expr ]
/// target_list := '*' | identifier { ',' identifier }
/// expr        := and_expr { OR and_expr }
/// and_expr    := predicate { AND predicate }
/// predicate   := primary [ comparison primary | IS [ NOT ] NULL ]
/// primary     := '(' expr ')' | identifier | literal
/// comparison  := '=' | '<>' | '!=' | '<' | '<=' | '>' | '>='
/// literal     := integer | decimal | string | NULL | TRUE | FALSE
/// ```
///
/// Errors use SQLSTATE `42601` (syntax error) or `0A000` (a recognized SQL
/// feature outside the supported subset, such as `JOIN` or `LIMIT`), and
/// always carry the position of the offending token.

export module sql.parser;

import std;
export import sql.ast;
export import sql.error;
export import sql.lexer;
export import sql.select_ll1;

export namespace kravidb::sql {

/// \brief Parses exactly one statement from a token sequence.
///
/// \details `tokens` must end with `TokenKind::end_of_input`, as produced by
/// `tokenize`. A trailing `;` is allowed; anything after it is rejected.
[[nodiscard]] auto parse_tokens(std::span<const Token> tokens) -> SqlResult<Stmt>;

/// \brief Tokenizes and parses exactly one statement.
[[nodiscard]] auto parse(std::string_view text) -> SqlResult<Stmt>;

namespace detail {
/// \brief Recursive-descent SELECT parser, kept as an oracle for differential tests only.
/// \details Bypasses the LL(1) engine; production callers must use parse/parse_tokens.
[[nodiscard]] auto parse_tokens_recursive_reference(std::span<const Token> tokens)
    -> SqlResult<Stmt>;
}  // namespace detail

}  // namespace kravidb::sql
