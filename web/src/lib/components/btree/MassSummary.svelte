<script lang="ts">
  import type { BTreeNode } from '$lib/api/types'
  import type { BulkState } from '$lib/stores/engine.svelte'
  import { Network, RotateCcw } from '@lucide/svelte'

  interface Props {
    tree: BTreeNode | null
    keys: number
    nodes: number
    height: number
    bulk: BulkState | null
    degree?: number
    onReset: () => void | Promise<unknown>
  }

  let { tree, keys, nodes, height, bulk, degree = 2, onReset }: Props = $props()

  const nf = new Intl.NumberFormat('es-ES')

  const level1 = $derived(tree?.children.slice(0, 8) ?? [])
  const hidden = $derived(Math.max(0, (tree?.children.length ?? 0) - level1.length))

  const keysOf = (node: BTreeNode) => node.keys.map((k) => k.key).join(' · ') || '—'
</script>

<div class="panel mass">
  <header class="head">
    <span class="panel-title"><Network size={13} /> Carga masiva · vista resumida</span>
    {#if bulk?.done}
      <span class="badge">completada</span>
    {/if}
  </header>

  <div class="hero">
    <span class="big">{nf.format(bulk?.opsPerSec ?? 0)}</span>
    <span class="unit">ops/s</span>
    <span class="hero-sub">inserciones por segundo medidas en bloque</span>
  </div>

  <div class="stats">
    <div class="stat"><span class="k">Tuplas</span><span class="v">{nf.format(keys)}</span></div>
    <div class="stat"><span class="k">Nodos</span><span class="v">{nf.format(nodes)}</span></div>
    <div class="stat"><span class="k">Altura</span><span class="v">{height}</span></div>
    <div class="stat">
      <span class="k">Divisiones</span><span class="v">{nf.format(bulk?.splits ?? 0)}</span>
    </div>
    <div class="stat">
      <span class="k">Tiempo</span><span class="v"
        >{nf.format(Math.round(bulk?.elapsedMs ?? 0))} ms</span
      >
    </div>
    <div class="stat"><span class="k">Grado</span><span class="v">t = {degree}</span></div>
  </div>

  <div class="schematic">
    <span class="eyebrow">Cima del árbol · niveles 0 – 1</span>
    {#if tree}
      <div class="tier">
        <div class="mini root">
          <span class="role">RAÍZ</span>
          <span class="keys">{keysOf(tree)}</span>
        </div>
      </div>
      <div class="tier">
        {#each level1 as child (child.id)}
          <div class="mini" class:leaf={child.is_leaf}>
            <span class="role">{child.is_leaf ? 'HOJA' : 'NODO'}</span>
            <span class="keys">{keysOf(child)}</span>
          </div>
        {/each}
        {#if hidden > 0}
          <div class="mini more">+{hidden} nodos…</div>
        {/if}
      </div>
    {/if}
  </div>

  <p class="note">
    Con {nf.format(nodes)} nodos el trazado interactivo saturaría el navegador, así que se omitió el layout
    completo. El índice sigue siendo consultable con SCAN; reinicia para volver al modo normal.
  </p>

  <button class="reset" onclick={() => onReset()}>
    <RotateCcw size={13} />
    Volver al modo interactivo
  </button>
</div>

<style>
  .mass {
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
    color: var(--mint-hi);
    background: color-mix(in srgb, var(--mint) 16%, transparent);
    border: 1px solid color-mix(in srgb, var(--mint) 40%, var(--line));
  }

  .hero {
    display: flex;
    align-items: baseline;
    flex-wrap: wrap;
    gap: 8px;
    padding: 22px 20px 18px;
    border-bottom: 1px solid var(--line);
    background: radial-gradient(
      120% 200% at 0% 0%,
      color-mix(in srgb, var(--gold) 12%, transparent),
      transparent 60%
    );
  }
  .big {
    font-family: var(--font-mono);
    font-size: 54px;
    font-weight: 800;
    line-height: 1;
    letter-spacing: -0.03em;
    color: var(--ink);
    font-variant-numeric: tabular-nums;
  }
  .unit {
    font-family: var(--font-mono);
    font-size: 18px;
    font-weight: 700;
    color: var(--gold);
  }
  .hero-sub {
    flex-basis: 100%;
    font-size: 13px;
    color: var(--ink-dim);
  }

  .stats {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
    gap: 1px;
    background: var(--line);
  }
  .stat {
    display: flex;
    flex-direction: column;
    gap: 5px;
    padding: 14px 16px;
    background: var(--panel);
  }
  .stat .k {
    font-family: var(--font-mono);
    font-size: 10.5px;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .stat .v {
    font-family: var(--font-mono);
    font-size: 20px;
    font-weight: 700;
    color: var(--ink);
    font-variant-numeric: tabular-nums;
  }

  .schematic {
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 18px 20px;
    border-bottom: 1px solid var(--line);
  }
  .tier {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
  }
  .mini {
    display: flex;
    flex-direction: column;
    gap: 4px;
    padding: 8px 11px;
    border-radius: 10px;
    border: 1px solid var(--line-2);
    background: var(--panel-2);
  }
  .mini.root {
    border-color: color-mix(in srgb, var(--gold) 55%, var(--line-2));
    background: color-mix(in srgb, var(--gold) 12%, var(--panel-2));
  }
  .mini.leaf {
    border-color: color-mix(in srgb, var(--mint) 45%, var(--line-2));
  }
  .mini.more {
    justify-content: center;
    font-family: var(--font-mono);
    font-size: 12px;
    color: var(--ink-mute);
    border-style: dashed;
  }
  .mini .role {
    font-family: var(--font-mono);
    font-size: 9.5px;
    letter-spacing: 0.1em;
    color: var(--ink-mute);
  }
  .mini.root .role {
    color: var(--gold);
  }
  .mini.leaf .role {
    color: var(--mint);
  }
  .mini .keys {
    font-family: var(--font-mono);
    font-size: 12.5px;
    color: var(--ink);
  }

  .note {
    padding: 16px 20px;
    font-size: 12.5px;
    line-height: 1.55;
    color: var(--ink-dim);
  }

  .reset {
    margin: 0 20px 20px;
    display: inline-flex;
    align-items: center;
    gap: 8px;
    align-self: flex-start;
    padding: 9px 15px;
    border-radius: 10px;
    border: 1px solid color-mix(in srgb, var(--gold) 40%, var(--line));
    background: color-mix(in srgb, var(--gold) 12%, transparent);
    color: var(--gold);
    font-size: 13px;
    font-weight: 600;
    cursor: pointer;
    transition:
      background 160ms ease,
      transform 160ms ease;
  }
  .reset:hover {
    background: color-mix(in srgb, var(--gold) 22%, transparent);
    transform: translateY(-1px);
  }
</style>
