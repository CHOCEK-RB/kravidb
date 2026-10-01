[<- Indice](index.md)

# kravidb::index::BTree

**Clase** - `src/index/btree.cppm` (linea 30)

Arbol B clasico (CLRS) que implementa el contrato `Index`.

Cada nodo guarda como maximo `2t - 1` claves y tiene como maximo `2t` hijos, donde `t` es el grado minimo (`t >= 2`). Los nodos se poseen a traves del pool interno y `root_` es un puntero no propietario al nodo raiz. El arbol es movible pero no copiable.

## Atributos privados

### `degree_`

```cpp
int degree_
```

Grado minimo `t` del arbol.

### `pool_`

```cpp
std::vector< std::unique_ptr< BTreeNode > > pool_
```

Propietario de todos los nodos del arbol.

### `root_`

```cpp
BTreeNode * root_
```

Puntero no propietario a la raiz; nulo si el arbol esta vacio.

### `last_search_stats_`

```cpp
SearchStats last_search_stats_
```

Estadisticas de la ultima busqueda, mutables desde `search` (const).

## Funciones publicas

### `BTree`

```cpp
BTree(int degree=recommended_degree)
```

Construye un arbol B vacio.

**Parametros**

- `degree` - Grado minimo `t`; debe ser mayor o igual que 2.

### `BTree`

```cpp
BTree(const BTree &)=delete
```

Copia deshabilitada.

### `operator=`

```cpp
BTree & operator=(const BTree &)=delete
```

Asignacion por copia deshabilitada.

### `BTree`

```cpp
BTree(BTree &&other) noexcept
```

Construye por movimiento, transfiriendo los nodos del otro arbol.

### `operator=`

```cpp
BTree & operator=(BTree &&other) noexcept
```

Asigna por movimiento, liberando antes el contenido actual.

### `~BTree`

```cpp
~BTree() override=default
```

Destructor.

### `clear`

```cpp
void clear() override
```

Elimina todas las claves y libera los nodos.

### `empty`

```cpp
bool empty() const noexcept override
```

Indica si el arbol no tiene claves.

### `last_search_stats`

```cpp
SearchStats last_search_stats() const noexcept override
```

Estadisticas de la ultima busqueda.

### `reset_search_stats`

```cpp
void reset_search_stats() noexcept override
```

Pone a cero las estadisticas de busqueda.

### `insert`

```cpp
void insert(Key key, RowID row_id) override
```

Inserta o reemplaza la asociacion de una clave.

**Parametros**

- `key` - Clave a insertar.
- `row_id` - Localizador asociado.

### `search`

```cpp
std::optional< RowID > search(Key key) const override
```

Busca una clave y registra sus estadisticas.

**Parametros**

- `key` - Clave buscada.

**Devuelve** - El `RowID` asociado, o `std::nullopt` si la clave no existe.

### `min_degree`

```cpp
int min_degree() const noexcept
```

Grado minimo `t` del arbol.

### `root`

```cpp
const BTreeNode * root() const noexcept
```

Puntero no propietario al nodo raiz.

### `height`

```cpp
std::size_t height() const noexcept
```

Altura del arbol, en numero de nodos desde la raiz hasta una hoja.

### `search_with_stats`

```cpp
SearchResult search_with_stats(Key key) const
```

Busca una clave devolviendo el resultado junto con sus estadisticas.

**Parametros**

- `key` - Clave buscada.

**Devuelve** - El localizador encontrado y los contadores de la busqueda.

## Funciones privadas

### `split_child`

```cpp
void split_child(BTreeNode *parent, std::size_t index, BTreeNode *child)
```

Divide el hijo lleno `child`, promoviendo su clave media al padre.

**Parametros**

- `parent` - Nodo padre de `child`.
- `index` - Posicion de `child` dentro de `parent`.
- `child` - Nodo hijo lleno que se divide.

### `insert_non_full`

```cpp
void insert_non_full(BTreeNode *node, Key key, RowID row_id)
```

Inserta en un nodo que no esta lleno, descendiendo si es necesario.

**Parametros**

- `node` - Nodo en el que insertar.
- `key` - Clave a insertar.
- `row_id` - Localizador asociado.

### `create_node`

```cpp
BTreeNode * create_node(bool leaf)
```

Reserva un nodo nuevo en el pool y devuelve un puntero a el.

**Parametros**

- `leaf` - Indica si el nodo nuevo es hoja.
