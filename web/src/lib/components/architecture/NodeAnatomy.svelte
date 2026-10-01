<script lang="ts">
  import type { BTreeNode } from '$lib/api/types'
  import { Network } from '@lucide/svelte'

  interface Props {
    node: BTreeNode | null
    isRoot?: boolean
    degree?: number
  }

  let { node, isRoot = false, degree = 2 }: Props = $props()

  const T = $derived(degree)
  const MAX_KEYS = $derived(2 * T - 1)
  const MAX_CHILDREN = $derived(2 * T)
  const MIN_KEYS = $derived(T - 1)

  const cells = $derived(Array.from({ length: MAX_KEYS }, (_, i) => node?.keys[i] ?? null))
  const pointers = $derived(
    Array.from({ length: MAX_CHILDREN }, (_, i) => node?.children[i] ?? null),
  )
  const isLeaf = $derived(!node || node.children.length === 0)
  const role = $derived(!node ? '—' : `${isRoot ? 'raíz · ' : ''}${isLeaf ? 'hoja' : 'interno'}`)
  const fillPct = $derived(node ? (node.keys.length / MAX_KEYS) * 100 : 0)
</script>

<div class="panel anatomy">
  <header class="head">
    <span class="panel-title"><Network size={13} /> Anatomía del nodo</span>
    <span class="badge">{role}</span>
  </header>

  {#if !node}
    <p class="idle">Sin nodo seleccionado. Haz clic en un nodo del árbol para inspeccionarlo.</p>
  {:else}
    <div class="spec">
      <span class="cell"><i>grado</i><b>t = {T}</b></span>
      <span class="cell"><i>claves</i><b>{node.keys.length} / {MAX_KEYS}</b></span>
      <span class="cell"><i>hijos</i><b>{node.children.length} / {MAX_CHILDREN}</b></span>
      <span class="cell"><i>mín. claves</i><b>{MIN_KEYS}</b></span>
    </div>

    <div class="diagram" aria-hidden="true">
      <div class="keys">
        {#each cells as k, i (i)}
          <span class="kcell" class:filled={k !== null}>
            <span class="kval">{k ? k.key : 'libre'}</span>
            {#if k}<span class="kpos">P{k.page_id}:S{k.slot_id}</span>{/if}
          </span>
        {/each}
      </div>
      <div class="ptrs" class:leaf={isLeaf}>
        {#each pointers as p, i (i)}
          <span class="ptr" class:filled={p !== null}>{isLeaf ? '·' : p ? '▼' : '—'}</span>
        {/each}
      </div>
      <p class="ptr-note">
        {isLeaf
          ? 'Nodo hoja: las claves apuntan a un RowID (PageID + SlotID) del heap.'
          : `Nodo interno: hasta 2t = ${MAX_CHILDREN} punteros a hijos y ${node.keys.length} claves separadoras.`}
      </p>
    </div>

    <div class="occ">
      <div class="occ-head"><span>Ocupación del nodo</span><span>{Math.round(fillPct)}%</span></div>
      <div class="occ-track"><span class="occ-fill" style="width: {fillPct}%"></span></div>
      <p class="rule">
        Se divide al alcanzar <b>2t − 1 = {MAX_KEYS}</b> claves: la mediana sube al padre y el nodo se
        parte en dos hijos.
      </p>
    </div>
  {/if}
</div>

<style>
  .anatomy {
    --tint: var(--lavender);
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
    color: var(--lavender);
  }
  .idle {
    padding: 22px 16px;
    font-size: 13px;
    line-height: 1.55;
    color: var(--ink-mute);
  }

  .spec {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 1px;
    background: var(--line);
    border-bottom: 1px solid var(--line);
  }
  .cell {
    display: flex;
    flex-direction: column;
    gap: 3px;
    padding: 10px 12px;
    background: var(--panel);
  }
  .cell i {
    font-family: var(--font-mono);
    font-style: normal;
    font-size: 10px;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .cell b {
    font-family: var(--font-mono);
    font-size: 16px;
    color: var(--ink);
  }

  .diagram {
    padding: 16px;
    border-bottom: 1px solid var(--line);
  }
  .keys {
    display: flex;
    gap: 6px;
  }
  .kcell {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
    padding: 9px 4px;
    border-radius: 9px;
    border: 1px dashed var(--line-2);
    background: #141124;
  }
  .kcell.filled {
    border-style: solid;
    border-color: color-mix(in srgb, var(--mint) 55%, var(--line-2));
    background: #162a26;
  }
  .kval {
    font-family: var(--font-mono);
    font-size: 15px;
    font-weight: 700;
    color: var(--ink-dim);
  }
  .kcell.filled .kval {
    color: var(--mint-hi);
  }
  .kpos {
    font-family: var(--font-mono);
    font-size: 9.5px;
    color: var(--ink-mute);
  }
  .ptrs {
    display: flex;
    justify-content: space-between;
    padding: 6px 8px 0;
    color: var(--line-3);
    font-size: 11px;
  }
  .ptr.filled {
    color: var(--lavender);
  }
  .ptrs.leaf {
    color: var(--ink-mute);
  }
  .ptr-note {
    margin-top: 6px;
    font-size: 11.5px;
    line-height: 1.45;
    color: var(--ink-mute);
  }

  .occ {
    padding: 14px 16px 16px;
  }
  .occ-head {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    font-family: var(--font-mono);
    font-size: 11px;
    letter-spacing: 0.1em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .occ-track {
    margin-top: 8px;
    height: 8px;
    border-radius: 99px;
    background: #100d1e;
    box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.8);
    overflow: hidden;
  }
  .occ-fill {
    display: block;
    height: 100%;
    border-radius: 99px;
    background: linear-gradient(
      90deg,
      color-mix(in srgb, var(--lavender) 40%, #000),
      var(--lavender)
    );
    transition: width 420ms var(--ease-out);
  }
  .rule {
    margin-top: 10px;
    font-size: 12.5px;
    line-height: 1.5;
    color: var(--ink-dim);
  }
  .rule b {
    color: var(--ink);
    font-family: var(--font-mono);
  }
</style>
