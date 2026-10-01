/// \file
/// \brief Preludio publico del motor de almacenamiento kravidb.
///
/// Re-exporta los contratos y las implementaciones para que un consumidor
/// necesite unicamente `import kravidb;`.

export module kravidb;

/// \brief Contrato de almacenamiento de registros paginado (`RecordStore`).
export import storage.record_store;

/// \brief Implementacion paginada del almacenamiento (`PageManager`).
export import storage.page_manager;

/// \brief Fachada de tabla que combina registros e indice (`Table`).
export import storage.table;

/// \brief Fachada que ensambla los componentes del motor (`StorageEngine`).
export import storage.engine;

/// \brief Contrato de indice (`Index`).
export import index.index;

/// \brief Implementacion de arbol B (`BTree`).
export import index.btree;
