# ADR 0002: salidas generadas fuera del control de versiones

- Estado: aceptada
- Fecha: 2026-10-25

## Contexto

`docs/md` se versionaba y quedaba desactualizado respecto a los módulos reales:
faltaban `storage.schema` y todos los módulos `api.*`. `graphify-out/` también se
regenera en cada actualización y produce diffs grandes sin valor de revisión.

## Decisión

- `docs/md/` y `docs/xml/` se generan bajo demanda con `xmake docs` y se ignoran.
- `graphify-out/` se ignora; es una salida local regenerable.
- Las decisiones que deben persistir viven en `docs/adr/`, no en `.agents/`.
- No se comprueba la documentación generada en CI; CI instalaría Doxygen solo
  para detectar drift de archivos que no se versionan.

## Consecuencias

- Hay que ejecutar `xmake docs` para leer la documentación técnica.
- Un clon no trae `docs/md`; sí trae este ADR y el resto de documentación escrita.
- Si más adelante se quiere publicar la documentación, se genera en un job de
  despliegue en lugar de versionarla.
