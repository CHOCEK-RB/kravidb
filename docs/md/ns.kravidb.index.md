[<- Indice](index.md)

# kravidb::index

**Namespace** - `src/index/btree.cppm` (linea 10)

**Tipos declarados:**

- [kravidb::index::BTree](kravidb.index.BTree.md)
- [kravidb::index::BTreeNode](kravidb.index.BTreeNode.md)
- [kravidb::index::Index](kravidb.index.Index.md)
- [kravidb::index::SearchResult](kravidb.index.SearchResult.md)
- [kravidb::index::SearchStats](kravidb.index.SearchStats.md)

## Alias de tipo

### `Key`

```cpp
using Key = std::int64_t
```

Clave indexada: entero de 64 bits con signo.

### `RowID`

```cpp
using RowID = std::uint64_t
```

Localizador de registro almacenado junto a cada clave.

## Variables

### `recommended_degree`

```cpp
int recommended_degree
```

Grado minimo por defecto del arbol B.
