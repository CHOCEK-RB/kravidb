/// \file
/// \brief Serializadores del motor kravidb al contrato JSON del frontend.

export module api.serialization;

import std;
import storage.record_store;
import storage.row_id;
import storage.table;
import storage.tuple;
import index.btree;
import index.btree_node;
import index.index;
import api.json;

export namespace kravidb::api {

/// \brief Serializa un nodo del arbol B y todo su subarbol.
/// \param records Almacen del que leer la carga util de cada clave.
/// \param node Nodo raiz del subarbol a serializar.
/// \return El nodo en el formato de `BTreeNode` del frontend.
[[nodiscard]] auto node_to_json(const storage::RecordStore& records, const index::BTreeNode& node)
    -> std::string;

/// \brief Serializa una pagina ranurada.
/// \param records Almacen del que leer las ranuras.
/// \param page_id Identificador de la pagina.
/// \return La pagina en el formato de `SlottedPageData` del frontend.
[[nodiscard]] auto page_to_json(const storage::RecordStore& records, storage::PageID page_id)
    -> std::string;

/// \brief Serializa las metricas de busqueda (Index Scan contra Full Scan).
/// \param records Almacen del que leer la tupla encontrada.
/// \param tree Indice sobre el que medir el descenso.
/// \param table Tabla sobre la que medir el barrido completo.
/// \param key Clave a buscar.
/// \return Las metricas en el formato de `SearchMetrics` del frontend.
[[nodiscard]] auto search_to_json(const storage::RecordStore& records, const index::BTree& tree,
                                  const storage::Table& table, index::Key key) -> std::string;

/// \brief Serializa el resultado de una insercion.
/// \param stats Estadisticas de la insercion en el indice.
/// \param row_id Ubicacion de la tupla recien escrita.
/// \return El resultado en el formato de `InsertResult` del frontend.
[[nodiscard]] auto insert_to_json(const index::InsertStats& stats, storage::RowID row_id)
    -> std::string;

}  // namespace kravidb::api
