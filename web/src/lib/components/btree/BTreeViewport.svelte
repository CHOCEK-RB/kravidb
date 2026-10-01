<script lang="ts">
  import type { BTreeKey, BTreeNode, SearchPathStep } from '$lib/api/types'
  import { hierarchy, tree, type HierarchyPointNode } from 'd3-hierarchy'
  import { zoom as d3zoom, zoomIdentity, type ZoomBehavior } from 'd3-zoom'
  import { select } from 'd3-selection'
  import 'd3-transition'
  import { fade, scale } from 'svelte/transition'
  import { cubicOut } from 'svelte/easing'
  import { Network, ZoomIn, ZoomOut, Maximize2 } from '@lucide/svelte'

  interface Props {
    treeData: BTreeNode | null
    highlightedNodeId?: string | null
    highlightedKey?: number | null
    lastSplitOccurred?: boolean
    promotedKey?: number | null
    newRoot?: boolean
    /** Velocidad de reproducción: escala las animaciones de split/crecimiento. */
    speed?: number
    /** Grado mínimo del árbol: define el máximo de claves por nodo (2t-1). */
    degree?: number
    isSearching?: boolean
    searchPath?: SearchPathStep[]
    activeStepIndex?: number
    onSelectKey: (key: BTreeKey) => void
    onSelectNode: (id: string) => void
  }

  let {
    treeData,
    highlightedNodeId = null,
    highlightedKey = null,
    lastSplitOccurred = false,
    promotedKey = null,
    newRoot = false,
    speed = 1,
    degree = 2,
    isSearching = false,
    searchPath = [],
    activeStepIndex = -1,
    onSelectKey,
    onSelectNode,
  }: Props = $props()

  const NODE_H = 62
  const CELL_W = 56
  const CELL_GAP = 6
  const X_GAP = 220
  const X_GAP_PADDING = 64
  const Y_GAP = 150

  type Placed = {
    id: string
    data: BTreeNode
    x: number
    y: number
    w: number
  }

  type Link = {
    d: string
    sourceId: string
    targetId: string
  }

  /** Claves dibujadas por nodo antes de resumir el resto en el contador. */
  const MAX_VISIBLE_KEYS = 6

  const widthOf = (n: BTreeNode) => {
    if (!n.keys.length) return 96
    const shown = Math.min(n.keys.length, MAX_VISIBLE_KEYS + 1)
    return 26 + shown * (CELL_W + CELL_GAP) - CELL_GAP
  }

  const layout = $derived.by(() => {
    if (!treeData) return { nodes: [] as Placed[], links: [] as Link[] }
    const root = hierarchy<BTreeNode>(treeData, (d) =>
      d.children && d.children.length ? d.children : undefined,
    )
    // La separación horizontal se adapta al nodo más ancho: con grados altos
    // (más claves por nodo) la separación fija solapaba los nodos.
    const widest = root
      .descendants()
      .reduce((max, current) => Math.max(max, widthOf(current.data)), 0)
    const spacing = Math.max(X_GAP, widest + X_GAP_PADDING)
    const laid = tree<BTreeNode>().nodeSize([spacing, Y_GAP])(root)
    const pts = laid.descendants() as HierarchyPointNode<BTreeNode>[]
    const nodes: Placed[] = pts.map((p) => ({
      id: p.data.id,
      data: p.data,
      x: p.x,
      y: p.depth * Y_GAP,
      w: widthOf(p.data),
    }))
    const links: Link[] = pts
      .filter((p) => p.parent)
      .map((p) => {
        const par = p.parent as HierarchyPointNode<BTreeNode>
        const sx = par.x
        const sy = par.depth * Y_GAP + NODE_H / 2
        const tx = p.x
        const ty = p.depth * Y_GAP - NODE_H / 2
        const mid = (sy + ty) / 2
        return {
          d: `M ${sx} ${sy} C ${sx} ${mid}, ${tx} ${mid}, ${tx} ${ty}`,
          sourceId: par.data.id,
          targetId: p.data.id,
        }
      })
    return { nodes, links }
  })

  const stats = $derived({
    nodes: layout.nodes.length,
    depth: layout.nodes.length ? Math.max(...layout.nodes.map((n) => n.y)) / Y_GAP + 1 : 0,
  })

  const activeStep = $derived(activeStepIndex >= 0 ? (searchPath[activeStepIndex] ?? null) : null)
  const probeNode = $derived(
    activeStep ? (layout.nodes.find((n) => n.id === activeStep.node_id) ?? null) : null,
  )
  const activeCellIx = $derived(activeStep ? activeStep.key_index_checked : -1)
  const visitedIds = $derived(searchPath.slice(0, activeStepIndex).map((s) => s.node_id))
  const traversedLinks = $derived.by(() => {
    const ids = searchPath.slice(0, activeStepIndex + 1).map((s) => s.node_id)
    const pairs: string[] = []
    for (let i = 1; i < ids.length; i += 1) {
      if (ids[i] !== ids[i - 1]) pairs.push(`${ids[i - 1]}=>${ids[i]}`)
    }
    return pairs
  })

  const glyphOf = (c?: string) =>
    c === 'equal' ? '=' : c === 'less' ? '<' : c === 'greater' ? '>' : '·'
  const wordOf = (c?: string) =>
    c === 'equal' ? 'igual' : c === 'less' ? 'menor' : c === 'greater' ? 'mayor' : '—'

  /** Nodo interno que recibió la mediana tras un split: punto de aterrizaje del token. */
  const medianNode = $derived(
    lastSplitOccurred && promotedKey !== null
      ? (layout.nodes.find(
          (n) => !n.data.is_leaf && n.data.keys.some((k) => k.key === promotedKey),
        ) ?? null)
      : null,
  )

  const cellX = (i: number, w: number) => -w / 2 + 13 + i * (CELL_W + CELL_GAP)

  let wrapEl = $state<HTMLDivElement | null>(null)
  let svgEl = $state<SVGSVGElement | null>(null)
  let gEl = $state<SVGGElement | null>(null)
  let size = $state({ w: 960, h: 520 })
  let zoomPct = $state(90)
  let zb: ZoomBehavior<SVGSVGElement, unknown> | null = null
  let touched = $state(false)

  $effect(() => {
    if (!wrapEl) return
    const ro = new ResizeObserver((entries) => {
      const r = entries[0].contentRect
      size = { w: Math.max(320, r.width), h: Math.max(320, r.height) }
    })
    ro.observe(wrapEl)
    return () => ro.disconnect()
  })

  $effect(() => {
    if (!svgEl || !gEl) return
    const g = gEl
    const z = d3zoom<SVGSVGElement, unknown>()
      .scaleExtent([0.28, 2.6])
      .on('zoom', (e) => {
        select(g).attr('transform', e.transform.toString())
        zoomPct = Math.round(e.transform.k * 100)
        if (e.sourceEvent) touched = true
      })
    zb = z
    select(svgEl).call(z)
    return () => {
      select(svgEl as SVGSVGElement).on('.zoom', null)
    }
  })

  const bbox = $derived.by(() => {
    const ns = layout.nodes
    if (!ns.length) return { w: 200, h: 80, cx: 0, cy: 0 }
    const xs = ns.map((n) => n.x)
    const ys = ns.map((n) => n.y)
    const minX = Math.min(...xs)
    const maxX = Math.max(...xs)
    const minY = Math.min(...ys)
    const maxY = Math.max(...ys)
    const maxW = Math.max(...ns.map((n) => n.w))
    return {
      w: maxX - minX + maxW,
      h: maxY - minY + NODE_H,
      cx: (minX + maxX) / 2,
      cy: (minY + maxY) / 2,
    }
  })

  /** Encuadre que centra el árbol en el panel (hasta 90 %). */
  const fitTransform = (minScale: number) => {
    const pad = 58
    const kx = (size.w - pad) / Math.max(160, bbox.w)
    const ky = (size.h - pad) / Math.max(90, bbox.h)
    const scale = Math.min(0.9, Math.max(minScale, Math.min(kx, ky)))
    return zoomIdentity
      .translate(size.w / 2 - bbox.cx * scale, size.h / 2 - bbox.cy * scale)
      .scale(scale)
  }

  /** Escala inicial: prioriza que el árbol quepa entero, sin bajar del 45 %. */
  const home = () => fitTransform(0.45)

  /** Encuadre total (puede quedar pequeño): es lo que hace el botón «maximizar». */
  const fitAll = () => fitTransform(0.28)

  $effect(() => {
    const w = size.w
    if (touched || !svgEl || !zb || w < 100 || !bbox.w) return
    select(svgEl).call(zb.transform, home())
  })

  function zoomBy(f: number) {
    touched = true
    if (!svgEl || !zb) return
    select(svgEl).transition().duration(240).call(zb.scaleBy, f)
  }
</script>

<div class="tree" class:searching={isSearching} style="--sp: {1 / speed}" bind:this={wrapEl}>
  <svg bind:this={svgEl} class="canvas" role="presentation">
    <g bind:this={gEl}>
      {#each layout.links as l (l.sourceId + '=>' + l.targetId)}
        <path
          class="link"
          class:hot={traversedLinks.includes(`${l.sourceId}=>${l.targetId}`)}
          d={l.d}
          pathLength="1"
          in:fade={{ duration: 300 }}
        />
      {/each}

      {#each layout.nodes as n (n.id)}
        <g
          class="node"
          class:leaf={n.data.is_leaf}
          class:on={n.id === highlightedNodeId}
          class:done={visitedIds.includes(n.id)}
          class:probing={probeNode?.id === n.id}
          class:root-new={newRoot && n.y === 0}
          style="transform: translate({n.x}px, {n.y}px)"
          role="button"
          tabindex="0"
          aria-label="Nodo {n.id}, {n.data.keys.length} claves"
          onclick={() => onSelectNode(n.id)}
          onkeydown={(e) => {
            if (e.key === 'Enter' || e.key === ' ') onSelectNode(n.id)
          }}
        >
          <g
            in:scale={{ duration: 460, start: 0.55, easing: cubicOut }}
            out:fade={{ duration: 180 }}
          >
            <rect class="shell" x={-n.w / 2} y={-NODE_H / 2} width={n.w} height={NODE_H} rx="12" />
            <line
              class="divider"
              x1={-n.w / 2 + 10}
              y1={-NODE_H / 2 + 20}
              x2={n.w / 2 - 10}
              y2={-NODE_H / 2 + 20}
            />
            <text class="role" x={-n.w / 2 + 13} y={-NODE_H / 2 + 14}>
              {n.data.is_leaf ? 'HOJA' : 'NODO'}
            </text>
            <text class="slots" x={n.w / 2 - 13} y={-NODE_H / 2 + 14} text-anchor="end">
              {n.data.keys.length}/{2 * degree - 1}
            </text>

            {#if n.data.keys.length === 0}
              <text class="void" x="0" y="16" text-anchor="middle">vacío</text>
            {:else}
              {#each n.data.keys.slice(0, MAX_VISIBLE_KEYS) as k, i (k.key)}
                <g
                  class="cell"
                  class:on={k.key === highlightedKey}
                  class:promo={lastSplitOccurred && k.key === promotedKey}
                  class:probe={probeNode?.id === n.id && i === activeCellIx}
                  role="button"
                  tabindex="0"
                  aria-label="Clave {k.key} en página {k.page_id} slot {k.slot_id}"
                  onclick={(e) => {
                    e.stopPropagation()
                    onSelectKey(k)
                  }}
                  onkeydown={(e) => {
                    if (e.key === 'Enter' || e.key === ' ') {
                      e.stopPropagation()
                      onSelectKey(k)
                    }
                  }}
                >
                  <rect x={cellX(i, n.w)} y="-10" width={CELL_W} height="36" rx="7" />
                  <text class="knum" x={cellX(i, n.w) + CELL_W / 2} y="7" text-anchor="middle">
                    {k.key}
                  </text>
                  <text class="kpos" x={cellX(i, n.w) + CELL_W / 2} y="20" text-anchor="middle">
                    P{k.page_id}:S{k.slot_id}
                  </text>
                </g>
              {/each}
            {/if}
          </g>
        </g>
      {/each}

      {#if medianNode && promotedKey !== null}
        {#key promotedKey}
          <g class="median" style="transform: translate({medianNode.x}px, {medianNode.y}px)">
            <g class="median-cast">
              <rect x="-30" y="-16" width="60" height="32" rx="8" />
              <text x="0" y="6" text-anchor="middle">{promotedKey}</text>
            </g>
          </g>
        {/key}
      {/if}

      {#if isSearching && probeNode}
        <g class="probe" style="transform: translate({probeNode.x}px, {probeNode.y}px)">
          <rect
            class="probe-ring"
            x={-probeNode.w / 2 - 7}
            y={-NODE_H / 2 - 7}
            width={probeNode.w + 14}
            height={NODE_H + 14}
            rx="16"
          />
          {#if activeCellIx >= 0 && activeCellIx < probeNode.data.keys.length}
            <circle
              class="probe-dot"
              cx={cellX(activeCellIx, probeNode.w) + CELL_W / 2}
              cy="8"
              r="5"
            />
          {/if}
        </g>
      {/if}

      {#if isSearching && activeStep && probeNode}
        <g class="cmp" style="transform: translate({probeNode.x}px, {probeNode.y}px)">
          <rect x="-35" y={-NODE_H / 2 - 34} width="70" height="24" rx="7" />
          <text x="0" y={-NODE_H / 2 - 17} text-anchor="middle">
            S{activeStep.key_index_checked}
            {glyphOf(activeStep.comparison)}
          </text>
        </g>
      {/if}
    </g>
  </svg>

  <div class="hud tl">
    <span class="hud-title"
      ><span class="ico"><Network size={13} /></span> Árbol B · t = {degree}</span
    >
    <span class="hud-row">
      <i>{stats.nodes}</i> nodos<span>·</span><i>{stats.depth}</i> niveles
    </span>
    {#if isSearching && activeStep}
      <span class="hud-trace">
        paso {activeStepIndex + 1}/{searchPath.length} · S{activeStep.key_index_checked}
        {glyphOf(activeStep.comparison)} · {wordOf(activeStep.comparison)}
      </span>
    {/if}
  </div>

  <div class="hud tr">
    <span class="pct">{zoomPct}%</span>
    <button onclick={() => zoomBy(1.3)} aria-label="Acercar"><ZoomIn size={14} /></button>
    <button onclick={() => zoomBy(1 / 1.3)} aria-label="Alejar"><ZoomOut size={14} /></button>
    <button
      onclick={() => {
        touched = true
        if (svgEl && zb) select(svgEl).transition().duration(340).call(zb.transform, fitAll())
      }}
      aria-label="Encuadrar todo el árbol"><Maximize2 size={14} /></button
    >
  </div>
</div>

<style>
  .tree {
    position: relative;
    height: 660px;
    border-radius: 14px;
    overflow: hidden;
    background:
      radial-gradient(circle at 1px 1px, rgba(255, 255, 255, 0.045) 1px, transparent 0) 0 0 / 26px
        26px,
      linear-gradient(180deg, #141210, #0f0e0c);
  }
  .canvas {
    display: block;
    width: 100%;
    height: 100%;
    cursor: grab;
  }
  .canvas:active {
    cursor: grabbing;
  }

  .link {
    fill: none;
    stroke: var(--line-3);
    stroke-width: 1.4;
    transition: opacity 220ms ease;
  }
  .link.hot {
    stroke: var(--mint-hi);
    stroke-width: 3;
    filter: drop-shadow(0 0 5px color-mix(in srgb, var(--mint) 70%, transparent));
  }

  .node {
    cursor: pointer;
    transition:
      transform 460ms var(--ease-out),
      opacity 220ms ease;
  }
  .node:focus-visible {
    outline: none;
  }
  .shell {
    fill: #1a162b;
    stroke: var(--line-2);
    stroke-width: 1;
    filter: drop-shadow(0 14px 18px rgba(0, 0, 0, 0.55));
    transition:
      stroke 260ms ease,
      fill 260ms ease;
  }
  .node:hover .shell {
    stroke: var(--line-3);
    fill: #241e3a;
  }
  .node.leaf .shell {
    fill: #1e1933;
    stroke: color-mix(in srgb, var(--sakura) 35%, var(--line-2));
  }
  .node.on .shell {
    stroke: var(--mint);
    fill: #162a26;
  }
  .divider {
    stroke: rgba(255, 255, 255, 0.09);
    stroke-width: 1;
  }
  .role,
  .slots {
    font-family: var(--font-mono);
    font-size: 11px;
    letter-spacing: 0.16em;
  }
  .role {
    fill: var(--ink-mute);
  }
  .node.leaf .role {
    fill: var(--sakura-lo);
  }
  .node.on .role {
    fill: var(--mint);
  }
  .slots {
    fill: var(--ink-mute);
  }
  .void {
    font-family: var(--font-mono);
    font-size: 11px;
    fill: var(--ink-mute);
  }

  .cell {
    cursor: pointer;
  }
  .cell rect {
    fill: #241d38;
    stroke: rgba(0, 0, 0, 0.5);
    stroke-width: 1;
    transition:
      fill 200ms ease,
      stroke 200ms ease,
      transform 200ms ease;
  }
  .cell:hover rect {
    fill: #342a52;
    stroke: var(--line-3);
  }
  .knum {
    font-family: var(--font-mono);
    font-size: 18px;
    font-weight: 800;
    fill: var(--ink);
  }
  .kpos {
    font-family: var(--font-mono);
    font-size: 11px;
    fill: var(--ink-mute);
    letter-spacing: 0.04em;
  }
  .cell.on rect {
    fill: #173029;
    stroke: var(--mint);
  }
  .cell.on .knum {
    fill: var(--mint-hi);
  }
  .cell.on .kpos {
    fill: color-mix(in srgb, var(--mint) 70%, var(--ink-mute));
  }
  /* La celda promovida espera a que la mediana termine de ascender (≈78%). */
  .cell.promo {
    animation: promo-reveal calc(550ms * var(--sp, 1)) var(--ease-out) both;
  }
  @keyframes promo-reveal {
    0%,
    78% {
      opacity: 0;
    }
    100% {
      opacity: 1;
    }
  }
  .cell.promo rect {
    fill: #2a2312;
    stroke: var(--gold);
  }
  .cell.promo .knum {
    fill: var(--gold);
  }

  /* Token de la mediana: asciende desde la hoja dividida hacia el nodo padre. */
  .median {
    pointer-events: none;
  }
  .median-cast {
    animation: median-rise calc(980ms * var(--sp, 1)) var(--ease-out) forwards;
  }
  .median-cast rect {
    fill: #4a3a16;
    stroke: var(--gold);
    stroke-width: 1.4;
    filter: drop-shadow(0 0 10px color-mix(in srgb, var(--gold) 60%, transparent));
  }
  .median-cast text {
    font-family: var(--font-mono);
    font-size: 16px;
    font-weight: 800;
    fill: var(--gold);
  }
  @keyframes median-rise {
    0% {
      transform: translateY(96px) scale(0.7);
      opacity: 0;
    }
    20% {
      opacity: 1;
      transform: translateY(72px) scale(1);
    }
    62% {
      transform: translateY(0) scale(1);
      opacity: 1;
    }
    100% {
      transform: translateY(-2px) scale(0.9);
      opacity: 0;
    }
  }

  /* Raíz recién creada: destello dorado mientras el árbol gana un nivel. */
  .node.root-new .shell {
    stroke: var(--gold);
    fill: #241d38;
    filter: drop-shadow(0 0 12px color-mix(in srgb, var(--gold) 55%, transparent));
  }
  .node.root-new .role {
    fill: var(--gold);
  }

  .hud {
    position: absolute;
    display: flex;
    flex-direction: column;
    gap: 5px;
    padding: 11px 15px;
    border-radius: 11px;
    border: 1px solid var(--line);
    background: color-mix(in srgb, var(--bg) 82%, transparent);
    backdrop-filter: blur(8px);
  }
  .tl {
    top: 14px;
    left: 14px;
  }
  .tr {
    top: 14px;
    right: 14px;
    flex-direction: row;
    align-items: center;
    gap: 8px;
  }
  .hud-title {
    display: flex;
    align-items: center;
    gap: 7px;
    font-size: 13.5px;
    font-weight: 600;
    color: var(--ink);
  }
  .hud-title .ico {
    display: inline-flex;
    color: var(--sakura);
  }
  .hud-row {
    display: flex;
    align-items: baseline;
    gap: 6px;
    font-family: var(--font-mono);
    font-size: 12.5px;
    color: var(--ink-mute);
  }
  .hud-row i {
    font-style: normal;
    font-weight: 700;
    color: var(--ink-dim);
  }
  .hud-trace {
    font-family: var(--font-mono);
    font-size: 11.5px;
    color: var(--mint);
  }
  .pct {
    font-family: var(--font-mono);
    font-size: 13px;
    color: var(--ink-dim);
    padding-right: 4px;
  }
  .tr button {
    display: grid;
    place-items: center;
    width: 32px;
    height: 32px;
    border-radius: 8px;
    border: 1px solid var(--line);
    background: transparent;
    color: var(--ink-dim);
    cursor: pointer;
    transition:
      color 160ms ease,
      border-color 160ms ease,
      background 160ms ease;
  }
  .tr button:hover {
    color: var(--mint-hi);
    border-color: color-mix(in srgb, var(--mint) 40%, var(--line));
    background: color-mix(in srgb, var(--mint) 10%, transparent);
  }

  .searching .link.hot {
    stroke-width: 3;
  }

  .node.done .shell {
    stroke: color-mix(in srgb, var(--mint) 45%, var(--line-2));
  }
  .node.probing .shell {
    stroke: var(--mint);
    fill: #162a26;
  }
  .cell.probe rect {
    fill: #1c3a32;
    stroke: var(--mint);
    stroke-width: 1.6;
  }
  .probe {
    pointer-events: none;
    transition: transform 300ms var(--ease-out);
  }
  .probe-ring {
    fill: none;
    stroke: var(--mint);
    stroke-width: 2;
    filter: drop-shadow(0 0 6px color-mix(in srgb, var(--mint) 60%, transparent));
  }
  .probe-dot {
    fill: var(--mint-hi);
    filter: drop-shadow(0 0 6px var(--mint));
  }
  .cmp {
    pointer-events: none;
    animation: cmp-in 240ms var(--ease-spring);
  }
  .cmp rect {
    fill: #162a26;
    stroke: var(--mint);
    stroke-width: 1;
  }
  .cmp text {
    font-family: var(--font-mono);
    font-size: 12px;
    font-weight: 700;
    fill: var(--mint-hi);
  }
  @keyframes cmp-in {
    from {
      opacity: 0;
    }
    to {
      opacity: 1;
    }
  }

  .tree.searching .link:not(.hot) {
    opacity: 0.32;
  }
  .tree.searching .node:not(.done):not(.probing) {
    opacity: 0.4;
  }
</style>
