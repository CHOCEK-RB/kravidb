[<- Indice](index.md)

# kravidb::storage::Table

**Clase** - `src/storage/table.cppm` (linea 18)

Tabla que persiste tuplas y las indexa por clave primaria.

El primer campo de cada tupla debe ser un entero de 64 bits y actua como clave primaria (PK). La tabla no posee el almacen ni el indice: los recibe por referencia en la construccion.

## Atributos privados

### `records_`

```cpp
RecordStore & records_
```

Almacen de registros (referencia no propietaria).

### `index_`

```cpp
index::Index & index_
```

Indice de claves primarias (referencia no propietaria).

## Funciones publicas

### `Table`

```cpp
Table(RecordStore &records, index::Index &index)
```

Construye la tabla sobre un almacen y un indice existentes.

**Parametros**

- `records` - Almacen donde se persisten las tuplas (no se posee).
- `index` - Indice que asocia la PK con el `RowID` (no se posee).

### `insert`

```cpp
void insert(const Tuple &tuple)
```

Inserta una tupla y la indexa por su clave primaria.

**Parametros**

- `tuple` - Tupla a insertar; el primer campo es la PK.

> **Nota:** Lanza `std::invalid_argument` si la tupla esta vacia o si el primer campo no es de tipo `Int`.

### `find_by_index`

```cpp
std::optional< Tuple > find_by_index(index::Key key) const
```

Busca una tupla por su clave primaria.

**Parametros**

- `key` - Clave primaria buscada.

**Devuelve** - La tupla encontrada, o `std::nullopt` si no existe.

### `scan_all`

```cpp
std::vector< Tuple > scan_all() const
```

Recorre y deserializa todas las tuplas del almacen.

**Devuelve** - Las tuplas encontradas, en orden de pagina y ranura.

> **Nota:** Usa `slot_at`, de modo que no incrementa las estadisticas de E/S.
