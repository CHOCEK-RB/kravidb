# SELECT LL(1) grammar

## EBNF

```ebnf
statement      = "SELECT" targets "FROM" table [ where ] [ ";" ] ;
targets        = "*" | target { "," target } ;
target         = identifier ;
table          = identifier ;
where          = "WHERE" expr ;
expr           = and_expr { "OR" and_expr } ;
and_expr       = predicate { "AND" predicate } ;
predicate      = primary [ comparison primary | "IS" [ "NOT" ] "NULL" ] ;
primary        = identifier | literal | "(" expr ")" ;
literal        = integer | decimal | string | "NULL" | "TRUE" | "FALSE" ;
comparison     = "=" | "<>" | "!=" | "<" | "<=" | ">" | ">=" ;
```

`identifier`, `integer`, `decimal` and `string` are lexical categories.
`!=` is normalized to the same terminal as `<>` by the lexer, so the parser sees
one `not_equal` terminal.

## BNF

The grammar below is the exact shape stored in `select_grammar()`. An empty
right-hand side is epsilon. `EOF` is the end-of-input sentinel.

```bnf
<statement>      ::= SELECT <targets> FROM <table> <where_opt> <semicolon_opt> EOF
<targets>        ::= "*"
                   | <target> <targets_tail>
<target>         ::= identifier
<targets_tail>   ::= "," <target> <targets_tail>
                   | ε
<table>          ::= identifier
<where_opt>      ::= WHERE <expr>
                   | ε
<expr>           ::= <and_expr> <or_tail>
<or_tail>        ::= OR <and_expr> <or_tail>
                   | ε
<and_expr>       ::= <predicate> <and_tail>
<and_tail>       ::= AND <predicate> <and_tail>
                   | ε
<predicate>      ::= <primary> <predicate_tail>
<predicate_tail> ::= <comparison> <primary>
                   | IS <not_opt> NULL
                   | ε
<not_opt>        ::= NOT
                   | ε
<primary>        ::= "(" <expr> ")"
                   | identifier
                   | <literal>
<literal>        ::= integer
                   | decimal
                   | string
                   | NULL
                   | TRUE
                   | FALSE
<comparison>     ::= "="
                   | "<>"
                   | "<"
                   | "<="
                   | ">"
                   | ">="
<semicolon_opt>  ::= ";"
                   | ε
```

## Left recursion and factorization

The natural list and precedence rules are left recursive:

```bnf
<targets>  ::= <targets> "," <target> | <target>
<expr>     ::= <expr> OR <and_expr> | <and_expr>
<and_expr> ::= <and_expr> AND <predicate> | <predicate>
```

They are rewritten as right-recursive tails:

```bnf
<targets>  ::= <target> <targets_tail>
<targets_tail> ::= "," <target> <targets_tail> | ε
<expr>     ::= <and_expr> <or_tail>
<or_tail>  ::= OR <and_expr> <or_tail> | ε
<and_expr> ::= <predicate> <and_tail>
<and_tail> ::= AND <predicate> <and_tail> | ε
```

`targets` is also factorized on its first terminal: `*` or an identifier.
`primary` and `predicate_tail` have alternatives with disjoint FIRST sets. No
nonterminal is left recursive after these transformations.

## FIRST and FOLLOW

`$` denotes end of input. `ε` is the empty string.

| Nonterminal | FIRST | FOLLOW |
| --- | --- | --- |
| statement | `SELECT` | `$` |
| targets | `*`, `identifier` | `FROM` |
| target | `identifier` | `,`, `FROM` |
| targets_tail | `,`, `ε` | `FROM` |
| table | `identifier` | `WHERE`, `;`, `$` |
| where_opt | `WHERE`, `ε` | `;`, `$` |
| expr | `identifier`, `integer`, `decimal`, `string`, `NULL`, `TRUE`, `FALSE`, `(` | `)`, `;`, `$` |
| or_tail | `OR`, `ε` | `)`, `;`, `$` |
| and_expr | `identifier`, `integer`, `decimal`, `string`, `NULL`, `TRUE`, `FALSE`, `(` | `OR`, `)`, `;`, `$` |
| and_tail | `AND`, `ε` | `OR`, `)`, `;`, `$` |
| predicate | `identifier`, `integer`, `decimal`, `string`, `NULL`, `TRUE`, `FALSE`, `(` | `AND`, `OR`, `)`, `;`, `$` |
| predicate_tail | `=`, `<>`, `<`, `<=`, `>`, `>=`, `IS`, `ε` | `AND`, `OR`, `)`, `;`, `$` |
| primary | `identifier`, `integer`, `decimal`, `string`, `NULL`, `TRUE`, `FALSE`, `(` | `=`, `<>`, `<`, `<=`, `>`, `>=`, `IS`, `AND`, `OR`, `)`, `;`, `$` |
| comparison | `=`, `<>`, `<`, `<=`, `>`, `>=` | `identifier`, `integer`, `decimal`, `string`, `NULL`, `TRUE`, `FALSE`, `(` |
| not_opt | `NOT`, `ε` | `NULL` |
| literal | `integer`, `decimal`, `string`, `NULL`, `TRUE`, `FALSE` | `=`, `<>`, `<`, `<=`, `>`, `>=`, `IS`, `AND`, `OR`, `)`, `;`, `$` |
| semicolon_opt | `;`, `ε` | `$` |

The selection set of a production is FIRST(right-hand side), plus FOLLOW(left)
when the right-hand side is nullable. They are pairwise disjoint:

- `targets`: `*` and `identifier` are distinct.
- `targets_tail`: `,` and FOLLOW `FROM` are distinct.
- `where_opt`: `WHERE` and FOLLOW `;`/`$` are distinct.
- `or_tail`: `OR` and FOLLOW `)`/`;`/`$` are distinct.
- `and_tail`: `AND` and FOLLOW `OR`/`)`/`;`/`$` are distinct.
- `predicate_tail`: the six comparison terminals and `IS` are distinct, and none
  is in FOLLOW `AND`/`OR`/`)`/`;`/`$`.
- `not_opt`: `NOT` and FOLLOW `NULL` are distinct.
- `primary`: `(`, `identifier` and the literal starters are distinct.
- `literal` and `comparison`: all alternatives start with distinct terminals.
- `semicolon_opt`: `;` and FOLLOW `$` are distinct.
- Every remaining nonterminal has one production.

Therefore every cell of `M[A,a]` has at most one production.

## Parse table M[A,a]

Production numbers are the indices returned by `select_grammar()` and stored in
the table by `build_table()`. The grammar has 38 productions (0–37) and the
table has 79 non-empty cells; the same numbers select the semantic actions of
the SDT below.

| Nonterminal | Lookahead | Production |
| --- | --- | --- |
| statement | `SELECT` | 0: `statement -> SELECT targets FROM table where_opt semicolon_opt EOF` |
| targets | `*` | 1: `targets -> *` |
| targets | `identifier` | 2: `targets -> target targets_tail` |
| target | `identifier` | 3: `target -> identifier` |
| targets_tail | `,` | 4: `targets_tail -> , target targets_tail` |
| targets_tail | `FROM` | 5: `targets_tail -> ε` |
| table | `identifier` | 6: `table -> identifier` |
| where_opt | `WHERE` | 7: `where_opt -> WHERE expr` |
| where_opt | `;` | 8: `where_opt -> ε` |
| where_opt | `$` | 8: `where_opt -> ε` |
| expr | `identifier` | 9: `expr -> and_expr or_tail` |
| expr | `integer` | 9: `expr -> and_expr or_tail` |
| expr | `decimal` | 9: `expr -> and_expr or_tail` |
| expr | `string` | 9: `expr -> and_expr or_tail` |
| expr | `NULL` | 9: `expr -> and_expr or_tail` |
| expr | `TRUE` | 9: `expr -> and_expr or_tail` |
| expr | `FALSE` | 9: `expr -> and_expr or_tail` |
| expr | `(` | 9: `expr -> and_expr or_tail` |
| or_tail | `OR` | 10: `or_tail -> OR and_expr or_tail` |
| or_tail | `)` | 11: `or_tail -> ε` |
| or_tail | `;` | 11: `or_tail -> ε` |
| or_tail | `$` | 11: `or_tail -> ε` |
| and_expr | `identifier` | 12: `and_expr -> predicate and_tail` |
| and_expr | `integer` | 12: `and_expr -> predicate and_tail` |
| and_expr | `decimal` | 12: `and_expr -> predicate and_tail` |
| and_expr | `string` | 12: `and_expr -> predicate and_tail` |
| and_expr | `NULL` | 12: `and_expr -> predicate and_tail` |
| and_expr | `TRUE` | 12: `and_expr -> predicate and_tail` |
| and_expr | `FALSE` | 12: `and_expr -> predicate and_tail` |
| and_expr | `(` | 12: `and_expr -> predicate and_tail` |
| and_tail | `AND` | 13: `and_tail -> AND predicate and_tail` |
| and_tail | `OR` | 14: `and_tail -> ε` |
| and_tail | `)` | 14: `and_tail -> ε` |
| and_tail | `;` | 14: `and_tail -> ε` |
| and_tail | `$` | 14: `and_tail -> ε` |
| predicate | `identifier` | 15: `predicate -> primary predicate_tail` |
| predicate | `integer` | 15: `predicate -> primary predicate_tail` |
| predicate | `decimal` | 15: `predicate -> primary predicate_tail` |
| predicate | `string` | 15: `predicate -> primary predicate_tail` |
| predicate | `NULL` | 15: `predicate -> primary predicate_tail` |
| predicate | `TRUE` | 15: `predicate -> primary predicate_tail` |
| predicate | `FALSE` | 15: `predicate -> primary predicate_tail` |
| predicate | `(` | 15: `predicate -> primary predicate_tail` |
| predicate_tail | `=` | 16: `predicate_tail -> comparison primary` |
| predicate_tail | `<>` | 16: `predicate_tail -> comparison primary` |
| predicate_tail | `<` | 16: `predicate_tail -> comparison primary` |
| predicate_tail | `<=` | 16: `predicate_tail -> comparison primary` |
| predicate_tail | `>` | 16: `predicate_tail -> comparison primary` |
| predicate_tail | `>=` | 16: `predicate_tail -> comparison primary` |
| predicate_tail | `IS` | 17: `predicate_tail -> IS not_opt NULL` |
| predicate_tail | `AND` | 18: `predicate_tail -> ε` |
| predicate_tail | `OR` | 18: `predicate_tail -> ε` |
| predicate_tail | `)` | 18: `predicate_tail -> ε` |
| predicate_tail | `;` | 18: `predicate_tail -> ε` |
| predicate_tail | `$` | 18: `predicate_tail -> ε` |
| not_opt | `NOT` | 19: `not_opt -> NOT` |
| not_opt | `NULL` | 20: `not_opt -> ε` |
| primary | `(` | 21: `primary -> ( expr )` |
| primary | `identifier` | 22: `primary -> identifier` |
| primary | `integer` | 23: `primary -> literal` |
| primary | `decimal` | 23: `primary -> literal` |
| primary | `string` | 23: `primary -> literal` |
| primary | `NULL` | 23: `primary -> literal` |
| primary | `TRUE` | 23: `primary -> literal` |
| primary | `FALSE` | 23: `primary -> literal` |
| literal | `integer` | 24: `literal -> integer` |
| literal | `decimal` | 25: `literal -> decimal` |
| literal | `string` | 26: `literal -> string` |
| literal | `NULL` | 27: `literal -> NULL` |
| literal | `TRUE` | 28: `literal -> TRUE` |
| literal | `FALSE` | 29: `literal -> FALSE` |
| comparison | `=` | 30: `comparison -> =` |
| comparison | `<>` | 31: `comparison -> <>` |
| comparison | `<` | 32: `comparison -> <` |
| comparison | `<=` | 33: `comparison -> <=` |
| comparison | `>` | 34: `comparison -> >` |
| comparison | `>=` | 35: `comparison -> >=` |
| semicolon_opt | `;` | 36: `semicolon_opt -> ;` |
| semicolon_opt | `$` | 37: `semicolon_opt -> ε` |

## Syntax-directed translation (SDT)

The LL(1) engine does not only recognize a SELECT: `parse_select_ll1` builds
the `SelectStmt` while the table drives the parse, in a single pass. There is
no second parser and no AST construction after recognition:

```text
tokens → LL(1) + SDT (parse_select_ll1) → SelectStmt → analyzer
```

The translation is **S-attributed**: every nonterminal has exactly one
synthesized attribute, computed only from the attributes of its right-hand
side, and there are no inherited attributes. That is what lets a top-down
parser evaluate it with a value stack and reduction markers.

`validate_select` is a thin wrapper over `parse_select_ll1` that discards the
AST; it produces the same trace and the same errors.

### Parse stack, markers and value stack

The engine keeps two stacks:

- the **parse stack**, holding grammar symbols and `Reduce(p)` markers;
- the **value stack**, holding the attributes produced so far.

Each step looks at the top of the parse stack:

| Top | Action |
| --- | --- |
| nonterminal `A`, lookahead `a` | Look up `p = M[A,a]` (error if empty). Replace `A` by the marker `Reduce(p)` and push the right-hand side `X1..Xn` above it in reverse, so `X1` is on top. |
| terminal `x` | It must equal the lookahead (error otherwise). Pop it, push the matched `const Token*` onto the value stack and advance. |
| `Reduce(p)` | Production `p` has `n` symbols on its right-hand side, and each of them left exactly one value, so the top `n` values are the attributes `$1..$n` of `X1..Xn`, in order. Pop them, run the action of `p` and push its result: the attribute of `A`. For an ε-production `n = 0`. |

Because the marker sits *below* the right-hand side, it is reached only after
the whole subtree of `A` has been parsed and reduced: the action runs in
post-order, exactly as a bottom-up S-attributed evaluation would. The run
accepts when the parse stack is empty and all tokens were consumed; at that
point the value stack holds exactly one `SelectStmt` (anything else is reported
as an internal error, SQLSTATE `XX000`).

Tokens are not copied: terminals contribute pointers into the caller's token
span, which outlives the parse. Errors are detected before any expansion or
match, exactly as in the pure recognizer, and markers never take part in
diagnostics, so SQLSTATEs, messages and positions are the same as before the
SDT. On error the partial values are discarded; no partial AST is returned.

### Traces

Markers are an implementation detail of the SDT and never appear in a trace:

- `TraceStep.stack` contains grammar symbols only, and
  `TraceStep.stack_size == stack.size()`.
- Reductions do not emit trace steps; the actions remain `expand`, `match`,
  `accept` and `error`.
- With markers hidden, the visible stack evolves as in a pure LL(1)
  recognizer: an expansion replaces the top by the `n` symbols of the
  right-hand side and a match pops one terminal.

### Attributes

| Symbol | Attribute |
| --- | --- |
| terminal | `const Token*` of the matched token |
| `statement` | `SelectStmt` |
| `targets` | `std::vector<Identifier>` in source order; empty means `*` |
| `targets_tail` | list of `Identifier`, accumulated **last first** |
| `target`, `table` | `Identifier` (name and position) |
| `where_opt` | `std::unique_ptr<Expr>`; null when there is no `WHERE` |
| `expr`, `and_expr`, `predicate`, `primary` | `std::unique_ptr<Expr>` |
| `or_tail`, `and_tail` | list of (operator position, operand), accumulated **innermost first** |
| `predicate_tail` | nothing, `(op, position, right operand)` or `(IS [NOT] NULL, position)` |
| `not_opt` | `bool`: `true` when `NOT` is present |
| `comparison` | `BinaryOp` and the position of the operator token |
| `literal` | `Literal` (value and position) |
| `semicolon_opt` | no value (`std::monostate`) |

### Production → action table

`$i` is the attribute of the `i`-th right-hand-side symbol; `pos($i)` is the
position of a terminal. `make(op, pos, l, r)` creates a `BinaryExpr`. The
numbers match `select_grammar()`, the parse table above and the `P`
enumeration that selects the actions in `src/sql/select_ll1.cpp`.

| # | Production | Action |
| --- | --- | --- |
| 0 | `statement -> SELECT targets FROM table where_opt semicolon_opt EOF` | `SelectStmt{targets = $2, table = $4, where = $5}` |
| 1 | `targets -> *` | empty list (all columns) |
| 2 | `targets -> target targets_tail` | append `$1` to the reversed list `$2`, then reverse it into source order |
| 3 | `target -> identifier` | `Identifier{text($1), pos($1)}` |
| 4 | `targets_tail -> , target targets_tail` | `$3` with `$2` appended |
| 5 | `targets_tail -> ε` | empty list |
| 6 | `table -> identifier` | `Identifier{text($1), pos($1)}` |
| 7 | `where_opt -> WHERE expr` | `$2` |
| 8 | `where_opt -> ε` | null expression |
| 9 | `expr -> and_expr or_tail` | left fold of `$1` with the operands of `$2` using `OR` |
| 10 | `or_tail -> OR and_expr or_tail` | `$3` with `(pos($1), $2)` appended |
| 11 | `or_tail -> ε` | empty tail |
| 12 | `and_expr -> predicate and_tail` | left fold of `$1` with the operands of `$2` using `AND` |
| 13 | `and_tail -> AND predicate and_tail` | `$3` with `(pos($1), $2)` appended |
| 14 | `and_tail -> ε` | empty tail |
| 15 | `predicate -> primary predicate_tail` | `$1` if `$2` is empty, else `make($2.op, $2.pos, $1, $2.right)` |
| 16 | `predicate_tail -> comparison primary` | `($1.op, $1.pos, $2)` |
| 17 | `predicate_tail -> IS not_opt NULL` | `($2 ? is_not_null : is_null, pos($1), null)` |
| 18 | `predicate_tail -> ε` | empty tail |
| 19 | `not_opt -> NOT` | `true` |
| 20 | `not_opt -> ε` | `false` |
| 21 | `primary -> ( expr )` | `$2` (parentheses only group; no node) |
| 22 | `primary -> identifier` | `ColumnRef{Identifier{text($1), pos($1)}}` |
| 23 | `primary -> literal` | `Expr{$1}` |
| 24 | `literal -> integer` | `int64` value; `DecimalLiteral{text}` if it does not fit in BIGINT |
| 25 | `literal -> decimal` | `DecimalLiteral{text($1)}` |
| 26 | `literal -> string` | string value `text($1)` (the lexer already removed the quotes and unescaped `''`) |
| 27 | `literal -> NULL` | `NullLiteral` |
| 28 | `literal -> TRUE` | `true` |
| 29 | `literal -> FALSE` | `false` |
| 30 | `comparison -> =` | `(eq, pos($1))` |
| 31 | `comparison -> <>` | `(ne, pos($1))`; `!=` lexes as the same terminal |
| 32 | `comparison -> <` | `(lt, pos($1))` |
| 33 | `comparison -> <=` | `(le, pos($1))` |
| 34 | `comparison -> >` | `(gt, pos($1))` |
| 35 | `comparison -> >=` | `(ge, pos($1))` |
| 36 | `semicolon_opt -> ;` | no value |
| 37 | `semicolon_opt -> ε` | no value |

Every literal and every `Identifier` keeps the position of its token.

### Rules worth checking

**List order.** `targets_tail` is right recursive, so its innermost (last)
occurrence is reduced first. Appending at each reduction is O(1) but builds the
list last-first; production 2 appends the first target and reverses once.
`SELECT c, a, b` yields `targets = [c, a, b]`.

**Left fold of `OR`/`AND`.** The grammar must be right recursive to be LL(1),
but SQL's `OR` and `AND` are left associative. The tails therefore only collect
their operands (innermost first, like the target list), and the production that
owns the tail folds them from left to right:

```text
a OR b OR c
  or_tail₃ -> ε                 tail = []
  or_tail₂ -> OR c or_tail₃     tail = [(OR₂, c)]
  or_tail₁ -> OR b or_tail₂     tail = [(OR₂, c), (OR₁, b)]
  expr     -> a or_tail₁        fold backwards: (a OR₁ b) OR₂ c
```

Each `BinaryExpr` gets the position of its own operator. Precedence comes from
the grammar, not from the actions: `a OR b AND c` is `a OR (b AND c)` because
`AND` operands are `predicate`s inside one `and_expr`.

**`IS [NOT] NULL`.** The node is a `BinaryExpr` whose `right` child is null,
with `op = is_not_null` when `not_opt` matched `NOT` and `is_null` otherwise.
Its position is the position of `IS`, not of `NULL`. Production 17 is the only
place where `NULL` is a terminal of a predicate; `NULL` in production 27 is a
literal value, so `a = NULL` is a comparison, not a null check.

### Keeping code and documentation in sync

`parse_select_ll1` rejects a grammar whose size differs from the number of
actions, and `SqlSelectLl1.SdtHasOneActionPerProduction` pins the count at 38.
Adding or reordering a production requires updating, together:
`select_grammar()`, the `P` enumeration and `reduce()` in
`src/sql/select_ll1.cpp`, the parse table and the action table in this file.
The SDT is checked against the recursive-descent oracle
(`detail::parse_tokens_recursive_reference`) by the AST-parity tests in
`tests/test_sql_select_ll1.cpp`.
