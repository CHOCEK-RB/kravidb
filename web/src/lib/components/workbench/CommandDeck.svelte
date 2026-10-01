<script lang="ts">
  import { Search, Plus, RotateCcw, Play, Gauge, Zap, X, Layers, Network } from '@lucide/svelte'
  import { padPayload } from '$lib/utils'
  import type { BulkState } from '$lib/stores/engine.svelte'

  interface Props {
    isLoading?: boolean
    isSearching?: boolean
    playbackSpeed?: number
    lastSplitOccurred?: boolean
    promotedKey?: number | null
    batch?: { current: number; total: number } | null
    bulk?: BulkState | null
    massMode?: boolean
    degree?: number
    onInsert: (key: number, payload: string) => void | Promise<unknown>
    onSearch: (key: number) => void | Promise<unknown>
    onBatchDemo: () => void | Promise<unknown>
    onBulkLoad: (total: number) => void | Promise<unknown>
    onSetDegree: (degree: number) => void | Promise<unknown>
    onReset: () => void | Promise<unknown>
    onChangeSpeed: (speed: number) => void
    onDismissSplitAlert: () => void
  }

  let {
    isLoading = false,
    isSearching = false,
    playbackSpeed = 1,
    lastSplitOccurred = false,
    promotedKey = null,
    batch = null,
    bulk = null,
    massMode = false,
    degree = 2,
    onInsert,
    onSearch,
    onBatchDemo,
    onBulkLoad,
    onSetDegree,
    onReset,
    onChangeSpeed,
    onDismissSplitAlert,
  }: Props = $props()

  let insertKeyVal = $state<number>(18)
  let insertPayload = $state<string>('{"role":"active"}')
  let searchKeyVal = $state<number>(15)
  let isBatchRunning = $state<boolean>(false)
  let bulkN = $state<number>(10000)
  let degreeOverride = $state<number | null>(null)

  const speeds = [0.5, 1, 2]
  const bulkSizes = [1000, 5000, 10000, 25000, 50000]
  const degreeSizes = [2, 3, 4, 5, 8, 16, 32, 64]

  const bulkRunning = $derived(bulk !== null && !bulk.done)
  const bulkPct = $derived(bulk ? (bulk.current / bulk.total) * 100 : 0)
  const degreeN = $derived(degreeOverride ?? degree)

  async function handleInsertSubmit(e: SubmitEvent) {
    e.preventDefault()
    if (insertKeyVal === undefined || insertKeyVal === null) return
    await onInsert(Number(insertKeyVal), padPayload(Number(insertKeyVal), insertPayload))
    insertKeyVal = Math.floor(Math.random() * 95) + 1
  }

  async function handleSearchSubmit(e: SubmitEvent) {
    e.preventDefault()
    if (searchKeyVal === undefined || searchKeyVal === null) return
    await onSearch(Number(searchKeyVal))
  }

  async function triggerBatch() {
    if (isBatchRunning) return
    isBatchRunning = true
    try {
      await onBatchDemo()
    } finally {
      isBatchRunning = false
    }
  }

  async function triggerBulk() {
    if (bulkRunning || isLoading || massMode) return
    await onBulkLoad(Number(bulkN))
  }

  async function triggerDegree(target: number) {
    if (isLoading || massMode) return
    degreeOverride = target
    await onSetDegree(target)
  }
</script>

<header class="deck">
  <div class="deck-inner">
    <div class="brand">
      <span class="brand-mark" aria-hidden="true">
        <i></i><i></i><i></i>
      </span>
      <span class="brand-text">
        <strong>KRAVIDB</strong>
        <em>B-Tree · Slotted Page 4K</em>
      </span>
      <span class="tag">v0.1.0</span>
    </div>

    <div class="ops">
      <form class="field" onsubmit={handleInsertSubmit} aria-label="Insertar clave">
        <label for="ins-key">+INS</label>
        <input id="ins-key" type="number" bind:value={insertKeyVal} placeholder="clave" required />
        <input
          class="pl"
          type="text"
          bind:value={insertPayload}
          placeholder="carga útil"
          aria-label="Carga útil"
        />
        <button type="submit" class="go copper" disabled={isLoading || massMode}>
          <Plus size={13} strokeWidth={2.6} />
          <span>Escribir</span>
        </button>
      </form>

      <form class="field" onsubmit={handleSearchSubmit} aria-label="Buscar clave">
        <label for="scan-key">SCAN</label>
        <input id="scan-key" type="number" bind:value={searchKeyVal} placeholder="clave" required />
        <button type="submit" class="go mint" disabled={isSearching}>
          <Search size={13} strokeWidth={2.6} />
          <span>Buscar</span>
        </button>
      </form>

      <div class="field bulk-field">
        <label for="bulk-n">MASA</label>
        <select id="bulk-n" bind:value={bulkN} disabled={bulkRunning || massMode}>
          {#each bulkSizes as n (n)}
            <option value={n}>{n >= 1000 ? `${n / 1000}k` : n}</option>
          {/each}
        </select>
        <button
          type="button"
          class="go gold"
          onclick={triggerBulk}
          disabled={bulkRunning || isLoading || massMode}
        >
          <Layers size={13} strokeWidth={2.4} />
          <span>{bulkRunning ? `${Math.round(bulkPct)}%` : 'Cargar'}</span>
        </button>
      </div>

      <div class="field degree-field">
        <label for="degree-n">GRADO t</label>
        <select
          id="degree-n"
          value={degreeN}
          onchange={(e) => void triggerDegree(Number(e.currentTarget.value))}
          disabled={isLoading || massMode}
        >
          {#each degreeSizes as t (t)}
            <option value={t}>{t}</option>
          {/each}
        </select>
        <button
          type="button"
          class="go lavender"
          onclick={() => void triggerDegree(degreeN)}
          disabled={isLoading || massMode}
        >
          <Network size={13} strokeWidth={2.4} />
          <span>Construir</span>
        </button>
      </div>

      <button
        class="demo"
        onclick={triggerBatch}
        disabled={isBatchRunning || isLoading || massMode}
      >
        <Play size={12} fill="currentColor" />
        <span>{batch ? `Lote ${batch.current}/${batch.total}` : 'Lote'}</span>
        {#if batch}
          <span class="demo-pct">{Math.round((batch.current / batch.total) * 100)}%</span>
        {/if}
      </button>

      <div class="speed" role="group" aria-label="Velocidad de reproducción">
        <Gauge size={13} />
        {#each speeds as speed (speed)}
          <button
            class="sp"
            class:on={playbackSpeed === speed}
            onclick={() => onChangeSpeed(speed)}
          >
            {speed}×
          </button>
        {/each}
      </div>

      <button class="reset" onclick={() => onReset()} aria-label="Reiniciar motor">
        <RotateCcw size={14} />
      </button>
    </div>
  </div>

  {#if batch}
    <div class="batch-bar" aria-hidden="true">
      <span style="width: {(batch.current / batch.total) * 100}%"></span>
    </div>
  {/if}

  {#if bulkRunning}
    <div class="batch-bar mass-bar" aria-hidden="true">
      <span style="width: {bulkPct}%"></span>
    </div>
  {/if}

  {#if lastSplitOccurred}
    <div class="split">
      <span class="zig"><Zap size={14} /></span>
      <span>
        <b>SPLIT DE NODO</b> — nodo saturado (2t-1 = 3 claves). Mediana
        <b>[{promotedKey}]</b> elevada al padre.
      </span>
      <button onclick={onDismissSplitAlert} aria-label="Cerrar aviso">
        <X size={13} />
      </button>
    </div>
  {/if}
</header>

<style>
  .deck {
    position: sticky;
    top: 0;
    z-index: 40;
    backdrop-filter: blur(14px);
    background: color-mix(in srgb, var(--bg) 82%, transparent);
    border-bottom: 1px solid var(--line);
  }
  .deck-inner {
    width: 100%;
    max-width: 1920px;
    margin: 0 auto;
    padding: 12px clamp(16px, 2.5vw, 40px);
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 14px;
  }

  .brand {
    display: flex;
    align-items: center;
    gap: 11px;
  }
  .brand-mark {
    display: flex;
    align-items: flex-end;
    gap: 2px;
    width: 30px;
    height: 30px;
    padding: 6px;
    border-radius: 9px;
    border: 1px solid color-mix(in srgb, var(--copper) 40%, var(--line));
    background: linear-gradient(180deg, var(--panel-3), var(--panel));
    box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.06);
  }
  .brand-mark i {
    flex: 1;
    border-radius: 1px;
    background: var(--copper);
    animation: bars 2.6s ease-in-out infinite;
  }
  .brand-mark i:nth-child(1) {
    height: 40%;
  }
  .brand-mark i:nth-child(2) {
    height: 100%;
    animation-delay: 0.18s;
  }
  .brand-mark i:nth-child(3) {
    height: 65%;
    animation-delay: 0.36s;
  }
  @keyframes bars {
    0%,
    100% {
      transform: scaleY(1);
    }
    50% {
      transform: scaleY(0.55);
    }
  }
  .brand-text {
    display: flex;
    flex-direction: column;
    line-height: 1.15;
  }
  .brand-text strong {
    font-size: 16px;
    font-weight: 700;
    letter-spacing: 0.18em;
  }
  .brand-text em {
    font-family: var(--font-mono);
    font-size: 10.5px;
    font-style: normal;
    letter-spacing: 0.05em;
    color: var(--ink-mute);
  }
  .tag {
    padding: 3px 8px;
    border-radius: 99px;
    border: 1px solid color-mix(in srgb, var(--sakura) 35%, var(--line));
    background: color-mix(in srgb, var(--sakura) 12%, transparent);
    color: var(--sakura-hi);
    font-family: var(--font-mono);
    font-size: 10px;
    letter-spacing: 0.08em;
  }

  .ops {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 10px;
  }

  .field {
    display: flex;
    align-items: stretch;
    height: 38px;
    border: 1px solid var(--line-2);
    border-radius: 10px;
    background: #0f0e0b;
    box-shadow: inset 0 2px 6px -3px rgba(0, 0, 0, 0.9);
    overflow: hidden;
  }
  .field label {
    display: flex;
    align-items: center;
    padding: 0 11px;
    font-family: var(--font-mono);
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.14em;
    color: var(--sakura);
    background: color-mix(in srgb, var(--sakura) 10%, transparent);
    cursor: default;
  }
  .field input {
    width: 72px;
    border: 0;
    border-left: 1px solid var(--line);
    background: transparent;
    color: var(--ink);
    padding: 0 10px;
    font-family: var(--font-mono);
    font-size: 14px;
    text-align: center;
  }
  .field input.pl {
    width: 125px;
    text-align: left;
    font-size: 11.5px;
    color: var(--ink-dim);
  }
  .field input:focus {
    outline: none;
    background: rgba(255, 255, 255, 0.03);
  }
  .field input::placeholder {
    color: var(--ink-mute);
  }
  .field select {
    border: 0;
    border-left: 1px solid var(--line);
    background: transparent;
    color: var(--ink);
    padding: 0 8px;
    font-family: var(--font-mono);
    font-size: 13px;
    cursor: pointer;
  }
  .field select:focus {
    outline: none;
    background: rgba(255, 255, 255, 0.03);
  }
  .field select option {
    background: var(--panel);
    color: var(--ink);
  }
  .bulk-field label {
    color: var(--gold);
    background: color-mix(in srgb, var(--gold) 10%, transparent);
  }

  .degree-field label {
    color: var(--lavender);
    background: color-mix(in srgb, var(--lavender) 10%, transparent);
  }

  .go.lavender {
    background: color-mix(in srgb, var(--lavender) 16%, transparent);
    color: var(--lavender);
  }

  .go {
    display: flex;
    align-items: center;
    gap: 7px;
    padding: 0 14px;
    border: 0;
    border-left: 1px solid var(--line);
    cursor: pointer;
    font-size: 13px;
    font-weight: 600;
    transition:
      background 160ms ease,
      color 160ms ease;
  }
  .go.copper {
    background: color-mix(in srgb, var(--sakura) 18%, transparent);
    color: var(--sakura-hi);
  }
  .go.copper:hover {
    background: color-mix(in srgb, var(--sakura) 30%, transparent);
  }
  .go.mint {
    background: color-mix(in srgb, var(--mint) 14%, transparent);
    color: var(--mint-hi);
  }
  .go.mint:hover {
    background: color-mix(in srgb, var(--mint) 26%, transparent);
  }
  .go:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }
  .go.gold {
    background: color-mix(in srgb, var(--gold) 14%, transparent);
    color: var(--gold);
  }
  .go.gold:hover {
    background: color-mix(in srgb, var(--gold) 26%, transparent);
  }

  .demo {
    display: flex;
    align-items: center;
    gap: 7px;
    height: 38px;
    padding: 0 16px;
    border-radius: 10px;
    border: 1px solid color-mix(in srgb, var(--gold) 40%, var(--line));
    background: color-mix(in srgb, var(--gold) 10%, transparent);
    color: var(--gold);
    font-size: 13px;
    font-weight: 600;
    cursor: pointer;
    transition:
      background 160ms ease,
      transform 160ms ease;
  }
  .demo:hover:not(:disabled) {
    background: color-mix(in srgb, var(--gold) 20%, transparent);
    transform: translateY(-1px);
  }
  .demo:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }
  .demo-pct {
    font-family: var(--font-mono);
    font-size: 10.5px;
    padding: 1px 6px;
    border-radius: 99px;
    background: color-mix(in srgb, var(--gold) 22%, transparent);
    color: var(--gold);
  }

  .batch-bar {
    height: 3px;
    background: #100d1e;
    overflow: hidden;
  }
  .batch-bar span {
    display: block;
    height: 100%;
    background: linear-gradient(90deg, color-mix(in srgb, var(--gold) 40%, #000), var(--gold));
    box-shadow: 0 0 10px -1px var(--gold);
    transition: width 420ms var(--ease-out);
  }
  .mass-bar span {
    background: linear-gradient(90deg, color-mix(in srgb, var(--sakura) 40%, #000), var(--sakura));
    box-shadow: 0 0 10px -1px var(--sakura);
  }

  .speed {
    display: flex;
    align-items: center;
    gap: 4px;
    height: 38px;
    padding: 0 10px;
    border-radius: 10px;
    border: 1px solid var(--line);
    background: var(--panel);
    color: var(--ink-mute);
  }
  .sp {
    border: 0;
    background: transparent;
    color: var(--ink-mute);
    font-family: var(--font-mono);
    font-size: 11.5px;
    padding: 4px 8px;
    border-radius: 6px;
    cursor: pointer;
    transition:
      background 150ms ease,
      color 150ms ease;
  }
  .sp:hover {
    color: var(--ink);
  }
  .sp.on {
    background: color-mix(in srgb, var(--sakura) 22%, transparent);
    color: var(--sakura-hi);
    font-weight: 700;
  }

  .reset {
    display: grid;
    place-items: center;
    width: 38px;
    height: 38px;
    border-radius: 10px;
    border: 1px solid var(--line);
    background: var(--panel);
    color: var(--ink-mute);
    cursor: pointer;
    transition:
      color 160ms ease,
      border-color 160ms ease,
      transform 320ms var(--ease-out);
  }
  .reset:hover {
    color: var(--ember);
    border-color: color-mix(in srgb, var(--ember) 45%, var(--line));
    transform: rotate(-90deg);
  }

  .split {
    display: flex;
    align-items: center;
    gap: 9px;
    padding: 7px clamp(12px, 3vw, 26px);
    border-top: 1px solid color-mix(in srgb, var(--ember) 30%, transparent);
    background: color-mix(in srgb, var(--ember) 10%, transparent);
    color: #ffd0c4;
    font-size: 12px;
    animation: banner-in 360ms var(--ease-out) both;
  }
  @keyframes banner-in {
    from {
      opacity: 0;
      transform: translateY(-8px);
    }
  }
  .split .zig {
    color: var(--ember);
    animation: zap 900ms ease-in-out infinite;
  }
  @keyframes zap {
    0%,
    100% {
      transform: scale(1);
      opacity: 1;
    }
    50% {
      transform: scale(1.25);
      opacity: 0.7;
    }
  }
  .split b {
    color: #fff;
  }
  .split button {
    margin-left: auto;
    display: grid;
    place-items: center;
    border: 0;
    background: transparent;
    color: var(--ember);
    cursor: pointer;
  }

  @media (max-width: 720px) {
    .field input.pl {
      display: none;
    }
  }
</style>
