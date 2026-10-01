[<- Indice](index.md)

# kravidb::storage

**Namespace** - `src/storage/engine.cppm` (linea 13)

**Tipos declarados:**

- [kravidb::storage::PageIOStats](kravidb.storage.PageIOStats.md)
- [kravidb::storage::PageManager](kravidb.storage.PageManager.md)
- [kravidb::storage::RecordStore](kravidb.storage.RecordStore.md)
- [kravidb::storage::RowLocation](kravidb.storage.RowLocation.md)
- [kravidb::storage::StorageEngine](kravidb.storage.StorageEngine.md)
- [kravidb::storage::Table](kravidb.storage.Table.md)
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

### `PageID`

```cpp
using PageID = std::uint32_t
```

Identificador de pagina (ocupa los 32 bits altos de un `RowID`).

### `SlotID`

```cpp
using SlotID = std::uint32_t
```

Identificador de ranura dentro de una pagina (32 bits bajos de un `RowID`).

### `RowID`

```cpp
using RowID = std::uint64_t
```

Localizador fisico de un registro: pagina y ranura empaquetadas en 64 bits.

### `Field`

```cpp
using Field = std::variant<std::int64_t, std::string>
```

Valor de un campo: entero de 64 bits o cadena.

## Variables

### `default_page_size`

```cpp
std::size_t default_page_size
```

Tamano de pagina por defecto, en bytes (4 KiB).

### `slot_bits`

```cpp
unsigned slot_bits
```

Numero de bits reservados para `slot_id` (los bajos).

### `slot_mask`

```cpp
RowID slot_mask
```

Mascara que aisla los bits bajos correspondientes a `slot_id`.

## Funciones

### `encode_row_id`

```cpp
RowID encode_row_id(RowLocation location) noexcept
```

Empaqueta una ubicacion en un unico `RowID`.

**Parametros**

- `location` - Pagina y ranura a codificar.

**Devuelve** - El `RowID` con la pagina en los 32 bits altos y la ranura en los bajos.

### `decode_row_id`

```cpp
RowLocation decode_row_id(RowID row_id) noexcept
```

Separa un `RowID` en sus componentes de pagina y ranura.

**Parametros**

- `row_id` - Localizador a decodificar.

**Devuelve** - La ubicacion reconstruida a partir del identificador.
