[<- Indice](index.md)

# storage.row_id

**Modulo** - `src/storage/row_id.cppm` (linea 4)

Localizadores de registro: empaquetado y desempaquetado de `RowID`.

**Tipos declarados:**

- [kravidb::storage::RowLocation](kravidb.storage.RowLocation.md)

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

## Variables

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
