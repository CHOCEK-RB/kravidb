[<- Indice](index.md)

# storage.tuple

**Modulo** - `src/storage/tuple.cppm` (linea 4)

Serializacion binaria de tuplas (`Tuple`).

**Tipos declarados:**

- [kravidb::storage::Tuple](kravidb.storage.Tuple.md)

## Enumeraciones

### `FieldType`

```cpp
enum class FieldType : std::uint8_t
```

Tipo de un campo dentro de una tupla serializada.

- `Int` (= 0) - Entero de 64 bits con signo.
- `Varchar` (= 1) - Cadena de longitud variable (prefijo `u32` de longitud mas bytes).

## Alias de tipo

### `Field`

```cpp
using Field = std::variant<std::int64_t, std::string>
```

Valor de un campo: entero de 64 bits o cadena.
