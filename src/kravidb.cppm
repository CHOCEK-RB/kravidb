/// \file
/// \brief Public prelude of the kravidb storage engine.
///
/// Re-exports the contracts and implementations so a consumer only needs
/// `import kravidb;`.

export module kravidb;

/// \brief Paged record storage contract (`RecordStore`).
export import storage.record_store;

/// \brief Paged implementation of the storage (`PageManager`).
export import storage.page_manager;

/// \brief Table facade that combines records and index (`Table`).
export import storage.table;

/// \brief Column and relational schema definition (`Schema`, `ColumnDef`).
export import storage.schema;

/// \brief Facade that assembles the engine components (`StorageEngine`).
export import storage.engine;

/// \brief Index contract (`Index`).
export import index.index;

/// \brief B-tree implementation (`BTree`).
export import index.btree;

/// \brief Relation file contract (`RelationFile`).
export import storage.relation_file;

/// \brief File-backed relation storage with segments (`FileRelationFile`).
export import storage.file_relation_file;

/// \brief 8 KiB page layout (`Page`, `PageHeader`, `ItemId`).
export import storage.page;

/// \brief Physical heap tuple (`HeapTuple`, `HeapTupleHeader`, `ItemPointerData`).
export import storage.heap_tuple;

/// \brief Page source contract and in-memory adapter (`PageSource`, `MemoryPageSource`).
export import storage.page_source;

/// \brief Buffer pool contract (`BufferPool`, `PageGuard`, `BufferTag`).
export import storage.buffer_pool;

/// \brief WAL contract and no-op adapter (`Wal`, `NoopWal`).
export import storage.wal;

/// \brief File-backed write-ahead log (`FileWal`).
export import storage.file_wal;

/// \brief Statement transaction contract (`TransactionManager`, `Transaction`).
export import storage.transaction;

/// \brief Catalog contract and in-memory adapter (`Catalog`, `SchemaCatalog`).
export import storage.catalog;

/// \brief In-memory index adapter (`MemoryBTree`).
export import index.memory_btree;

/// \brief SQL source position (`SourcePosition`).
export import sql.source;

/// \brief SQL error value and result alias (`SqlError`, `SqlResult`).
export import sql.error;

/// \brief Typed SQL abstract syntax tree (`Stmt`, `Expr`, `Literal`).
export import sql.ast;

/// \brief SQL tokens and lexer (`Token`, `TokenKind`, `Keyword`).
export import sql.lexer;
