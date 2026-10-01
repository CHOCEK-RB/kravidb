[<- Indice](index.md)

# kravidb::storage::PageManager

**Clase** - `src/storage/page_manager.cppm` (linea 79)

Almacen de registros paginado, de crecimiento solo por el final.

Los registros se insertan en la ultima pagina o en una nueva; las paginas nunca se reasignan, por lo que los bytes devueltos por `read` y `slot_at` siguen siendo validos despues de inserciones posteriores.

## Atributos privados

### `page_size_`

```cpp
std::size_t page_size_
```

Tamano de pagina configurado, en bytes.

### `pages_`

```cpp
std::vector< detail::Page > pages_
```

Paginas en orden de creacion.

### `stats_`

```cpp
PageIOStats stats_
```

Contadores de E/S, mutables para poder actualizarlos desde `read` (const).

## Funciones publicas

### `PageManager`

```cpp
PageManager(std::size_t page_size=default_page_size)
```

Crea el almacen con el tamano de pagina indicado.

**Parametros**

- `page_size` - Tamano de pagina, en bytes.

### `insert`

```cpp
RowID insert(std::span< const std::byte > record) override
```

Inserta un registro.

**Parametros**

- `record` - Bytes del registro a almacenar.

**Devuelve** - El `RowID` asignado.

> **Nota:** Lanza `std::length_error` si el registro no cabe ni en una pagina vacia.

### `read`

```cpp
std::optional< std::span< const std::byte > > read(RowID row_id) const override
```

Lee un registro por su localizador, contabilizando la E/S.

**Parametros**

- `row_id` - Localizador del registro.

**Devuelve** - Los bytes del registro, o `std::nullopt` si no existe.

### `slot_at`

```cpp
std::optional< std::span< const std::byte > > slot_at(PageID page_id, SlotID slot_id) const noexcept override
```

Lee un registro por ubicacion explicita, sin contabilizar E/S.

**Parametros**

- `page_id` - Pagina objetivo.
- `slot_id` - Ranura dentro de la pagina.

**Devuelve** - Los bytes del registro, o `std::nullopt` si la ubicacion no existe.

### `page_count`

```cpp
std::size_t page_count() const noexcept override
```

Numero de paginas del almacen.

### `slot_count`

```cpp
std::size_t slot_count(PageID page_id) const noexcept override
```

Numero de ranuras ocupadas en una pagina.

**Parametros**

- `page_id` - Pagina consultada.

### `page_size`

```cpp
std::size_t page_size() const noexcept override
```

Tamano de pagina configurado, en bytes.

### `stats`

```cpp
const PageIOStats &override stats() const noexcept
```

Estadisticas acumuladas de E/S.

### `reset_stats`

```cpp
void reset_stats() noexcept override
```

Pone a cero las estadisticas de E/S.
