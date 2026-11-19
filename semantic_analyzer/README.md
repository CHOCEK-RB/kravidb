# Semantic analyzer visualizer

FTXUI visualizer for the SELECT semantic analyzer. It takes the typed tree
produced by `sql::prepare_query`, shows the demo catalog, the decorated AST and
the resolved targets, and reports semantic errors without leaving the terminal.

```text
SQL text -> lexer -> LL(1) + SDT -> SelectStmt -> analyzer -> Query
                                                    |
                                          kravidb_semantic
```

## Contents

| File | Role |
| --- | --- |
| `../apps/sql_semantic/main.cpp` | `kravidb_semantic` FTXUI application: input, demo catalog, AST, targets and diagnostics. |
| `../src/sql/explain.cppm` | Public interface of the decorated-AST formatter: `explain_query` and `format_query`. |
| `../src/sql/explain.cpp` | Renders the typed tree with node labels, inferred types, positions and left-folded operators. |
| `../tests/test_sql_explain.cpp` | Formatter tests for targets, predicates, boolean/NULL constants and an absent WHERE. |

## Build and run

```bash
xmake build kravidb_semantic
xmake run kravidb_semantic
xmake run kravidb_semantic --query "SELECT id, name FROM students WHERE gpa >= 3.5 AND active = TRUE;"
xmake run kravidb_semantic --print --query "SELECT * FROM students;"
```

`--print` writes the same decorated AST to standard output and exits with `0`
for a valid query or `1` for a lexical, syntax or semantic error. Without
`--print`, the application opens the interactive view.

## Demo catalog

| Relation | Columns |
| --- | --- |
| `students` | `id BIGINT PK`, `name VARCHAR`, `gpa FLOAT`, `active BOOLEAN` |
| `courses` | `id BIGINT PK`, `title VARCHAR`, `credits BIGINT` |
| `enrollments` | `student_id BIGINT`, `course_id BIGINT`, `grade FLOAT`, `passed BOOLEAN` |

The catalog is in memory (`storage::SchemaCatalog`); it has metadata only and
does not read or write heap rows.

## Interactive view

```text
┌ kravidb · semantic analyzer ──────────────────────────────────────────────┐
│ SQL  SELECT id, name FROM students WHERE gpa >= 3.5 AND active = TRUE;    │
├──────────────────┬───────────────────────────────────┬────────────────────┤
│ Catalog          │ Decorated AST                     │ Resolved targets   │
│ students         │ SelectStmt students (oid=1)       │ students (#1)      │
│  #1 id bigint PK │ ├─ Targets                        │  #1 id : bigint    │
│  #2 name varchar │ │  ├─ id : bigint (#1)            │  #2 name : varchar │
│  #3 gpa float    │ │  └─ name : varchar (#2)         │                    │
│  #4 active bool  │ └─ Where : boolean                │                    │
│ courses ...      │    └─ AND : boolean @ 1:48        │                    │
│                  │       ├─ >= : boolean @ 1:41      │                    │
│                  │       │  ├─ gpa : float (#3)      │                    │
│                  │       │  └─ 3.5 : decimal        │                    │
│                  │       └─ = : boolean @ 1:59       │                    │
├──────────────────┴───────────────────────────────────┴────────────────────┤
│ OK · 2 targets · 1 predicate                  e editar · d demo · q salir │
└───────────────────────────────────────────────────────────────────────────┘
```

Keys:

- `e`, `E` or `/`: edit the SQL text.
- `Enter`: analyze the edited text.
- `Esc`: cancel editing, or quit when not editing.
- `d`: load the next demo.
- `q`: quit.

On terminals narrower than 110 columns the side panes shrink and the AST hides
source positions; `--print` always includes them.

## Demos

The built-in demos cover explicit projection, `SELECT *`, a compound predicate,
a duplicate target (`42701`), an unknown column (`42703`), an unknown relation
(`42P01`) and an incompatible comparison (`42804`).

## Scope

The visualizer is read-only: it resolves metadata and types but does not execute
the query. FLOAT and BOOLEAN columns are metadata only; row encoding and
executor support are separate work.
