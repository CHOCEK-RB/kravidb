# ADR 0001: PK duplicada y esquema de tabla

- Estado: aceptada
- Fecha: 2026-10-25

## Contexto

El contrato de escritura era ambiguo: el B-Tree almacenaba claves duplicadas y
`Table::insert` no comprobaba la PK. La API escribía directamente en el store y
en el índice, saltándose la tabla. El esquema existía pero no estaba cableado en
`StorageEngine`.

## Decisión

- `Table::insert` es el único punto de escritura y rechaza una PK duplicada con
  `std::invalid_argument` antes de escribir en el record store.
- El B-Tree conserva duplicados estructurales y su contrato lo declara; la
  unicidad de la PK es responsabilidad de `Table`.
- `StorageEngine` recibe un `Schema`; la API usa `(key Int PK, payload Varchar)`.
- `Table::insert` devuelve `InsertResult` con el `RowID` y las estadísticas del
  índice, para que la API no necesite llamar al B-Tree directamente.

## Consecuencias

- Una PK duplicada no crea un segundo registro ni deja huérfanos por validación.
- El store sigue siendo append-only; un fallo del índice después de escribir el
  registro puede dejar un registro sin índice (documentado como `shortcut:`).
- Los llamadores deben usar `Table` y no `records().insert()` ni
  `btree().insert*()`.
