[<- Indice](index.md)

# kravidb::storage::detail::Page

**Clase** - `src/storage/page_manager.cppm` (linea 18)

Pagina en memoria: ranuras contiguas con su contabilidad de uso.

No se exporta; es un detalle de implementacion de `PageManager`.

## Atributos privados

### `capacity_`

```cpp
std::size_t capacity_
```

Capacidad total de la pagina, en bytes.

### `used_bytes_`

```cpp
std::size_t used_bytes_
```

Bytes consumidos hasta el momento.

### `slots_`

```cpp
std::vector< std::vector< std::byte > > slots_
```

Ranuras almacenadas, cada una con su copia de los bytes.

## Funciones publicas

### `Page`

```cpp
Page(std::size_t capacity)
```

Crea una pagina con la capacidad indicada.

**Parametros**

- `capacity` - Capacidad total de la pagina, en bytes.

### `can_fit`

```cpp
bool can_fit(std::size_t record_size) const noexcept
```

Comprueba si un registro de este tamano cabe en la pagina.

**Parametros**

- `record_size` - Tamano del registro, sin la contabilidad de la ranura.

### `append`

```cpp
SlotID append(std::span< const std::byte > record)
```

Anade un registro al final de la pagina.

**Parametros**

- `record` - Bytes del registro.

**Devuelve** - La ranura asignada al registro.

### `slot`

```cpp
std::optional< std::span< const std::byte > > slot(SlotID slot_id) const noexcept
```

Devuelve los bytes de una ranura.

**Parametros**

- `slot_id` - Ranura consultada.

**Devuelve** - Los bytes almacenados, o `std::nullopt` si la ranura no existe.

### `slot_count`

```cpp
std::size_t slot_count() const noexcept
```

Numero de ranuras ocupadas en la pagina.

### `used_bytes`

```cpp
std::size_t used_bytes() const noexcept
```

Bytes consumidos por registros y contabilidad.
