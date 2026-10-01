[<- Indice](index.md)

# kravidb::storage::Tuple

**Clase** - `src/storage/tuple.cppm` (linea 58)

Coleccion ordenada de campos con serializacion binaria propia.

El formato serializado es un `u32` con el numero de campos, seguido de cada campo como un `u8` de tipo mas su carga: `Int` ocupa 8 bytes y `Varchar` un `u32` de longitud seguido de los bytes de la cadena.

## Atributos privados

### `fields_`

```cpp
std::vector< Field > fields_
```

Campos de la tupla, en orden.

## Funciones publicas

### `Tuple`

```cpp
Tuple()=default
```

Construye una tupla vacia.

### `Tuple`

```cpp
Tuple(std::vector< Field > fields)
```

Construye una tupla a partir de sus campos.

**Parametros**

- `fields` - Campos en orden.

### `field_count`

```cpp
std::size_t field_count() const noexcept
```

Numero de campos de la tupla.

### `fields`

```cpp
const std::vector< Field > & fields() const noexcept
```

Acceso de solo lectura a los campos, en orden.

### `serialized_size`

```cpp
std::size_t serialized_size() const noexcept
```

Tamano en bytes de la representacion serializada.

### `serialize`

```cpp
std::vector< std::byte > serialize() const
```

Serializa la tupla a su representacion binaria.

**Devuelve** - Los bytes en el formato descrito en la documentacion de la clase.

### `operator==`

```cpp
bool operator==(const Tuple &) const=default
```

Compara dos tuplas campo a campo.

## Funciones estaticas publicas

### `deserialize`

```cpp
std::optional< Tuple > deserialize(std::span< const std::byte > buffer)
```

Reconstruye una tupla desde su representacion binaria.

**Parametros**

- `buffer` - Bytes previamente producidos por `serialize`.

**Devuelve** - La tupla rehidratada, o `std::nullopt` si el buffer esta truncado o malformado.
