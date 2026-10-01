<script lang="ts">
  import type { SearchMetrics } from '$lib/api/types'
  import { Tween } from 'svelte/motion'
  import { cubicOut } from 'svelte/easing'
  import { Binoculars, List, Rocket } from '@lucide/svelte'

  interface Props {
    metrics: SearchMetrics | null
    isSearching?: boolean
    onSearchPreset: (key: number) => void
  }

  let { metrics, isSearching = false, onSearchPreset }: Props = $props()

  const presets = [10, 20, 35, 50, 99]

  const indexNs = new Tween(0, { duration: 420, easing: cubicOut })
  const fullUs = new Tween(0, { duration: 420, easing: cubicOut })
  const speedup = new Tween(1, { duration: 520, easing: cubicOut })

  $effect(() => {
    const m = metrics
    if (!m) {
      indexNs.target = 0
      fullUs.target = 0
      speedup.target = 1
      return
    }
    indexNs.target = m.index_scan.latency_ns
    fullUs.target = m.full_scan.latency_ns
    speedup.target = Math.max(
      1,
      Math.round((m.full_scan.latency_ns * 1000) / Math.max(1, m.index_scan.latency_ns)),
    )
  })

  const indexPct = $derived(
    metrics
      ? Math.max(1.5, (metrics.index_scan.latency_ns / (metrics.full_scan.latency_ns * 1000)) * 100)
      : 0,
  )

  const nf = new Intl.NumberFormat('es-ES')
</script>

<div class="panel bench">
  <header class="head">
    <span class="panel-title">
      <Binoculars size={13} />
      Índice vs Barrido
    </span>
    {#if metrics}
      <span class="verdict" class:miss={!metrics.found}>
        {metrics.found ? 'encontrada' : 'sin coincidencia'} · clave {metrics.key}
      </span>
    {/if}
  </header>

  {#if !metrics}
    <div class="empty">
      <p>
        Ejecuta una búsqueda para medir la diferencia entre descender por el árbol y barrer todas
        las tuplas.
      </p>
      <div class="presets">
        {#each presets as k (k)}
          <button onclick={() => onSearchPreset(k)}>#{k}</button>
        {/each}
      </div>
    </div>
  {:else}
    <div class="hero">
      <span class="rocket"><Rocket size={16} /></span>
      <span class="x">{Math.round(speedup.current)}<i>×</i></span>
      <span class="hero-label">más rápido que el barrido completo</span>
    </div>

    {#if isSearching}
      {#key metrics.key}
        <div class="race" aria-hidden="true">
          <div class="lane idx">
            <span class="lane-tag">IDX · O(log n)</span>
            <span class="lane-track"><span class="runner"></span></span>
          </div>
          <div class="lane full">
            <span class="lane-tag">FULL · O(n)</span>
            <span class="lane-track"><span class="runner"></span></span>
          </div>
          <span class="race-flag">descendiendo la clave {metrics.key}…</span>
        </div>
      {/key}
    {/if}

    <div class="meters">
      <div class="meter mint">
        <div class="m-head">
          <span class="m-name"><span class="ico"><Binoculars size={12} /></span> Index Scan</span>
          <span class="m-complex">O(log n)</span>
        </div>
        <div class="track"><span class="fill" style="width: {indexPct}%"></span></div>
        <div class="m-foot">
          <b>{nf.format(Math.round(indexNs.current))}</b> ns
          <span class="sep">·</span>
          {metrics.index_scan.page_ios} I/O
          <span class="sep">·</span>
          {metrics.index_scan.nodes_visited} nodos
        </div>
      </div>

      <div class="meter copper">
        <div class="m-head">
          <span class="m-name"><span class="ico"><List size={12} /></span> Full Scan</span>
          <span class="m-complex">O(n)</span>
        </div>
        <div class="track"><span class="fill" style="width: 100%"></span></div>
        <div class="m-foot">
          <b>{nf.format(Math.round(fullUs.current))}</b> µs
          <span class="sep">·</span>
          {metrics.full_scan.page_ios} I/O
          <span class="sep">·</span>
          {metrics.full_scan.tuples_scanned} tuplas
        </div>
      </div>
    </div>
  {/if}
</div>

<style>
  .bench {
    --tint: var(--gold);
    display: flex;
    flex-direction: column;
  }
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 13px 16px;
    border-bottom: 1px solid var(--line);
  }
  .verdict {
    font-family: var(--font-mono);
    font-size: 11.5px;
    letter-spacing: 0.08em;
    color: var(--mint);
  }
  .verdict.miss {
    color: var(--gold);
  }

  .empty {
    padding: 20px 16px 18px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .empty p {
    font-size: 13px;
    line-height: 1.55;
    color: var(--ink-dim);
  }
  .presets {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }
  .presets button {
    padding: 6px 13px;
    border-radius: 99px;
    border: 1px solid var(--line-2);
    background: var(--panel);
    color: var(--ink-dim);
    font-family: var(--font-mono);
    font-size: 12px;
    cursor: pointer;
    transition:
      color 160ms ease,
      border-color 160ms ease,
      transform 160ms ease;
  }
  .presets button:hover {
    color: var(--mint-hi);
    border-color: color-mix(in srgb, var(--mint) 45%, var(--line));
    transform: translateY(-2px);
  }

  .hero {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 16px;
    border-bottom: 1px solid var(--line);
    background: radial-gradient(
      120% 200% at 0% 0%,
      color-mix(in srgb, var(--mint) 12%, transparent),
      transparent 60%
    );
  }
  .rocket {
    color: var(--mint);
    animation: hover 2.4s ease-in-out infinite;
  }
  @keyframes hover {
    0%,
    100% {
      transform: translateY(0);
    }
    50% {
      transform: translateY(-3px);
    }
  }
  .x {
    font-family: var(--font-mono);
    font-size: 46px;
    font-weight: 800;
    line-height: 1;
    color: var(--ink);
    font-variant-numeric: tabular-nums;
    letter-spacing: -0.02em;
  }
  .x i {
    color: var(--mint);
    font-style: normal;
    font-size: 26px;
  }
  .hero-label {
    font-size: 13.5px;
    line-height: 1.35;
    color: var(--ink-dim);
    max-width: 160px;
  }

  .race {
    display: flex;
    flex-direction: column;
    gap: 9px;
    padding: 14px 16px;
    border-bottom: 1px solid var(--line);
    background: #100d1e;
  }
  .lane {
    display: flex;
    align-items: center;
    gap: 9px;
  }
  .lane-tag {
    flex: 0 0 96px;
    font-family: var(--font-mono);
    font-size: 10.5px;
    letter-spacing: 0.06em;
  }
  .idx .lane-tag {
    color: var(--mint);
  }
  .full .lane-tag {
    color: var(--sakura);
  }
  .lane-track {
    position: relative;
    flex: 1;
    height: 8px;
    border-radius: 99px;
    background: #1a1530;
    box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.8);
    overflow: hidden;
  }
  .runner {
    position: absolute;
    top: 0;
    left: 0;
    width: 12px;
    height: 100%;
    border-radius: 99px;
    animation: race-run 900ms var(--ease-out) forwards;
  }
  .idx .runner {
    background: var(--mint);
    box-shadow: 0 0 12px 1px var(--mint);
  }
  .full .runner {
    background: var(--sakura);
    box-shadow: 0 0 12px 1px var(--sakura);
    animation-duration: 3200ms;
  }
  @keyframes race-run {
    from {
      left: 0;
    }
    to {
      left: calc(100% - 12px);
    }
  }
  .race-flag {
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--ink-mute);
  }

  .meters {
    display: flex;
    flex-direction: column;
    gap: 1px;
    background: var(--line);
  }
  .meter {
    padding: 14px 16px;
    background: var(--panel);
  }
  .m-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 10px;
  }
  .m-name {
    display: flex;
    align-items: center;
    gap: 7px;
    font-size: 14.5px;
    font-weight: 600;
    color: var(--ink);
  }
  .mint .ico {
    color: var(--mint);
  }
  .copper .ico {
    color: var(--sakura);
  }
  .ico {
    display: inline-flex;
  }
  .m-complex {
    font-family: var(--font-mono);
    font-size: 11.5px;
    color: var(--ink-mute);
  }
  .track {
    height: 9px;
    border-radius: 99px;
    background: #100d1e;
    box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.8);
    overflow: hidden;
  }
  .fill {
    display: block;
    height: 100%;
    border-radius: 99px;
    transition: width 560ms var(--ease-out);
  }
  .mint .fill {
    background: linear-gradient(90deg, color-mix(in srgb, var(--mint) 40%, #000), var(--mint));
    box-shadow: 0 0 14px -2px var(--mint);
  }
  .copper .fill {
    background: linear-gradient(90deg, color-mix(in srgb, var(--sakura) 40%, #000), var(--sakura));
    box-shadow: 0 0 14px -2px var(--sakura);
  }
  .m-foot {
    margin-top: 9px;
    display: flex;
    align-items: baseline;
    gap: 7px;
    font-family: var(--font-mono);
    font-size: 12.5px;
    color: var(--ink-mute);
  }
  .m-foot b {
    font-size: 19px;
    font-weight: 700;
    color: var(--ink);
    font-variant-numeric: tabular-nums;
  }
  .sep {
    color: var(--line-3);
  }
</style>
