# LL(1) SELECT parser

Documentation package for the predictive LL(1) SQL parser. The recognizer is built
as the `sql.select_ll1` module of `kravidb_lib`, and its visualizer is the
`kravidb_parser` binary. The SQL lexer feeds it tokens directly.

## Contents

| File | Role |
| --- | --- |
| `GRAMMAR.md` | EBNF and BNF, FIRST/FOLLOW sets, parse table `M[A,a]` and the LL(1) conflict argument. |
| `../src/sql/select_ll1.cppm` | Public interface: grammar data, `build_table`, trace types and `validate_select`. |
| `../src/sql/select_ll1.cpp` | Stack-based recognizer, table construction, diagnostics and tracer. |
| `../apps/sql_parser/main.cpp` | `kravidb_parser` FTXUI visualizer: source, token tape, stack, active table cell and trace. |
| `../tests/test_sql_select_ll1.cpp` | Unit tests for the grammar, table, trace, error path and acceptance/rejection corpus. |
| `../src/sql/lexer.cppm` | SQL lexer; `sql::tokenize` produces the token stream. |
| `../src/sql/parser.cpp` | Front-end entry point; calls `validate_select` before building the AST. |

## Build and run

```bash
xmake build kravidb_parser
xmake run kravidb_parser
xmake run kravidb_parser --query "SELECT id, name FROM users WHERE id >= 10 AND name <> 'ada';"
xmake test
```

The visualizer is interactive: `←`/`→` step through the trace, `Space` plays it,
`Tab` shows the full parse table, `D` cycles the built-in demos and `Q` quits.

Only the LL(1) tests:

```bash
./build/linux/x86_64/debug/kravidb_tests --gtest_filter='SqlSelectLl1*'
```

## Supported language

`SELECT` with `*` or a comma-separated column list, a `FROM` table, and an
optional `WHERE` with the comparison operators `=`, `<>`, `<`, `<=`, `>`, `>=`,
the logical operators `AND`/`OR`, parentheses, literals and `IS [NOT] NULL`.
The trailing `;` is optional by team decision, so both `SELECT a FROM t` and
`SELECT a FROM t;` are accepted. `INSERT`, `UPDATE`, `DELETE` and the DDL
statements keep their existing recursive parser; `GRAMMAR.md` documents them in
the annex.

## Error reporting

`validate_select` returns a `SqlError` with the SQLSTATE, the 1-based line and
column, the token found and the terminals expected from the current table row:

```text
syntax error at line 1, column 8 at or near "from"; expected: *, identifier
```

## Design note

The LL(1) engine decides acceptance and rejection. After validation,
`Parser::parse_select` builds the SELECT AST with an explicit task stack
(`parse_expr_iterative`); the recursive reference parser is kept only as a test
oracle.
