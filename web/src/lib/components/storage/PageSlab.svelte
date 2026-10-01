<script lang="ts">
  import type { SlottedPageData } from '$lib/api/types'
  import type { InsertMark } from '$lib/stores/engine.svelte'
  import { fly } from 'svelte/transition'
  import { cubicOut } from 'svelte/easing'

  interface Props {
    page: SlottedPageData
    focused?: boolean
    selectedSlotId?: number | null
    lastInsert?: InsertMark | null
    strained?: boolean
    highlightedKey?: number | null
    located?: boolean
    locatedSlotId?: number | null
    onSelectSlot: (pageId: number, slotId: number) => void
    onFocusPage: (pageId: number) => void
  }

  let {
    page,
    focused = false,
    selectedSlotId = null,
    lastInsert = null,
    strained = false,
    highlightedKey = null,
    located = false,
    locatedSlotId = null,
    onSelectSlot,
    onFocusPage,
  }: Props = $props()

  // Lienzo físico de la losa (px). Metáfora gráfica de slotted page de 4 KB.
  const W = 500
  const H = 780
  const HEADER_H = 74

  // Carril izquierdo: Directorio de ranuras (fijo, estable)
  const SLOT_L = 16
  const SLOT_W = 148
  const SLOT_H = 26
  const SLOT_HEADER_H = 26
  const SLOTS_TOP = 86

  // Carril derecho: Área de datos (espacio libre arriba, tuplas abajo)
  const TUPLE_L = 188
  const TUPLE_W = 270
  const BASE = 20
  const GAP = 10
  const PAGE_SIZE = 4096

  const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v))

  // Identidad cromática de la losa: el tono identifica la página, mientras que
  // menta (puntero), oro (clave promovida), rojo (saturación) siguen siendo
  // colores semánticos fijos.
  const PALETTE = [
    { c: '#ff7ebb', hi: '#ffd4e8', lo: '#9e3668' }, // sakura pink
    { c: '#a78bfa', hi: '#ebd5ff', lo: '#603eb5' }, // lavanda mist
    { c: '#38bdf8', hi: '#bae6fd', lo: '#0369a1' }, // sky candy
    { c: '#fb923c', hi: '#fed7aa', lo: '#9a3412' }, // peach blossom
    { c: '#48e5c2', hi: '#c2ffef', lo: '#1b7a65' }, // mint candy
  ]
  const accent = $derived(PALETTE[(page.page_id - 1) % PALETTE.length])

  const usedBytes = $derived(Math.max(0, PAGE_SIZE - page.free_bytes))
  const fill = $derived(clamp(usedBytes / PAGE_SIZE, 0, 1))
  const fillPct = $derived(Math.round(fill * 100))

  const ramp = $derived(fill > 0.82 ? 'var(--hot)' : fill > 0.6 ? 'var(--warn)' : 'var(--ok)')

  type Row = {
    slotId: number
    offset: number
    key: number
    size: number
    i: number
    vh: number
    bottom: number
    top: number
  }

  // Altura máxima reservada para la pila de tuplas (garantiza espacio libre arriba)
  const MAX_TUPLE_STACK_H = H - SLOTS_TOP - BASE - 48

  const rows = $derived.by<Row[]>(() => {
    const count = page.slots.length
    if (count === 0) return []

    // 1. Alturas deseadas sin escalar
    const rawItems = page.slots.map((slot, i) => {
      const tuple = page.tuples.find((t) => t.slot_id === slot.slot_id) ?? null
      const size = tuple?.size_bytes ?? 0
      const vh = clamp(Math.round(size * 0.15), 36, 60)
      return { slot, tuple, size, i, vh }
    })

    const rawTotalH = rawItems.reduce((acc, it) => acc + it.vh, 0) + (count - 1) * GAP

    // 2. Si la suma excede el espacio disponible, escalar suavemente
    const scale = rawTotalH > MAX_TUPLE_STACK_H ? MAX_TUPLE_STACK_H / rawTotalH : 1
    const effGap = Math.max(5, Math.round(GAP * scale))

    let y = BASE
    return rawItems.map(({ slot, tuple, size, i, vh: rawVh }) => {
      const vh = Math.max(26, Math.round(rawVh * scale))
      const bottom = y
      y += vh + effGap
      return {
        slotId: slot.slot_id,
        offset: slot.offset,
        key: tuple?.key ?? 0,
        size,
        i,
        vh,
        bottom,
        top: bottom + vh,
      }
    })
  })

  // La cima visual de las tuplas en el carril derecho se mide desde H.
  const highestTop = $derived(rows.length ? H - rows[rows.length - 1].top : H - 20)
  const cavityTop = SLOTS_TOP
  // La cavidad libre vive exclusivamente sobre las tuplas y jamás se extiende hacia abajo sobre ellas
  const cavityH = $derived(Math.max(0, highestTop - cavityTop - GAP))

  // El puntero sólo vive en la losa enfocada: si no, las dos páginas
  // resaltarían la misma ranura (mismo índice de slot en ambas).
  const activeRow = $derived(
    focused ? (rows.find((r) => r.slotId === selectedSlotId) ?? null) : null,
  )

  /** Tupla señalada por un hallazgo del Index Scan (RowID → losa física). */
  const locatedRow = $derived(
    located && locatedSlotId !== null
      ? (rows.find((r) => r.slotId === locatedSlotId) ?? null)
      : null,
  )

  const beam = $derived.by(() => {
    if (!activeRow) return null
    const ax = SLOT_L + SLOT_W
    const ay = SLOTS_TOP + SLOT_HEADER_H + activeRow.i * SLOT_H + (SLOT_H - 4) / 2
    const bx = TUPLE_L
    const by = H - activeRow.bottom - activeRow.vh / 2
    const cx = (ax + bx) / 2
    return `M ${ax} ${ay} C ${cx} ${ay}, ${cx} ${by}, ${bx} ${by}`
  })

  const isFresh = (slotId: number) =>
    lastInsert !== null && lastInsert.pageId === page.page_id && lastInsert.slotId === slotId

  const hex = (n: number) => `0x${n.toString(16).toUpperCase().padStart(4, '0')}`
</script>

<article
  class="slab"
  class:focused
  class:strained
  data-page={page.page_id}
  style="--ramp: {ramp}; --accent: {accent.c}; --accent-hi: {accent.hi}; --accent-lo: {accent.lo}; width: {W}px; height: {H}px"
  in:fly={{ x: -22, duration: 460, easing: cubicOut }}
  aria-label="Página física {page.page_id}"
>
  <!-- Cabecera grabada con métricas completas sin solapes -->
  <button
    type="button"
    class="plate"
    style="height: {HEADER_H}px"
    onclick={() => onFocusPage(page.page_id)}
    aria-label="Enfocar página {page.page_id}"
  >
    <div class="plate-left">
      <span class="plate-dot"></span>
      <span class="plate-label">PÁGINA</span>
      <span class="plate-id">#{page.page_id}</span>
    </div>
    <div class="plate-right">
      <span class="plate-stat"
        ><em class="plate-label">LSN</em><b class="plate-val">{page.header.lsn}</b></span
      >
      <span class="plate-stat"
        ><em class="plate-label">SLOTS</em><b class="plate-val">{page.header.slot_count}</b></span
      >
      <span class="plate-stat"
        ><em class="plate-label">USO</em><b class="plate-val" style="color: {ramp}">{fillPct}%</b
        ></span
      >
    </div>
  </button>

  <!-- Directorio de Ranuras (carril izquierdo fijo y estable) -->
  <div class="slots-deck" style="left: {SLOT_L}px; top: {SLOTS_TOP}px; width: {SLOT_W}px">
    <header class="slots-head">
      <span class="slots-title">RANURAS</span>
      <span class="slots-badge">{page.slots.length}</span>
    </header>
    <div class="slots-list">
      {#each rows as row (row.slotId)}
        <button
          type="button"
          class="slot-notch"
          class:on={focused && row.slotId === selectedSlotId}
          class:fresh={isFresh(row.slotId)}
          style="top: {row.i * SLOT_H}px; height: {SLOT_H - 4}px"
          onclick={(e) => {
            e.stopPropagation()
            onSelectSlot(page.page_id, row.slotId)
          }}
          aria-label="Slot {row.slotId}, clave {row.key}, offset {hex(row.offset)}"
        >
          <span class="sidx">S{row.slotId}</span>
          <span class="soff">{hex(row.offset)}</span>
        </button>
      {/each}
      {#if rows.length === 0}
        <span class="slots-empty">Sin ranuras</span>
      {/if}
    </div>
  </div>

  <!-- Cavidad libre (carril derecho de datos, sobre las tuplas) -->
  {#if cavityH >= 46}
    <div
      class="cavity"
      style="left: {TUPLE_L}px; width: {TUPLE_W}px; top: {cavityTop}px; height: {cavityH}px"
      aria-hidden="true"
    >
      {#if !strained}
        <span class="cavity-label">
          <em>espacio libre</em>
          <b>{page.free_bytes} B</b>
        </span>
      {/if}
    </div>
  {:else if cavityH >= 22}
    <div
      class="cavity compact"
      style="left: {TUPLE_L}px; width: {TUPLE_W}px; top: {cavityTop}px; height: {cavityH}px"
      aria-hidden="true"
    >
      {#if !strained}
        <span class="cavity-label compact">
          <b>{page.free_bytes} B</b> <em>libres</em>
        </span>
      {/if}
    </div>
  {/if}

  <!-- Tuplas (carril derecho de datos, apiladas desde la base) -->
  <div class="tuples">
    {#each rows as row (row.slotId)}
      <button
        type="button"
        class="tuple"
        class:on={focused && row.slotId === selectedSlotId}
        class:fresh={isFresh(row.slotId)}
        class:hot={highlightedKey !== null && highlightedKey === row.key}
        style="bottom: {row.bottom}px; height: {row.vh}px; left: {TUPLE_L}px; width: {TUPLE_W}px"
        onclick={(e) => {
          e.stopPropagation()
          onSelectSlot(page.page_id, row.slotId)
        }}
        aria-label="Tupla clave {row.key}, {row.size} bytes"
      >
        <span class="accent"></span>
        <span class="tkey">{row.key}</span>
        <span class="tmeta">{row.size}B</span>
      </button>
    {/each}
  </div>

  <!-- Marca de localización: RowID hallado por el Index Scan -->
  {#if locatedRow}
    {#key locatedSlotId}
      <div
        class="locate"
        style="top: {H -
          locatedRow.top}px; left: {TUPLE_L}px; width: {TUPLE_W}px; height: {locatedRow.vh}px"
        aria-hidden="true"
      >
        <span class="locate-frame"></span>
        <span class="locate-ping"></span>
        <span class="locate-tag">P{page.page_id}:S{locatedSlotId}</span>
      </div>
    {/key}
  {/if}

  <!-- Haz puntero: ranura → tupla -->
  <svg class="beam-layer" viewBox="0 0 {W} {H}" aria-hidden="true">
    {#if beam}
      {#key activeRow?.slotId}
        <path class="beam-halo" d={beam} pathLength="1" fill="none" />
        <path class="beam-core" d={beam} pathLength="1" fill="none" />
        <circle
          class="beam-node"
          cx={SLOT_L + SLOT_W}
          cy={SLOTS_TOP + SLOT_HEADER_H + (activeRow?.i ?? 0) * SLOT_H + (SLOT_H - 4) / 2}
          r="4"
        />
        <circle
          class="beam-node"
          cx={TUPLE_L}
          cy={H - (activeRow?.bottom ?? 0) - (activeRow?.vh ?? 0) / 2}
          r="4.5"
        />
      {/key}
    {/if}
  </svg>

  <!-- Sello de alerta de saturación / división -->
  {#if strained}
    <div class="stamp-alert" aria-hidden="true">
      <span class="stamp-pill">PÁGINA SATURADA</span>
      <span class="stamp-main">DIVISIÓN · SPLIT</span>
    </div>
  {/if}

  <!-- Medidor vertical de capacidad -->
  <div class="gauge" aria-hidden="true" title="Llenado: {fillPct}% de 4 KB">
    <span class="gauge-track"></span>
    <span class="gauge-fill" style="height: {fill * 100}%"></span>
    {#each [25, 50, 75] as t (t)}
      <span class="gauge-tick" style="bottom: {t}%"></span>
    {/each}
  </div>
</article>

<style>
  .slab {
    position: relative;
    flex: 0 0 auto;
    border-radius: 16px;
    border: 1px solid var(--line);
    background: #141122;
    box-shadow:
      inset 0 1px 0 rgba(255, 255, 255, 0.04),
      0 20px 40px -20px rgba(0, 0, 0, 0.8);
    overflow: hidden;
    cursor: pointer;
    transition:
      transform 240ms var(--ease-spring),
      border-color 200ms ease,
      box-shadow 200ms ease;
  }
  .slab::after {
    content: '';
    position: absolute;
    inset: 0;
    border-radius: 16px;
    pointer-events: none;
    box-shadow: inset 0 0 30px -20px var(--accent);
    opacity: 0.2;
    transition: box-shadow 400ms var(--ease-out);
  }
  .slab:hover {
    transform: translateY(-3px);
    border-color: color-mix(in srgb, var(--accent) 45%, var(--line-2));
  }
  .slab.focused {
    border-color: var(--accent);
    box-shadow:
      inset 0 1px 0 rgba(255, 255, 255, 0.08),
      0 0 0 1px color-mix(in srgb, var(--accent) 30%, transparent),
      0 24px 48px -20px rgba(0, 0, 0, 0.85);
  }

  .slab.strained {
    animation: strain 720ms var(--ease-out);
  }
  @keyframes strain {
    0% {
      transform: translateX(0);
    }
    12% {
      transform: translateX(-5px) rotate(-0.5deg);
    }
    30% {
      transform: translateX(5px) rotate(0.5deg);
    }
    48% {
      transform: translateX(-4px);
    }
    66% {
      transform: translateX(3px);
    }
    84% {
      transform: translateX(-2px);
    }
    100% {
      transform: translateX(0);
    }
  }

  /* Cabecera grabada */
  .plate {
    position: absolute;
    inset: 0 0 auto 0;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 18px;
    border: 0;
    border-bottom: 1px solid var(--line);
    background: #18142a;
    box-shadow: inset 0 -1px 0 rgba(0, 0, 0, 0.4);
    overflow: hidden;
    cursor: pointer;
    text-align: left;
    font: inherit;
  }
  .plate:hover {
    background: #1d1833;
  }
  .plate-left {
    display: flex;
    align-items: baseline;
    gap: 9px;
  }
  .plate-dot {
    align-self: center;
    width: 9px;
    height: 9px;
    border-radius: 99px;
    background: var(--accent);
    box-shadow: 0 0 12px 1px color-mix(in srgb, var(--accent) 75%, transparent);
    animation: dot-beat 2.6s ease-in-out infinite;
  }
  @keyframes dot-beat {
    0%,
    100% {
      opacity: 1;
      transform: scale(1);
    }
    50% {
      opacity: 0.55;
      transform: scale(0.82);
    }
  }
  .plate-label {
    font-family: var(--font-mono);
    font-size: 11.5px;
    letter-spacing: 0.2em;
    color: color-mix(in srgb, var(--accent) 75%, #a193c2);
  }
  .plate-id {
    font-family: var(--font-mono);
    font-size: 32px;
    font-weight: 800;
    color: var(--accent-hi);
    text-shadow: 0 1px 0 rgba(0, 0, 0, 0.7);
  }
  .plate-right {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .plate-stat {
    display: flex;
    align-items: baseline;
    gap: 5px;
  }
  .plate-val {
    font-family: var(--font-mono);
    font-size: 14.5px;
    font-weight: 700;
    color: var(--ink-dim);
    text-align: right;
  }

  /* Directorio de Ranuras (carril izquierdo fijo y ordenado) */
  .slots-deck {
    position: absolute;
    display: flex;
    flex-direction: column;
    z-index: 2;
  }
  .slots-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    height: 22px;
    margin-bottom: 4px;
    padding: 0 4px;
  }
  .slots-title {
    font-family: var(--font-mono);
    font-size: 10.5px;
    font-weight: 700;
    letter-spacing: 0.16em;
    color: var(--ink-mute);
  }
  .slots-badge {
    font-family: var(--font-mono);
    font-size: 10.5px;
    font-weight: 700;
    padding: 1px 6px;
    border-radius: 99px;
    background: #1c1830;
    color: var(--accent-hi);
    border: 1px solid var(--line);
  }
  .slots-list {
    position: relative;
  }
  .slots-empty {
    display: block;
    padding: 14px 6px;
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--ink-mute);
  }

  .slot-notch {
    position: absolute;
    left: 0;
    width: 100%;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 9px;
    border: 1px solid var(--line);
    border-left: 2.5px solid var(--accent);
    border-radius: 5px;
    background: #171329;
    color: var(--ink-mute);
    cursor: pointer;
    font-family: var(--font-mono);
    transition:
      background 150ms ease,
      color 150ms ease,
      border-color 150ms ease,
      transform 200ms var(--ease-spring);
  }
  .slot-notch:hover {
    color: var(--ink);
    background: #211b3b;
    transform: translateX(3px);
  }
  .slot-notch.on {
    color: var(--mint-hi);
    border-left-color: var(--mint);
    background: #152924;
    border-color: color-mix(in srgb, var(--mint) 40%, var(--line));
  }
  .slot-notch.fresh {
    animation: notch-in 420ms var(--ease-spring) both;
  }
  @keyframes notch-in {
    0% {
      opacity: 0;
      transform: translateX(-14px) scaleX(0.6);
    }
    100% {
      opacity: 1;
      transform: translateX(0) scaleX(1);
    }
  }
  .sidx {
    font-size: 11.5px;
    font-weight: 700;
    letter-spacing: 0.06em;
  }
  .soff {
    font-size: 10.5px;
    opacity: 0.85;
  }

  /* Cavidad libre (carril derecho, sobre las tuplas) */
  .cavity {
    position: absolute;
    z-index: 1;
    border: 1px dashed var(--line-2);
    border-radius: 10px;
    background-color: #0f0d1a;
    background-image: radial-gradient(circle, rgba(255, 255, 255, 0.07) 1px, transparent 1px);
    background-size: 14px 14px;
    box-shadow: inset 0 2px 8px rgba(0, 0, 0, 0.6);
    overflow: hidden;
    transition:
      height 420ms var(--ease-out),
      top 420ms var(--ease-out);
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .cavity-label {
    position: relative;
    display: grid;
    justify-items: center;
    gap: 6px;
    padding: 8px 16px;
    border-radius: 9px;
    background: rgba(16, 13, 28, 0.85);
    backdrop-filter: blur(4px);
    border: 1px solid var(--line);
    font-family: var(--font-mono);
    font-weight: 700;
    letter-spacing: 0.06em;
    color: var(--ink-dim);
    white-space: nowrap;
  }
  .cavity-label b {
    font-size: 19px;
    color: var(--accent-hi);
  }
  .cavity-label em {
    font-size: 11px;
    font-weight: 500;
    font-style: normal;
    letter-spacing: 0.2em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .cavity.compact {
    border-style: dotted;
  }
  .cavity-label.compact {
    display: flex;
    flex-direction: row;
    align-items: baseline;
    gap: 6px;
    padding: 3px 10px;
    border-radius: 6px;
  }
  .cavity-label.compact b {
    font-size: 13px;
  }
  .cavity-label.compact em {
    font-size: 9.5px;
    letter-spacing: 0.12em;
  }

  /* Tuplas */
  .tuples {
    position: absolute;
    inset: 0;
    pointer-events: none;
    z-index: 2;
  }
  .tuple {
    position: absolute;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 0 16px;
    border: 1px solid var(--line);
    border-radius: 10px;
    background: #19152e;
    z-index: 2;
    box-shadow: 0 4px 10px -4px rgba(0, 0, 0, 0.5);
    color: var(--ink-dim);
    text-align: left;
    cursor: pointer;
    pointer-events: auto;
    overflow: hidden;
    transition:
      transform 200ms var(--ease-spring),
      border-color 180ms ease,
      background 180ms ease,
      color 180ms ease;
  }
  .tuple:hover {
    transform: translateY(-2px);
    background: #231d3d;
    border-color: color-mix(in srgb, var(--accent) 50%, var(--line-2));
    color: var(--ink);
  }
  .tuple .accent {
    position: absolute;
    left: 0;
    top: 0;
    bottom: 0;
    width: 4px;
    background: var(--accent);
    transition: background 200ms ease;
  }
  .tuple.on {
    background: #152924;
    border-color: var(--mint);
    color: var(--mint-hi);
  }
  .tuple.on .accent {
    background: var(--mint);
  }
  .tuple.hot {
    border-color: var(--gold);
    color: var(--gold);
  }
  .tuple.hot .accent {
    background: var(--gold);
  }
  .tuple.fresh {
    animation: drop 620ms var(--ease-out) both;
  }
  @keyframes drop {
    0% {
      opacity: 0;
      transform: translateY(-26px) scaleY(0.72);
    }
    55% {
      opacity: 1;
      transform: translateY(2px) scaleY(1.04);
    }
    78% {
      transform: translateY(-1px) scaleY(0.98);
    }
    100% {
      opacity: 1;
      transform: translateY(0) scaleY(1);
    }
  }
  .tkey {
    font-family: var(--font-mono);
    font-size: 20px;
    font-weight: 800;
    letter-spacing: 0.02em;
  }
  .tmeta {
    margin-left: auto;
    font-family: var(--font-mono);
    font-size: 11.5px;
    color: var(--ink-mute);
  }

  /* Haz puntero */
  .beam-layer {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
  }
  .beam-halo,
  .beam-core {
    stroke-linecap: round;
    stroke-dasharray: 1;
    stroke-dashoffset: 1;
    animation: draw 520ms var(--ease-out) forwards;
  }
  .beam-halo {
    stroke: color-mix(in srgb, var(--mint) 45%, transparent);
    stroke-width: 6;
    filter: blur(3px);
  }
  .beam-core {
    stroke: var(--mint-hi);
    stroke-width: 1.6;
  }
  @keyframes draw {
    to {
      stroke-dashoffset: 0;
    }
  }
  .beam-node {
    fill: var(--mint);
    filter: drop-shadow(0 0 5px var(--mint));
    animation: node-pulse 1.8s ease-in-out infinite;
  }
  @keyframes node-pulse {
    0%,
    100% {
      opacity: 1;
    }
    50% {
      opacity: 0.35;
    }
  }

  /* Sello de saturación / SPLIT */
  .stamp-alert {
    position: absolute;
    top: 92px;
    left: 198px;
    width: 250px;
    z-index: 30;
    pointer-events: none;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 4px;
    padding: 10px 14px;
    border: 2px solid var(--ember);
    border-radius: 8px;
    background: rgba(26, 10, 18, 0.96);
    backdrop-filter: blur(8px);
    box-shadow:
      0 0 24px -2px color-mix(in srgb, var(--ember) 75%, transparent),
      0 8px 22px rgba(0, 0, 0, 0.85);
    transform: rotate(-4deg);
    animation: stamp-slam 720ms var(--ease-spring) both;
  }
  .stamp-pill {
    font-family: var(--font-mono);
    font-size: 9.5px;
    font-weight: 700;
    letter-spacing: 0.18em;
    color: var(--ember);
    text-transform: uppercase;
    opacity: 0.95;
  }
  .stamp-main {
    font-family: var(--font-mono);
    font-size: 13px;
    font-weight: 900;
    letter-spacing: 0.22em;
    color: #fff;
    text-shadow: 0 0 10px var(--ember);
  }
  @keyframes stamp-slam {
    0% {
      opacity: 0;
      transform: rotate(-8deg) scale(2.4);
    }
    60% {
      opacity: 1;
      transform: rotate(-4deg) scale(0.95);
    }
    100% {
      transform: rotate(-4deg) scale(1);
    }
  }

  /* Medidor de capacidad vertical */
  .gauge {
    position: absolute;
    top: 86px;
    bottom: 20px;
    right: 14px;
    width: 8px;
    border-radius: 99px;
    background: #100d1e;
    box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.7);
    overflow: hidden;
  }
  .gauge-track {
    position: absolute;
    inset: 0;
    background: repeating-linear-gradient(
      0deg,
      rgba(255, 255, 255, 0.05) 0 1px,
      transparent 1px 8px
    );
  }
  .gauge-fill {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    background: var(--ramp);
    box-shadow: 0 0 10px -2px var(--ramp);
    transition:
      height 560ms var(--ease-out),
      background 400ms ease;
  }
  .gauge-tick {
    position: absolute;
    left: 0;
    width: 100%;
    height: 1px;
    background: rgba(0, 0, 0, 0.6);
  }
  .locate {
    position: absolute;
    z-index: 4;
    pointer-events: none;
  }
  .locate-frame {
    position: absolute;
    inset: -4px;
    border: 2px solid var(--mint);
    border-radius: 12px;
    box-shadow: 0 0 18px -3px var(--mint);
  }
  .locate-ping {
    position: absolute;
    inset: -4px;
    border: 2px solid var(--mint);
    border-radius: 12px;
    animation: locate-ping 1100ms var(--ease-out) forwards;
  }
  @keyframes locate-ping {
    0% {
      transform: scale(1);
      opacity: 0.85;
    }
    100% {
      transform: scale(1.55);
      opacity: 0;
    }
  }
  .locate-tag {
    position: absolute;
    top: -26px;
    right: 0;
    padding: 2px 8px;
    border-radius: 7px;
    border: 1px solid var(--mint);
    background: #162a26;
    color: var(--mint-hi);
    font-family: var(--font-mono);
    font-size: 11px;
    font-weight: 700;
    white-space: nowrap;
  }
</style>
