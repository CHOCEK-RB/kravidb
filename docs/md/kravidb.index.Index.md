[<- Indice](index.md)

# kravidb::index::Index

**Clase** - `src/index/index.cppm` (linea 24)

Contrato abstracto de un indice que asocia clave con `RowID`.

Relaciona claves enteras de 64 bits con localizadores de registro. No es copiable ni movible: se consume por referencia a traves de la interfaz.

## Funciones publicas

### `Index`

```cpp
Index()=default
```

Construye el contrato, sin estado propio.

### `Index`

```cpp
Index(const Index &)=delete
```

Copia deshabilitada: el contrato se usa por referencia.

### `operator=`

```cpp
Index & operator=(const Index &)=delete
```

Asignacion por copia deshabilitada.

### `Index`

```cpp
Index(Index &&)=delete
```

Movimiento deshabilitado.

### `operator=`

```cpp
Index & operator=(Index &&)=delete
```

Asignacion por movimiento deshabilitada.

### `~Index`

```cpp
~Index()=default
```

Destructor virtual.

### `insert`

```cpp
void insert(Key key, RowID row_id)=0
```

Inserta o reemplaza la asociacion de una clave.

**Parametros**

- `key` - Clave a indexar.
- `row_id` - Localizador de registro asociado.

### `search`

```cpp
std::optional< RowID > search(Key key) const=0
```

Busca una clave en el indice.

**Parametros**

- `key` - Clave buscada.

**Devuelve** - El `RowID` asociado, o `std::nullopt` si la clave no existe.

### `empty`

```cpp
bool empty() const noexcept=0
```

Indica si el indice no contiene ninguna entrada.

### `clear`

```cpp
void clear()=0
```

Elimina todas las entradas del indice.

### `last_search_stats`

```cpp
SearchStats last_search_stats() const noexcept=0
```

Estadisticas de la ultima busqueda realizada.

### `reset_search_stats`

```cpp
void reset_search_stats() noexcept=0
```

Pone a cero las estadisticas de busqueda.
