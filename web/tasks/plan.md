# Plan de Refinamiento: Márgenes, Superficies Mate y Micro-interacciones

## 1. Orden de Ejecución
1. **Fase 1: Geometría y Márgenes en `PageSlab.svelte`**
   - Ajustar `GAP = 9` (separación entre tuplas).
   - Ajustar `TUPLE_L = 176` y `TUPLE_W = 196`.
   - Ajustar `SLOT_H = 18` con ranuras de `height: 14px` (4px de separación).
   - Mover `.gauge` a `right: 18px`, logrando 22px de margen con respecto a las tuplas.
2. **Fase 2: Limpieza de Degradados y Superficies Mate**
   - Sustituir en `app.css` los radial gradients de `body` por un fondo mate sobrio y limpio.
   - En `PageSlab.svelte`, simplificar fondos de losa, placa, tuplas y ranuras a superficies oscuras sólidas de alto contraste.
   - Implementar el *dot grid* estático en `.cavity`.
3. **Fase 3: Empty State en `HexByteViewer.svelte` y Micro-interacciones**
   - Añadir icono y layout amigable cuando no hay tupla seleccionada.
   - Afinar animaciones de hover `var(--ease-spring)` reactivas al usuario.
4. **Fase 4: Verificación Integral**
   - Ejecutar `bun run check:all` y `bun run build`.
