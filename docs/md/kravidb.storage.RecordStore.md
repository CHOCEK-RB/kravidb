[<- Indice](index.md)

# kravidb::storage::RecordStore

**Clase** - `src/storage/record_store.cppm` (linea 31)

Contrato abstracto de un almacen de registros paginado.

Un registro es una secuencia opaca de bytes a la que el almacen asigna un `RowID` estable. La misma coleccion de bytes se puede recuperar tanto por ese identificador (`read`) como por su ubicacion explicita de pagina y ranura (`slot_at`). La clase no es copiable ni movible: se consume por referencia a traves de la interfaz.

## Funciones publicas

### `RecordStore`

```cpp
RecordStore()=default
```

Construye el contrato, sin estado propio.

### `RecordStore`

```cpp
RecordStore(const RecordStore &)=delete
```

Copia deshabilitada: el contrato se usa por referencia.

### `operator=`

```cpp
RecordStore & operator=(const RecordStore &)=delete
```

Asignacion por copia deshabilitada.

### `RecordStore`

```cpp
RecordStore(RecordStore &&)=delete
```

Movimiento deshabilitado.

### `operator=`

```cpp
RecordStore & operator=(RecordStore &&)=delete
```

Asignacion por movimiento deshabilitada.

### `~RecordStore`

```cpp
~RecordStore()=default
```

Destructor virtual.

### `insert`

```cpp
RowID insert(std::span< const std::byte > record)=0
```

Inserta un registro y devuelve su localizador.

**Parametros**

- `record` - Bytes del registro a almacenar.

**Devuelve** - El `RowID` asignado al registro.

### `read`

```cpp
std::optional< std::span< const std::byte > > read(RowID row_id) const=0
```

Lee un registro por su localizador, contabilizando la E/S.

**Parametros**

- `row_id` - Localizador devuelto previamente por `insert`.

**Devuelve** - Los bytes del registro, o `std::nullopt` si el identificador no existe.

### `slot_at`

```cpp
std::optional< std::span< const std::byte > > slot_at(PageID page_id, SlotID slot_id) const noexcept=0
```

Lee un registro por su ubicacion explicita, sin contabilizar E/S.

**Parametros**

- `page_id` - Pagina objetivo.
- `slot_id` - Ranura dentro de la pagina.

**Devuelve** - Los bytes del registro, o `std::nullopt` si la ubicacion no existe.

### `page_count`

```cpp
std::size_t page_count() const noexcept=0
```

Numero de paginas que componen el almacen.

### `slot_count`

```cpp
std::size_t slot_count(PageID page_id) const noexcept=0
```

Numero de ranuras ocupadas en una pagina.

**Parametros**

- `page_id` - Pagina consultada.

**Devuelve** - La cantidad de ranuras, o cero si la pagina no existe.

### `page_size`

```cpp
std::size_t page_size() const noexcept=0
```

Tamano configurado de pagina, en bytes.

### `stats`

```cpp
const PageIOStats & stats() const noexcept=0
```

Estadisticas acumuladas de E/S.

### `reset_stats`

```cpp
void reset_stats() noexcept=0
```

Pone a cero las estadisticas de E/S.
