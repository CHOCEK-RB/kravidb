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
literal        = integer | decimal | string | "NULL" ;
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
| expr | `identifier`, `integer`, `decimal`, `string`, `NULL`, `(` | `)`, `;`, `$` |
| or_tail | `OR`, `ε` | `)`, `;`, `$` |
| and_expr | `identifier`, `integer`, `decimal`, `string`, `NULL`, `(` | `OR`, `)`, `;`, `$` |
| and_tail | `AND`, `ε` | `OR`, `)`, `;`, `$` |
| predicate | `identifier`, `integer`, `decimal`, `string`, `NULL`, `(` | `AND`, `OR`, `)`, `;`, `$` |
| predicate_tail | `=`, `<>`, `<`, `<=`, `>`, `>=`, `IS`, `ε` | `AND`, `OR`, `)`, `;`, `$` |
| primary | `identifier`, `integer`, `decimal`, `string`, `NULL`, `(` | `=`, `<>`, `<`, `<=`, `>`, `>=`, `IS`, `AND`, `OR`, `)`, `;`, `$` |
| comparison | `=`, `<>`, `<`, `<=`, `>`, `>=` | `identifier`, `integer`, `decimal`, `string`, `NULL`, `(` |
| not_opt | `NOT`, `ε` | `NULL` |
| literal | `integer`, `decimal`, `string`, `NULL` | `=`, `<>`, `<`, `<=`, `>`, `>=`, `IS`, `AND`, `OR`, `)`, `;`, `$` |
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
the table by `build_table()`.

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
| literal | `integer` | 24: `literal -> integer` |
| literal | `decimal` | 25: `literal -> decimal` |
| literal | `string` | 26: `literal -> string` |
| literal | `NULL` | 27: `literal -> NULL` |
| comparison | `=` | 28: `comparison -> =` |
| comparison | `<>` | 29: `comparison -> <>` |
| comparison | `<` | 30: `comparison -> <` |
| comparison | `<=` | 31: `comparison -> <=` |
| comparison | `>` | 32: `comparison -> >` |
| comparison | `>=` | 33: `comparison -> >=` |
| semicolon_opt | `;` | 34: `semicolon_opt -> ;` |
| semicolon_opt | `$` | 35: `semicolon_opt -> ε` |
