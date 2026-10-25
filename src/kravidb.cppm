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
