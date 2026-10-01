[<- Indice](index.md)

# kravidb::storage::detail

**Namespace** - `src/storage/page_manager.cppm` (linea 10)

**Tipos declarados:**

- [kravidb::storage::detail::Page](kravidb.storage.detail.Page.md)

## Variables

### `slot_overhead`

```cpp
std::size_t slot_overhead
```

Coste de contabilidad por ranura: la longitud `u32` que la precede.

## Funciones

### `write_raw`

```cpp
void write_raw(std::vector< std::byte > &out, T value)
```

Escribe un valor escalar en crudo al final de un buffer.

**Parametros de plantilla**

- `T` - Tipo escalar a escribir.

**Parametros**

- `out` - Buffer de destino.
- `value` - Valor a serializar (se copian sus bytes tal cual).

### `read_raw`

```cpp
std::optional< T > read_raw(std::span< const std::byte > buffer, std::size_t &offset)
```

Lee un valor escalar en crudo desde un buffer.

**Parametros de plantilla**

- `T` - Tipo escalar a leer.

**Parametros**

- `buffer` - Bytes de origen.
- `offset` - Desplazamiento de lectura; avanza en `sizeof(T)`.

**Devuelve** - El valor leido, o `std::nullopt` si no quedan bytes suficientes.
