[<- Indice](index.md)

# index.btree_node

**Modulo** - `src/index/btree_node.cppm` (linea 4)

Nodo del arbol B (`BTreeNode`) y sus invariantes.

**Tipos declarados:**

- [kravidb::index::BTreeNode](kravidb.index.BTreeNode.md)

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
