[<- Indice](index.md)

# kravidb::storage::StorageEngine

**Clase** - `src/storage/engine.cppm` (linea 19)

Motor de almacenamiento: posee y conecta almacen, indice y tabla.

Es el punto de entrada de mas alto nivel. Construye un `PageManager`, un `BTree` y un `Table` que los enlaza, y expone cada componente por referencia.

## Atributos privados

### `records_`

```cpp
PageManager records_
```

Almacen de registros paginado.

### `btree_`

```cpp
index::BTree btree_
```

Indice sobre la clave primaria.

### `table_`

```cpp
Table table_
```

Tabla que combina almacen e indice.

## Funciones publicas

### `StorageEngine`

```cpp
StorageEngine(std::size_t page_size=default_page_size, int degree=index::recommended_degree)
```

Construye el motor con el tamano de pagina y el grado indicados.

**Parametros**

- `page_size` - Tamano de pagina del almacen, en bytes.
- `degree` - Grado minimo `t` del arbol B del indice.

### `records`

```cpp
RecordStore & records() noexcept
```

Acceso al almacen de registros.

### `index`

```cpp
index::Index & index() noexcept
```

Acceso al indice.

### `table`

```cpp
Table & table() noexcept
```

Acceso a la tabla.
