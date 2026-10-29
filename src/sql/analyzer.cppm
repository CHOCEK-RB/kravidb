/// \file
/// \brief Semantic analysis of SELECT against the catalog.
///
/// \details The analyzer resolves the relation and every column through the
/// `Catalog`, assigns SQL types and checks that expressions are well typed.
/// It never touches the heap: a statement that fails here has read no rows.
///
/// Type rules:
/// - `BIGINT` and `DECIMAL` are comparable with each other.
/// - `VARCHAR` is comparable only with `VARCHAR`.
/// - `BOOLEAN` is comparable only with `BOOLEAN`.
/// - The `NULL` literal is comparable with every type.
/// - `AND`, `OR` and `WHERE` need `BOOLEAN` operands (or `NULL`).
/// - A numeric literal compared with a `DECIMAL(p,s)` column must have at most
///   `p - s` integer digits.
///
/// Errors: `42P01` unknown relation, `42809` relation is not a table,
/// `42703` unknown column, `42804` incompatible types, `22003` numeric value
/// out of range, `22023` invalid DECIMAL precision or scale in the catalog,
/// `0A000` statement other than SELECT.

export module sql.analyzer;

import std;
export import sql.ast;
export import sql.error;
export import sql.query;
export import storage.catalog;
export import storage.transaction;

export namespace kravidb::sql {

/// \brief Largest DECIMAL precision the storage layer can represent.
inline constexpr std::uint8_t max_decimal_precision = storage::decimal_max_scale;

/// \brief Resolves `select` against `catalog`.
[[nodiscard]] auto analyze_select(const SelectStmt& select, const storage::Catalog& catalog,
                                  storage::Transaction& transaction) -> SqlResult<Query>;

/// \brief Resolves `select` against a relation cache snapshot.
[[nodiscard]] auto analyze_select(const SelectStmt& select, const storage::RelCache& cache)
    -> SqlResult<Query>;

/// \brief Resolves `statement`; only SELECT is supported so far.
[[nodiscard]] auto analyze(const Stmt& statement, const storage::Catalog& catalog,
                           storage::Transaction& transaction) -> SqlResult<Query>;

/// \brief Resolves `statement` against a relation cache snapshot.
[[nodiscard]] auto analyze(const Stmt& statement, const storage::RelCache& cache)
    -> SqlResult<Query>;

/// \brief Resolves and validates any supported DML/DDL statement without mutating the catalog.
[[nodiscard]] auto analyze_statement(const Stmt& statement, const storage::Catalog& catalog,
                                     storage::Transaction& transaction)
    -> SqlResult<AnalyzedStatement>;

/// \brief Parses and validates one DML/DDL statement without executing it.
[[nodiscard]] auto prepare_statement(std::string_view text, const storage::Catalog& catalog,
                                     storage::Transaction& transaction)
    -> SqlResult<AnalyzedStatement>;

/// \brief Parses and analyzes `text`: the single entry point of the SQL front end.
///
/// \details Any statement that fails to lex, parse or analyze returns an
/// error here and never reaches the planner or the executor.
[[nodiscard]] auto prepare_query(std::string_view text, const storage::Catalog& catalog,
                                 storage::Transaction& transaction) -> SqlResult<Query>;

/// \brief Parses and analyzes `text` against a relation cache snapshot.
[[nodiscard]] auto prepare_query(std::string_view text, const storage::RelCache& cache)
    -> SqlResult<Query>;

}  // namespace kravidb::sql
