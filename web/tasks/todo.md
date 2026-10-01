# Lista de Tareas: Refinamiento de Márgenes y Acabados Mate

- [x] Tarea 1: Ajustar geometría y márgenes en `PageSlab.svelte`
  - Aceptación: Tuplas con `GAP: 9px`, ranuras con separación visible, ancho de tupla ajustado a 196px y barra de capacidad con más de 20px de margen respecto a las tuplas.
  - Verificación: `bun run build` y visualización sin desbordamientos.
  - Archivos: `src/lib/components/storage/PageSlab.svelte`

- [x] Tarea 2: Limpieza de degradados hacia superficies mate sólidas
  - Aceptación: Fondo global mate y sobrio en `src/app.css`, losas y tuplas en tonos sólidos limpios, y cuadrícula de matriz de puntos (dot grid) estática en la cavidad.
  - Verificación: Inspección de CSS y `bun run check:types`.
  - Archivos: `src/app.css`, `src/lib/components/storage/PageSlab.svelte`

- [x] Tarea 3: Empty state ilustrado en `HexByteViewer.svelte` y micro-interacciones spring
  - Aceptación: Visor de bytes con estado vacío estilizado, micro-interacción de hover elástica bajo demanda en tuplas y ranuras.
  - Verificación: `bun run check:types`.
  - Archivos: `src/lib/components/storage/HexByteViewer.svelte`, `src/lib/components/storage/PageSlab.svelte`

- [x] Tarea 4: Validación integral de calidad
  - Aceptación: `bun run check:all` pasa con 0 errores y 0 warnings.
  - Verificación: `bun run check:all && bun run build`
  - Archivos: Todo el workspace
