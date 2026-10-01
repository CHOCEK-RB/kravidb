[<- Indice](index.md)

# kravidb::index::BTreeNode

**Clase** - `src/index/btree_node.cppm` (linea 21)

Nodo de un arbol B, con `2t - 1` claves como maximo.

El nodo no posee a sus hijos: solo guarda punteros, y es el arbol quien posee los nodos. Un nodo hoja no tiene hijos; un nodo interno tiene exactamente `claves + 1` hijos.

## Atributos privados

### `degree_`

```cpp
int degree_
```

Grado minimo `t`.

### `is_leaf_`

```cpp
bool is_leaf_
```

Indica si el nodo es hoja.

### `keys_`

```cpp
std::vector< Key > keys_
```

Claves, ordenadas de forma ascendente.

### `row_ids_`

```cpp
std::vector< RowID > row_ids_
```

Localizadores paralelos a las claves.

### `children_`

```cpp
std::vector< BTreeNode * > children_
```

Hijos; vacio en las hojas y `claves + 1` elementos en los nodos internos.

## Funciones publicas

### `BTreeNode`

```cpp
BTreeNode(int degree, bool leaf)
```

Crea un nodo vacio.

**Parametros**

- `degree` - Grado minimo `t`; debe ser mayor o igual que 2.
- `leaf` - Indica si el nodo es hoja.

### `BTreeNode`

```cpp
BTreeNode(const BTreeNode &)=delete
```

Copia deshabilitada.

### `operator=`

```cpp
BTreeNode & operator=(const BTreeNode &)=delete
```

Asignacion por copia deshabilitada.

### `BTreeNode`

```cpp
BTreeNode(BTreeNode &&)=delete
```

Movimiento deshabilitado (los hijos son punteros y la propiedad la fija el arbol).

### `operator=`

```cpp
BTreeNode & operator=(BTreeNode &&)=delete
```

Asignacion por movimiento deshabilitada.

### `~BTreeNode`

```cpp
~BTreeNode()=default
```

Destructor.

### `min_degree`

```cpp
int min_degree() const noexcept
```

Grado minimo `t` del nodo.

### `is_leaf`

```cpp
bool is_leaf() const noexcept
```

Indica si el nodo es hoja.

### `max_keys`

```cpp
std::size_t max_keys() const noexcept
```

Numero maximo de claves de un nodo (`2t - 1`).

### `min_keys`

```cpp
std::size_t min_keys() const noexcept
```

Numero minimo de claves de un nodo no raiz (`t - 1`).

### `max_children`

```cpp
std::size_t max_children() const noexcept
```

Numero maximo de hijos de un nodo interno (`2t`).

### `key_count`

```cpp
std::size_t key_count() const noexcept
```

Numero de claves almacenadas.

### `child_count`

```cpp
std::size_t child_count() const noexcept
```

Numero de hijos almacenados.

### `is_full`

```cpp
bool is_full() const noexcept
```

Indica si el nodo alcanzo el maximo de claves.

### `is_empty`

```cpp
bool is_empty() const noexcept
```

Indica si el nodo no tiene claves.

### `has_valid_shape`

```cpp
bool has_valid_shape() const noexcept
```

Comprueba las invariantes estructurales del nodo.

**Devuelve** - `true` si hay tantos `row_ids` como claves y no se supera `max_keys()`; en las hojas, si no hay hijos, y en los nodos internos, si hay `claves + 1` hijos.

### `lower_bound_index`

```cpp
std::size_t lower_bound_index(Key key, std::size_t &comparisons) const
```

Indice de la primera clave que no es menor que `key`.

**Parametros**

- `key` - Clave a comparar.
- `comparisons` - Contador que se incrementa con cada comparacion realizada.

**Devuelve** - La posicion dentro de `keys()` donde buscar o insertar `key`.

### `keys`

```cpp
std::vector< Key > & keys() noexcept
```

Acceso mutable a las claves.

### `keys`

```cpp
const std::vector< Key > & keys() const noexcept
```

Acceso de solo lectura a las claves.

### `row_ids`

```cpp
std::vector< RowID > & row_ids() noexcept
```

Acceso mutable a los localizadores.

### `row_ids`

```cpp
const std::vector< RowID > & row_ids() const noexcept
```

Acceso de solo lectura a los localizadores.

### `children`

```cpp
std::vector< BTreeNode * > & children() noexcept
```

Acceso mutable a los hijos.

### `children`

```cpp
const std::vector< BTreeNode * > & children() const noexcept
```

Acceso de solo lectura a los hijos.

## Funciones estaticas privadas

### `validate_degree`

```cpp
int validate_degree(int degree)
```

Valida el grado minimo recibido.

**Parametros**

- `degree` - Grado propuesto.

**Devuelve** - El mismo grado si es valido.

> **Nota:** Lanza `std::invalid_argument` si `degree < 2`.
