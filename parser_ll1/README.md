# LL(1) SELECT parser

Documentation package for the predictive LL(1) SQL parser. The engine is built
as the `sql.select_ll1` module of `kravidb_lib`, and its visualizer is the
`kravidb_parser` binary. The SQL lexer feeds it tokens directly, and the engine
builds the SELECT AST in the same pass with a syntax-directed translation (SDT):

```text
tokens → LL(1) + SDT → AST (SelectStmt) → analyzer
```

## Contents

| File | Role |
| --- | --- |
| `GRAMMAR.md` | EBNF and BNF, FIRST/FOLLOW sets, parse table `M[A,a]`, the LL(1) conflict argument and the SDT: value stack, markers and production → action table. |
| `../src/sql/select_ll1.cppm` | Public interface: grammar data, `build_table`, trace types, `parse_select_ll1` and `validate_select`. |
| `../src/sql/select_ll1.cpp` | Table construction, stack-based engine with the SDT actions, diagnostics and tracer. |
| `../apps/sql_parser/main.cpp` | `kravidb_parser` FTXUI visualizer: source, token tape, stack, active table cell and trace. |
| `../tests/test_sql_select_ll1.cpp` | Unit tests for the grammar, table, trace, error path, acceptance/rejection corpus and AST parity of the SDT against the recursive oracle. |
| `../src/sql/lexer.cppm` | SQL lexer; `sql::tokenize` produces the token stream. |
| `../src/sql/parser.cpp` | Front-end entry point; translates SELECT with `parse_select_ll1` and parses DML/DDL by recursive descent. |

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

## Entry points

| Function | Returns | Use |
| --- | --- | --- |
| `parse_select_ll1(tokens, trace)` | `SqlResult<SelectStmt>` | Parses and translates one SELECT in a single LL(1) pass. Used by `parse`/`parse_tokens`. |
| `validate_select(tokens, trace)` | `SqlResult<void>` | Wrapper over `parse_select_ll1` that discards the AST; same trace and errors. Used by the visualizer. |

The optional `trace` callback receives one `TraceStep` per `expand`, `match`,
`accept` or `error`. The SDT's internal reduction markers are filtered out, so
`TraceStep.stack` only holds grammar symbols and `stack_size == stack.size()`.

## Error reporting

`parse_select_ll1` (and therefore `validate_select`) returns a `SqlError` with the SQLSTATE, the 1-based line and
column, the token found and the terminals expected from the current table row:

```text
syntax error at line 1, column 8 at or near "from"; expected: *, identifier
```

## Design note

The LL(1) engine both decides acceptance and builds the AST. Expanding
`A -> X1..Xn` pushes a `Reduce` marker below the right-hand side; matched
terminals push their token onto a value stack, and popping the marker runs the
production's semantic action over the `n` values of its right-hand side. Lists
keep source order and `AND`/`OR` tails are folded left. `GRAMMAR.md` documents
the attribute of every symbol and the action of each of the 38 productions.
The recursive-descent `Parser::parse_select` is kept only as a test oracle
(`detail::parse_tokens_recursive_reference`) for the AST-parity tests.
