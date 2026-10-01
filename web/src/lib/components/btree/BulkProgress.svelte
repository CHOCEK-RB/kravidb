<script lang="ts">
  import type { BulkState } from '$lib/stores/engine.svelte'
  import { Network } from '@lucide/svelte'

  interface Props {
    bulk: BulkState
    degree?: number
  }

  let { bulk, degree = 2 }: Props = $props()

  const nf = new Intl.NumberFormat('es-ES')

  const pct = $derived(bulk.total > 0 ? (bulk.current / bulk.total) * 100 : 0)
  const remaining = $derived(Math.max(0, bulk.total - bulk.current))
  const etaMs = $derived(bulk.current > 0 ? (bulk.elapsedMs / bulk.current) * remaining : 0)
  const eta = $derived(etaMs >= 1000 ? `${(etaMs / 1000).toFixed(1)} s` : `${Math.round(etaMs)} ms`)

  const peak = $derived(bulk.samples.reduce((max, sample) => Math.max(max, sample.opsPerSec), 1))
  const sparkPoints = $derived.by(() => {
    const samples = bulk.samples
    if (samples.length < 2) return '0,30 100,30'
    return samples
      .map((sample, index) => {
        const x = (index / (samples.length - 1)) * 100
        const y = 30 - (sample.opsPerSec / peak) * 27
        return `${x.toFixed(2)},${y.toFixed(2)}`
      })
      .join(' ')
  })
</script>

<div class="panel live">
  <header class="head">
    <span class="panel-title"><Network size={13} /> Carga masiva en curso</span>
    <span class="badge">t = {degree}</span>
  </header>

  <div class="hero">
    <div class="pct-row">
      <span class="big">{pct.toFixed(1)}%</span>
      <span class="count">{nf.format(bulk.current)} / {nf.format(bulk.total)}</span>
    </div>
    <div class="track"><span class="fill" style="width: {pct}%"></span></div>
    <span class="eta">faltan ~{eta} · {nf.format(remaining)} tuplas</span>
  </div>

  <div class="grid">
    <div class="g">
      <span class="k">ops/s</span><span class="v">{nf.format(bulk.opsPerSec)}</span>
    </div>
    <div class="g">
      <span class="k">divisiones</span><span class="v">{nf.format(bulk.splits)}</span>
    </div>
    <div class="g">
      <span class="k">tiempo</span><span class="v">{(bulk.elapsedMs / 1000).toFixed(1)} s</span>
    </div>
    <div class="g"><span class="k">grado</span><span class="v">t = {degree}</span></div>
  </div>

  <div class="spark-wrap">
    <div class="spark-head">
      <span class="eyebrow">Throughput por trozo</span>
      <span class="peak">pico {nf.format(peak)} ops/s</span>
    </div>
    <svg class="spark" viewBox="0 0 100 32" preserveAspectRatio="none" aria-hidden="true">
      <polyline points={sparkPoints} />
    </svg>
  </div>

  <p class="note">
    Seis obreros insertan en paralelo contra la API; el trazado del árbol vuelve al terminar.
  </p>
</div>

<style>
  .live {
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
  .badge {
    font-family: var(--font-mono);
    font-size: 11px;
    letter-spacing: 0.08em;
    padding: 2px 9px;
    border-radius: 99px;
    color: var(--gold);
    background: color-mix(in srgb, var(--gold) 14%, transparent);
    border: 1px solid color-mix(in srgb, var(--gold) 40%, var(--line));
  }

  .hero {
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 22px 20px 20px;
    border-bottom: 1px solid var(--line);
  }
  .pct-row {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 12px;
  }
  .big {
    font-family: var(--font-mono);
    font-size: 46px;
    font-weight: 800;
    line-height: 1;
    letter-spacing: -0.03em;
    color: var(--ink);
    font-variant-numeric: tabular-nums;
  }
  .count {
    font-family: var(--font-mono);
    font-size: 13px;
    color: var(--ink-dim);
    font-variant-numeric: tabular-nums;
  }
  .track {
    height: 10px;
    border-radius: 99px;
    background: #100d1e;
    box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.8);
    overflow: hidden;
  }
  .fill {
    display: block;
    height: 100%;
    border-radius: 99px;
    background: linear-gradient(90deg, color-mix(in srgb, var(--gold) 40%, #000), var(--gold));
    transition: width 320ms var(--ease-out);
  }
  .eta {
    font-size: 12.5px;
    color: var(--ink-mute);
    font-variant-numeric: tabular-nums;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 1px;
    background: var(--line);
    border-bottom: 1px solid var(--line);
  }
  .g {
    display: flex;
    flex-direction: column;
    gap: 5px;
    padding: 13px 14px;
    background: var(--panel);
  }
  .g .k {
    font-family: var(--font-mono);
    font-size: 10px;
    letter-spacing: 0.1em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .g .v {
    font-family: var(--font-mono);
    font-size: 18px;
    font-weight: 700;
    color: var(--ink);
    font-variant-numeric: tabular-nums;
  }

  .spark-wrap {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 16px 20px 6px;
  }
  .spark-head {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 12px;
  }
  .peak {
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--gold);
  }
  .spark {
    width: 100%;
    height: 84px;
    border-radius: 10px;
    border: 1px solid var(--line);
    background: #100d1e;
  }
  .spark polyline {
    fill: none;
    stroke: var(--gold);
    stroke-width: 1.6;
    vector-effect: non-scaling-stroke;
    stroke-linejoin: round;
  }

  .note {
    padding: 12px 20px 18px;
    font-size: 12.5px;
    line-height: 1.55;
    color: var(--ink-dim);
  }
</style>
