[<- Indice](index.md)

# kravidb::storage::PageIOStats

**Struct** - `src/storage/record_store.cppm` (linea 16)

Contadores logicos de E/S de paginas del almacen.

Solo se incrementan en las operaciones tratadas como E/S real (`insert` y `read`). Las lecturas por `slot_at` no los modifican, de modo que un recorrido secuencial completo puede no contabilizar lecturas.

## Atributos publicos

### `page_reads`

```cpp
std::size_t page_reads
```

Numero de lecturas de pagina acumuladas.

### `page_writes`

```cpp
std::size_t page_writes
```

Numero de escrituras de pagina acumuladas.
