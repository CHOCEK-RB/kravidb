[<- Indice](index.md)

# kravidb::index::SearchResult

**Struct** - `src/index/btree.cppm` (linea 16)

Resultado de una busqueda que ademas informa de sus estadisticas.

## Atributos publicos

### `row_id`

```cpp
std::optional< RowID > row_id
```

Localizador encontrado, o `std::nullopt` si la clave no existe.

### `stats`

```cpp
SearchStats stats
```

Estadisticas de la busqueda que produjo el resultado.
