/// \file
/// \brief Recursive-descent parser for the supported SELECT statement.
///
/// \details Grammar accepted by `parse`:
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
/// literal     := integer | decimal | string | NULL
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

export namespace kravidb::sql {

/// \brief Parses exactly one statement from a token sequence.
///
/// \details `tokens` must end with `TokenKind::end_of_input`, as produced by
/// `tokenize`. A trailing `;` is allowed; anything after it is rejected.
[[nodiscard]] auto parse_tokens(std::span<const Token> tokens) -> SqlResult<Stmt>;

/// \brief Tokenizes and parses exactly one statement.
[[nodiscard]] auto parse(std::string_view text) -> SqlResult<Stmt>;

}  // namespace kravidb::sql
