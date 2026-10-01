# AGENT.md — kravidb web (visualizador SLAB)

Documentación del diseño y de cada pieza del frontend. Léelo antes de tocar
`web/`. Si algo de aquí contradice al código, gana el código: corrige este archivo.

## 0. Estado del proyecto

- `web/` es un **frontend de visualización** para el motor `kravidb`.
- El backend C++ **es un stub**: `src/main.cpp` y `src/storage/engine.cppm` sólo
  imprimen la versión. No hay B-Tree, ni páginas ranuradas, ni servidor HTTP.
- Toda la lógica que se ve corre sobre un **mock en TypeScript**
  (`src/lib/api/mock.ts`). El contrato (`src/lib/api/types.ts`) es el que deberá
  cumplir el C++ real.
- **No se toca** `src/lib/api/types.ts`, ni `client.ts`, ni `mock.ts` para
  "arreglar" la vista: el mock es la fuente de verdad de los datos.
- En git, `web/` está **sin trackear** (sólo existe `README.md` raíz).

## 1. Ejecutar y verificar

```bash
cd web
bun install
bun run dev        # http://127.0.0.1:5173
bun run check:all  # format + lint:biome + eslint + svelte-check
bun run build      # vite build -> dist/
bun run check:knip # exports y dependencias sin uso
```

- Gates obligatorios antes de dar algo por terminado: `check:all` (0 errores y
  **0 warnings**: `svelte-check --threshold warning`) y `build`.
- `check:knip` debe salir sin hallazgos; los 4 "configuration hints"
  (`tailwindcss` y `@types/*` en `ignoreDependencies`, entry `src/main.ts`
  redundante, extensión `.css`) son informativos y no fallan.
- Al editar un `.svelte` con el dev server vivo, **`touch` el archivo**: vite
  puede servir un transform cacheado y verás el cambio a medias. Después recarga
  la pestaña.

## 2. Contrato de datos (`src/lib/api/`)

`types.ts` es la frontera. Resumen:

| Tipo | Campos clave |
| --- | --- |
| `BTreeKey` | `key`, `value`, `page_id`, `slot_id` |
| `BTreeNode` | `id`, `keys[]`, `children[]`, `is_leaf` |
| `PageHeader` | `page_id`, `lsn`, `slot_count`, `free_space_offset`, `free_space_end` |
| `PageSlot` | `slot_id`, `offset`, `length`, `is_deleted` |
| `PageTuple` | `slot_id`, `offset`, `key`, `data`, `size_bytes` |
| `SlottedPageData` | `page_id`, `header`, `slots[]`, `tuples[]`, `total_bytes`, `free_bytes` |
| `SearchMetrics` | `key`, `found`, `target?`, `path[]`, `index_scan{latency_ns,page_ios,nodes_visited}`, `full_scan{latency_ns,page_ios,tuples_scanned}` |
| `InsertResult` | `split_occurred`, `promoted_key?`, `new_root_created`, `target_page_id`, `target_slot_id` |

`client.ts`: `HttpApiClient` (base `http://localhost:8080`, endpoints
`/api/v1/btree`, `/api/v1/btree/insert`, `/api/v1/btree/search?key=`,
`/api/v1/page/:id`, `/api/v1/reset`). El export es:

```ts
export const api: ApiClient = useMock ? mockClient : new HttpApiClient()
// useMock = import.meta.env.VITE_USE_MOCK !== 'false'  -> mock por defecto
```

### Geometría real del mock (importa para la vista)

- Grado `t = 2` → máximo `2t-1 = 3` claves por nodo; nodo lleno ⇒ split.
- Página de **4096 B**, cabecera fija **64 B**, ranura de **4 B**.
- Tupla: `tupleBytes = 8 + payload.length` (8 B de clave + payload).
- Se necesita `4 + tupleBytes` libres; si no, el mock **crea la página siguiente**
  (`currentPageId += 1`).
- Semillas: 7 personas (Ada Lovelace 10, Alan Turing 20, Grace Hopper 5,
  Linus Torvalds 15, Dennis Ritchie 25, Ken Thompson 30, Edsger Dijkstra 35)
  con payload de ~300 B ⇒ **~12 tuplas por losa**.
- `insertKey` de una clave existente **actualiza** el registro y devuelve
  `split_occurred: false`.
- `splitChild` anota `lastPromotedKey` y suma `splitCount`; eso es lo que la
  vista lee como "clave promovida" (antes salía `undefined` y se mostraba
  "Mediana undefined": no reintroduzcas heurísticas en el motor de estado).
- `searchKey` fabrica las métricas de forma **analítica**:
  `index_latency = 180 + nodes_visited*90 + rand(40) ns`,
  `full_scan_latency = max(12, round(tuplas*1.8 + páginas*8.5)) µs`.

`utils.ts` → `padPayload(key, raw, target = 300)`: rellena el payload hasta ~300 B
(añade `pad: "payload_0 payload_1 …"`). Existe para que la losa se llene a un
ritmo comparable al de una página real. Es la única utilidad compartida.

## 3. Motor de estado (`src/lib/stores/engine.svelte.ts`)

Singleton `export const engine = new EngineStore()` (runas). Estado reactivo:

- Datos: `tree`, `pages[]`, `currentPage`, `searchMetrics`, `logs[]`.
- Selección: `selectedPageId` (1), `selectedSlotId` (null).
- Coreografía: `isSearching`, `activeStepIndex`, `highlightedNodeId`,
  `highlightedKey`, `lastSplitOccurred`, `promotedKey`, `strainedPageId`,
  `lastInsert` (`InsertMark {pageId, slotId, key, at}`).
- UI: `playbackSpeed` (1), `isLoading`.

Getters derivados del árbol: `pageIds` (recorre el árbol, deduplica, suma la
página seleccionada; **sin `Set`** porque `svelte/prefer-svelte-reactivity` lo
prohíbe), `totalKeys`, `treeDepth`, `treeNodeCount`.

Métodos: `init`, `refreshTree`, `refreshPages` (`Promise.all` de `api.getPage`),
`selectPage(pageId, slotId?)`, `selectSlot(pageId, slotId)` (delega si cambia de
página), `insertKey`, `executeSearch`, `resetEngine`. Privados: `strain(pageId)`
(900 ms de sacudida) y `markInsert(...)` (1400 ms de marca de escritura).

Reglas de la coreografía (no las rompas sin querer):

- `insertKey` captura `previousIds`; si la página destino es nueva y no es la
  anterior máxima, marca `strain(previousMax)` → la losa que **desbordó** es la
  que tiembla, no la nueva.
- `markInsert` es lo que dispara la caída de la tupla y el encendido de la
  ranura recién creada.
- `executeSearch` recorre `metrics.path` con `delay = 350 / playbackSpeed` y
  resalta nodo a nodo; al encontrar, selecciona página + ranura.
- `addLog` guarda **máximo 50** entradas, la más nueva primero.

## 4. Sistema visual

Identidad **SLAB Cute Cyber-Dark**: el disco como un estante de losas de 4 KB con estética
linda y futurista. Fondo terciopelo púrpura oscuro (`#0c0b16`), acentos pastel vibrantes
(Sakura Pink `#ff7ebb`, Lilac Lavender `#c084fc`, Mint Candy `#48e5c2`, Peach `#ffaa80`)
y efectos láser continuos (haz perimetral orbital en losas y escáner neón en la cavidad libre).

Tokens en `src/app.css`: `--bg #0c0b16`, `--bg-2 #141224`, `--panel #18152b`, `--panel-2 #211c3a`,
`--panel-3 #2a234b`, `--line #2e2652`, `--line-2 #413673`, `--line-3 #58499c`, `--ink #f8f6fd`,
`--ink-dim #c5bcdb`, `--ink-mute #8579a3`, `--sakura #ff7ebb`, `--sakura-hi`, `--sakura-lo`,
`--copper` (alias a sakura), `--lavender #c084fc`, `--mint #48e5c2`, `--mint-hi #b8fff0`,
`--peach #ffaa80`, `--gold #fde047`, `--ember #ff6384`, `--ok #4ade80`, `--warn #fde047`,
`--hot #ff6384`, `--font-ui`, `--font-mono`, `--ease-out`, `--ease-spring`.
Sincronizados en `@theme` de Tailwind v4.

Tipografía: **Space Grotesk** para chrome, **JetBrains Mono** para datos. Regla:
si es un dato del motor (clave, offset, byte, latencia) va en mono.

Clases globales reutilizables: `.panel` (borde, radio 16, gradiente, highlight
interior + sombra), `.panel::before` (filo cromático superior que lee `--tint`),
`.panel-title`, `.eyebrow`. Cada panel declara su matiz:
benchmark `--tint: var(--gold)`, lente de bytes `var(--mint)`, bitácora `var(--sakura)`.
El contenedor del árbol **no** es `.panel` y no lleva filo.

Semántica de color (fija, no negociable):

| Color | Significado |
| --- | --- |
| menta `--mint` | puntero / selección activa / acierto de búsqueda |
| oro `--gold` | clave promovida en un split, y panel de métricas |
| ember `--ember` | saturación, split, error |
| rampa capacidad | verde `--ok` < 60 %, oro `--warn` > 60 %, rojo `--hot` > 82 % |
| acento por losa | **identidad de la página** (no semántico) |

Acento por losa (`PageSlab.PALETTE`, rotación por `page_id`):
`sakura → lavanda mist → sky candy → peach blossom → mint candy`.
Tiñe borde láser orbital (`.slab-laser`), glow, placa, ticks de la regla, barrido de la cavidad
y barra de la tupla. Regla de oro: **sólo se anima lo que significa algo o da feedback visual de vida**;
el acento nunca sustituye a menta/oro/rojo.

## 5. Piezas

```
src/
  main.ts                  # mount(App)
  App.svelte               # workspace: deck + telemetría + estante + árbol + side
  app.css                  # tokens, grano, .panel, scrollbars, reduced-motion
  lib/
    api/{types,client,mock}.ts
    stores/engine.svelte.ts
    utils.ts               # padPayload
    components/
      storage/PageSlab.svelte      # ★ la losa de 4 KB (pieza central)
      storage/HexByteViewer.svelte # lente de bytes
      btree/BTreeViewport.svelte   # índice lógico (d3)
      metrics/ScanBenchmark.svelte # índice vs barrido
      workbench/CommandDeck.svelte # barra sticky de operaciones
      workbench/TelemetryBar.svelte
      workbench/EventTerminal.svelte
```

### `App.svelte` — composición

- `emptyPage` de relleno para la lente cuando no hay página.
- Derivados: `currentPage`, `selectedSlot`, `selectedTuple` (por `selectedSlotId`).
- Handlers: `handleSelectKey` (clave del árbol → losa + log), `handleSelectNode`,
  `handleSelectSlot`, `handleFocusPage`, `handleBatchDemo` (inserta secuencial
  `[40,48,62,75,90,12,55,68,82,95,2,44,58,71]` con pausa `560 / playbackSpeed`).
- Layout: `CommandDeck` → `TelemetryBar` → `section.hero` (título "Memoria física",
  cadena `clave → P{page}:S{slot} → bytes`, y `.rack` con las losas + rail) →
  `section.lower` (grid `minmax(0,2.1fr) / minmax(340px,1fr)`; a ≤1100 px pasa a
  1 columna) con árbol a la izquierda y `ScanBenchmark`/`HexByteViewer`/
  `EventTerminal` a la derecha → `footer`.

### `PageSlab.svelte` — la losa (★)

Props: `page`, `focused`, `selectedSlotId`, `lastInsert`, `strained`,
`highlightedKey`, `onSelectSlot(pageId, slotId)`, `onFocusPage(pageId)`.

Constantes (px, **metáfora legible, no escala literal**): `W 420`, `H 900`,
`HEADER_H 70`, `SLOT_TOP 70`, `SLOT_H 15`, `TUPLE_L 186`, `TUPLE_W 220`,
`BASE 18`, `GAP 5`, `PAGE_SIZE 4096`. Altura de tupla:
`vh = clamp(round(size_bytes * 0.145), 30, 56)`.

Anatomía, de arriba a abajo:

1. `button.plate` (cabecera grabada): PÁGINA + `#id`, LSN, SLOTS, punto del
   acento latiendo, brillo `sheen`. Es el botón que enfoca la página.
2. `div.ruler`: regla de direcciones de 4 KB con ticks en
   `0 / 1024 / 2048 / 3072 / 4096`.
3. `div.slots`: ranuras apiladas **hacia abajo** (`S0..Sn` + offset en hex).
4. `div.cavity`: hueco libre rayado, con barrido `sweep` y la etiqueta
   `espacio libre` + `{free_bytes} B`. Su alto se anima al llenarse.
5. `div.tuples`: tuplas apiladas **hacia arriba** (`bottom`), con barra de acento,
   clave en grande y `{size}B`.
6. `svg.beam-layer`: haz bezier ranura→tupla del elemento activo.
7. `div.stamp`: sello **SPLIT** cuando la losa tiembla.
8. `div.gauge` + `.gauge-read`: medidor vertical de capacidad con la rampa.

Detalles que son bugs conocidos si se "simplifican":

- Las filas se apilan desde el fondo (`bottom`), así que la cima visual se mide
  `highestTop = H - rows[last].top` (no `rows[last].top`).
- `activeRow` sólo existe si `focused`. El índice de ranura se repite en cada
  página; si se compara `slotId === selectedSlotId` sin `focused`, **todas** las
  losas resaltan la misma ranura y dibujan el mismo haz.
- Las claves dentro de las tuplas llevan `stopPropagation` para no enfocar la
  página al seleccionar una tupla.
- La cavidad va en `z-index: 0` y `.slots` en `z-index: 1` para que el directorio
  quede por encima del hueco libre.

### `BTreeViewport.svelte` — índice lógico

d3-hierarchy (`tree().nodeSize([188, 134])`) para colocar, d3-zoom para
pan/zoom (`scaleExtent [0.28, 2.6]`), `d3-selection` para aplicar el transform,
`d3-transition` **importado explícitamente** (si no, `.transition()` no tipa).
Props: `treeData`, `highlightedNodeId`, `highlightedKey`, `lastSplitOccurred`,
`promotedKey`, `isSearching`, `onSelectKey`, `onSelectNode`.

- Constantes: `NODE_H 54`, `CELL_W 46`, `CELL_GAP 5`, `X_GAP 188`, `Y_GAP 134`.
- `bbox` + `fitTransform(minScale)` centran el árbol en ambos ejes;
  `home() = fitTransform(0.45)` (auto-encuadre mientras el usuario no toque) y
  `fitAll() = fitTransform(0.28)` (botón *Maximize2*).
- `touched` se activa con el primer evento real (`e.sourceEvent`) o con los
  botones; a partir de ahí el auto-encuadre no vuelve a mover la vista.
- El reflujo animado del árbol sale de `.node { transition: transform 460ms }`.
- Durante `isSearching`, los enlaces llevan `.link.hot` (guiones en movimiento).
- La clave promovida se marca `class:promo` (destello oro, keyframes `promo`).
- Canvas fijo de 560 px de alto; nodos con `role="button"` y `tabindex`, teclado
  con Enter/Espacio.

### `ScanBenchmark.svelte` — índice vs barrido

Props: `metrics`, `onSearchPreset`. Presets `[10, 20, 35, 50, 99]`. Tres `Tween`
de `svelte/motion` (`indexNs` 420 ms, `fullUs` 420 ms, `speedup` 520 ms) para
contar las latencias al aparecer el resultado. `indexPct` es la proporción
`index / (full*1000)`, con un mínimo del 1,5 % para que la barra no desaparezca.
Veredicto verde si coincidió, oro si no.

### `HexByteViewer.svelte` — lente de bytes

Props: `page`, `selectedSlot`, `selectedTuple`. Calcula el LBA físico real
`(page_id - 1) * 4096 + slot.offset` y volca la tupla en filas de 16 bytes
(hex + ASCII, los 8 primeros bytes resaltados). Sin selección muestra un estado
de reposo con instrucciones, nunca un panel vacío.

### `CommandDeck.svelte` — operaciones

Props: `isLoading`, `isSearching`, `playbackSpeed`, `lastSplitOccurred`,
`promotedKey` + callbacks `onInsert`, `onSearch`, `onBatchDemo`, `onReset`,
`onChangeSpeed`, `onDismissSplitAlert`. Formularios `+INS` (clave + payload, el
payload pasa por `padPayload` y luego la clave se aleatoriza) y `SCAN`, botón
`Lote`, velocidades `[0.5, 1, 2]` y reset. El banner `.split` sólo aparece con
`lastSplitOccurred` y es descartable.

### `TelemetryBar.svelte` — cabecera de estado

Props: `totalKeys`, `nodes`, `depth`, `pageCount`, `selectedPageId`, `t = 2`.
Seis lecturas: claves, nodos, prof., losas, grado t, página. Punto "EN LÍNEA"
latiendo. **No tiene pestañas**: el workspace muestra todo a la vez.

### `EventTerminal.svelte` — bitácora

Props: `logs`, `onClearLogs`. Filtros Todo/Splits/Scans/Misses; mapas `TONE` y
`LABEL` (`split→SPLIT`, `match→HIT`, `miss→MISS`, `insert→WRITE`, `search→SCAN`,
`info→INFO`); cada evento entra con `log-in` y un filo del color de su tono.

## 6. Inventario de animaciones

Regla: toda animación tiene disparador, significado y duración. Nada decorativo.

| # | Qué | Disparador | Técnica | Duración |
| --- | --- | --- | --- | --- |
| 1 | Caída y asentamiento de la tupla | `lastInsert` en esa ranura | `@keyframes drop` | 620 ms |
| 2 | Aparición de la ranura | `lastInsert` | `@keyframes notch-in` (spring) | 420 ms |
| 3 | Trazo del haz ranura→tupla | selección activa | `stroke-dashoffset` (`draw`) | 520 ms |
| 4 | Latido de los extremos del haz | siempre | `node-pulse` | 1,8 s ∞ |
| 5 | Encogimiento del hueco libre | cambio de `free_bytes` | `transition` height/top | 520 ms |
| 6 | Barrido del espacio libre | reposo | `sweep` (background-position) | 3,6 s ∞ |
| 7 | Medidor de capacidad | cambio de ocupación | `transition` height | 560 ms |
| 8 | Tensión + sello SPLIT | `strainedPageId` | `strain` (sacudida) + `stamp-in` | 720 ms |
| 9 | Entrada de una losa nueva | `each` con `in:fly` | `fly` x −22 + `cubicOut` | 460 ms |
| 10 | Brillo de la placa | reposo | `sheen` | 6,5 s ∞ |
| 11 | Punto del acento | reposo | `dot-beat` | 2,6 s ∞ |
| 12 | Reflujo del árbol | cambio de layout | `transition: transform` | 460 ms |
| 13 | Haz de búsqueda en el árbol | `isSearching` | `flow` sobre `.link.hot` | 900 ms ∞ |
| 14 | Clave promovida | `lastSplitOccurred` | `promo` (destello oro) | 700 ms |
| 15 | Latencias de métricas | nuevo `metrics` | `Tween` de svelte/motion | 420/520 ms |

Micro-interacción (hover): losa `translateY(-4px)`, tupla `translateX(3px)`,
ranura `translateX(2px)`, presets `translateY(-2px)`, reset gira −90° — todas
entre 160 y 320 ms. Entradas de log 350 ms, banner de split 360 ms.

Global: `@media (prefers-reduced-motion: reduce)` desactiva animaciones y
transiciones. Si añades animación, no la excluyas de ese bloque.

## 7. Decisiones y por qué

- **Estilo en CSS scoped, no en utilidades Tailwind.** El layout es paramétrico
  (posiciones y alturas calculadas en runas y aplicadas con `style`), el color es
  por instancia (`--accent`, `--ramp`) y las animaciones son `@keyframes` con
  overshoot. Tailwind sigue instalado y activo (`@import "tailwindcss"` + `@theme`)
  y es bienvenido para lo estático; lo dinámico vive en CSS del componente. Si se
  migra a utilidades, que sea por componente y sin tocar el API ni las animaciones.
- **Sin pestañas.** Antes había un `activeTab` que ocultaba paneles; el valor del
  producto es ver la cadena `clave → P{page}:S{slot} → bytes` **completa** a la vez.
  No reintroduzcas pestañas que escondan la losa o el árbol.
- **Un acento por losa.** Con varias páginas en pantalla, el color es la forma más
  rápida de decir "esta tupla vive en aquella losa".
- **Sin navegador en el backend.** No hay servidor C++ al que apuntar: el mock es
  el contrato vivo.
- **Escala no literal.** La losa de 900 px no son 4096 B; es una metáfora legible
  con las proporciones suficientes para que el desbordamiento se vea.

## 8. Convenciones

- UI en **español**; identificadores y comentarios de código pueden ser español o
  inglés, pero mantenlos consistentes dentro del archivo.
- Datos siempre en `--font-mono` y con `font-variant-numeric: tabular-nums` cuando
  cambian en vivo (para que no "baile" el ancho).
- Accesibilidad: los controles son `<button>` reales con `aria-label`; los
  contenedores puramente decorativos van `aria-hidden="true"`. El texto dentro de
  un `aria-hidden` no es consultable por herramientas de espera por texto.
- Formato: biome (TS/CSS, comillas simples, sin punto y coma, ancho 100) y
  prettier + `prettier-plugin-svelte` para `.svelte`. `biome.json` necesita
  `css.parser.tailwindDirectives: true` y la excepción a `noImportantStyles` en CSS.
- No dejes código muerto: `knip` y `svelte-check` lo detectan. Los cinco
  componentes antiguos (`ActivityLog`, `BTreeCanvas`, `ControlsHeader`,
  `ScanComparator`, `SlottedPageViewer`) y `SlottedPageMap` ya se borraron; no los
  resucites.

## 9. Pendientes conocidos

- `index.html` conserva clases Tailwind del tema viejo en `<body>`
  (`bg-[#07090e] text-slate-200 …`). El fondo real lo pone `app.css`; esas clases
  sobrescriben el color base y conviene limpiarlas.
- El árbol con ≥13 nodos no cabe entero de forma legible a 45 %: recorta ~15 %
  por la derecha y se resuelve con panorámica o con el botón *Encuadrar todo*. Si
  se quiere encuadre total por defecto, hay que aceptar tipografía menor.
- El `README.md` de `web/` sigue siendo la plantilla genérica de Svelte + Vite.
