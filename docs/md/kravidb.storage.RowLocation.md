[<- Indice](index.md)

# kravidb::storage::RowLocation

**Struct** - `src/storage/row_id.cppm` (linea 26)

Ubicacion logica de un registro dentro del almacen.

## Atributos publicos

### `page_id`

```cpp
PageID page_id
```

Pagina que contiene el registro.

### `slot_id`

```cpp
SlotID slot_id
```

Ranura ocupada dentro de la pagina.

## Funciones publicas

### `operator==`

```cpp
bool operator==(const RowLocation &) const=default
```

Compara dos ubicaciones campo a campo.
