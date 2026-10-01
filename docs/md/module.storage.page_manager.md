[<- Indice](index.md)

# storage.page_manager

**Modulo** - `src/storage/page_manager.cppm` (linea 4)

Implementacion paginada del almacen de registros (`PageManager`).

**Re-exporta:** `storage.record_store`, `storage.row_id`

**Tipos declarados:**

- [kravidb::storage::detail::Page](kravidb.storage.detail.Page.md)
- [kravidb::storage::PageManager](kravidb.storage.PageManager.md)

## Variables

### `slot_overhead`

```cpp
std::size_t slot_overhead
```

Coste de contabilidad por ranura: la longitud `u32` que la precede.

### `default_page_size`

```cpp
std::size_t default_page_size
```

Tamano de pagina por defecto, en bytes (4 KiB).
