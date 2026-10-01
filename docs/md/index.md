# kravidb - Documentacion de la API

Re-exporta los contratos y las implementaciones para que un consumidor necesite unicamente `import kravidb;`.

## Modulos

- [index.btree](module.index.btree.md) - Arbol B como implementacion del contrato `Index`.
- [index.btree_node](module.index.btree_node.md) - Nodo del arbol B (`BTreeNode`) y sus invariantes.
- [index.index](module.index.index.md) - Contrato `Index` y estadisticas de busqueda.
- [kravidb](module.kravidb.md) - Preludio publico del motor de almacenamiento kravidb.
- [storage.engine](module.storage.engine.md) - Fachada `StorageEngine` que ensambla los componentes del motor.
- [storage.page_manager](module.storage.page_manager.md) - Implementacion paginada del almacen de registros (`PageManager`).
- [storage.record_store](module.storage.record_store.md) - Contrato `RecordStore` y estadisticas logicas de E/S de paginas.
- [storage.row_id](module.storage.row_id.md) - Localizadores de registro: empaquetado y desempaquetado de `RowID`.
- [storage.table](module.storage.table.md) - Fachada de tabla: combina un almacen de registros y un indice.
- [storage.tuple](module.storage.tuple.md) - Serializacion binaria de tuplas (`Tuple`).

## Clases y estructuras

- [kravidb::index::BTree](kravidb.index.BTree.md) - Arbol B clasico (CLRS) que implementa el contrato `Index`.
- [kravidb::index::BTreeNode](kravidb.index.BTreeNode.md) - Nodo de un arbol B, con `2t - 1` claves como maximo.
- [kravidb::index::Index](kravidb.index.Index.md) - Contrato abstracto de un indice que asocia clave con `RowID`.
- [kravidb::storage::PageManager](kravidb.storage.PageManager.md) - Almacen de registros paginado, de crecimiento solo por el final.
- [kravidb::storage::RecordStore](kravidb.storage.RecordStore.md) - Contrato abstracto de un almacen de registros paginado.
- [kravidb::storage::StorageEngine](kravidb.storage.StorageEngine.md) - Motor de almacenamiento: posee y conecta almacen, indice y tabla.
- [kravidb::storage::Table](kravidb.storage.Table.md) - Tabla que persiste tuplas y las indexa por clave primaria.
- [kravidb::storage::Tuple](kravidb.storage.Tuple.md) - Coleccion ordenada de campos con serializacion binaria propia.
- [kravidb::storage::detail::Page](kravidb.storage.detail.Page.md) - Pagina en memoria: ranuras contiguas con su contabilidad de uso.
- [kravidb::index::SearchResult](kravidb.index.SearchResult.md) - Resultado de una busqueda que ademas informa de sus estadisticas.
- [kravidb::index::SearchStats](kravidb.index.SearchStats.md) - Contadores de la ultima busqueda realizada en un indice.
- [kravidb::storage::PageIOStats](kravidb.storage.PageIOStats.md) - Contadores logicos de E/S de paginas del almacen.
- [kravidb::storage::RowLocation](kravidb.storage.RowLocation.md) - Ubicacion logica de un registro dentro del almacen.

## Namespaces

- [kravidb](ns.kravidb.md)
- [kravidb::index](ns.kravidb.index.md)
- [kravidb::storage](ns.kravidb.storage.md)
- [kravidb::storage::detail](ns.kravidb.storage.detail.md)
