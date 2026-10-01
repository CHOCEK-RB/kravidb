<script lang="ts">
  import type { BTreeNode, BTreeKey, SlottedPageData } from '$lib/api/types'
  import { engine } from '$lib/stores/engine.svelte'
  import { padPayload } from '$lib/utils'
  import CommandDeck from '$lib/components/workbench/CommandDeck.svelte'
  import TelemetryBar from '$lib/components/workbench/TelemetryBar.svelte'
  import EventTerminal from '$lib/components/workbench/EventTerminal.svelte'
  import BTreeViewport from '$lib/components/btree/BTreeViewport.svelte'
  import MassSummary from '$lib/components/btree/MassSummary.svelte'
  import PageSlab from '$lib/components/storage/PageSlab.svelte'
  import HexByteViewer from '$lib/components/storage/HexByteViewer.svelte'
  import ScanBenchmark from '$lib/components/metrics/ScanBenchmark.svelte'
  import NodeAnatomy from '$lib/components/architecture/NodeAnatomy.svelte'
  import TupleAnatomy from '$lib/components/architecture/TupleAnatomy.svelte'
  import { Layers, Network, ArrowRight } from '@lucide/svelte'

  const emptyPage: SlottedPageData = {
    page_id: 0,
    header: { page_id: 0, lsn: 0, slot_count: 0, free_space_offset: 64, free_space_end: 4096 },
    slots: [],
    tuples: [],
    total_bytes: 4096,
    free_bytes: 4096,
  }

  const currentPage = $derived(
    engine.pages.find((p) => p.page_id === engine.selectedPageId) ?? engine.currentPage,
  )
  const selectedSlot = $derived(
    currentPage?.slots.find((s) => s.slot_id === engine.selectedSlotId) ?? null,
  )
  const selectedTuple = $derived(
    currentPage?.tuples.find((t) => t.slot_id === engine.selectedSlotId) ?? null,
  )

  function findNode(node: BTreeNode | null, id: string | null): BTreeNode | null {
    if (!node) return null
    if (node.id === id) return node
    for (const child of node.children) {
      const hit = findNode(child, id)
      if (hit) return hit
    }
    return null
  }

  const archNode = $derived(findNode(engine.tree, engine.highlightedNodeId) ?? engine.tree)

  let rackEl = $state<HTMLDivElement | null>(null)

  // Auto-scroll: sigue la losa activa mientras el lote inserta en serie.
  $effect(() => {
    const id = engine.selectedPageId
    const at = engine.lastInsert?.at ?? engine.located?.at ?? 0
    void at
    if (!rackEl) return
    const el = rackEl.querySelector<HTMLElement>(`[data-page="${id}"]`)
    if (!el) return
    const left = el.offsetLeft - (rackEl.clientWidth - el.offsetWidth) / 2
    rackEl.scrollTo({ left: Math.max(0, left), behavior: 'smooth' })
  })

  function handleSelectKey(key: BTreeKey) {
    engine.highlightedKey = key.key
    engine.selectSlot(key.page_id, key.slot_id)
    engine.addLog(
      'info',
      `Puntero a clave ${key.key}`,
      `Del índice lógico a la losa física: Página #${key.page_id}, Slot #${key.slot_id}.`,
    )
  }

  function handleSelectNode(id: string) {
    engine.highlightedNodeId = id
  }

  function handleSelectSlot(pageId: number, slotId: number) {
    engine.selectSlot(pageId, slotId)
    const t = engine.pages
      .find((p) => p.page_id === pageId)
      ?.tuples.find((x) => x.slot_id === slotId)
    if (t) engine.highlightedKey = t.key
  }

  function handleFocusPage(pageId: number) {
    void engine.selectPage(pageId)
  }

  async function handleBatchDemo() {
    const keys = [40, 48, 62, 75, 90, 12, 55, 68, 82, 95, 2, 44, 58, 71]
    for (let i = 0; i < keys.length; i += 1) {
      engine.batch = { current: i + 1, total: keys.length }
      await engine.insertKey(
        keys[i],
        padPayload(keys[i], JSON.stringify({ table: 'users', role: 'active' })),
      )
      await new Promise((r) => setTimeout(r, 560 / engine.playbackSpeed))
    }
    engine.batch = null
  }
</script>

<div class="app">
  <CommandDeck
    isLoading={engine.isLoading}
    isSearching={engine.isSearching}
    playbackSpeed={engine.playbackSpeed}
    lastSplitOccurred={engine.lastSplitOccurred}
    promotedKey={engine.promotedKey}
    batch={engine.batch}
    bulk={engine.bulk}
    massMode={engine.massMode}
    degree={engine.degree}
    onInsert={(k, p) => engine.insertKey(k, p)}
    onSearch={(k) => engine.executeSearch(k)}
    onBatchDemo={handleBatchDemo}
    onBulkLoad={(n) => engine.bulkLoad(n)}
    onSetDegree={(n) => engine.setDegree(n)}
    onReset={() => engine.resetEngine()}
    onChangeSpeed={(s) => (engine.playbackSpeed = s)}
    onDismissSplitAlert={() => (engine.lastSplitOccurred = false)}
  />

  <TelemetryBar
    totalKeys={engine.totalKeys}
    nodes={engine.treeNodeCount}
    depth={engine.treeDepth}
    pageCount={engine.massMode ? engine.pageIds.length : engine.pages.length}
    selectedPageId={engine.selectedPageId}
  />

  <main>
    <section class="hero">
      <header class="hero-head">
        <div class="hh-left">
          <span class="eyebrow"><Layers size={12} /> Estante de losas · 4 KB por página</span>
          <h1>Memoria física</h1>
        </div>
        <div class="chain" aria-label="Cadena de resolución">
          <span class="node-k">
            clave {engine.highlightedKey !== null ? engine.highlightedKey : '—'}
          </span>
          <ArrowRight size={13} />
          <span class="node-p">
            P{engine.selectedPageId}:S{engine.selectedSlotId ?? '—'}
          </span>
          <ArrowRight size={13} />
          <span class="node-b">
            {selectedTuple ? `${selectedTuple.size_bytes} B` : 'bytes'}
          </span>
        </div>
      </header>

      <div class="rack" bind:this={rackEl}>
        {#if engine.massMode}
          <div class="mass-note">
            <span class="mn-title">Modo masa activo</span>
            <span class="mn-body">
              {engine.pageIds.length} losas en el heap. La materialización de losas está desactivada para
              mantener el render fluido; reinicia para volver al modo interactivo.
            </span>
          </div>
        {:else}
          {#each engine.pages as page (page.page_id)}
            <PageSlab
              {page}
              focused={page.page_id === engine.selectedPageId}
              selectedSlotId={engine.selectedSlotId}
              lastInsert={engine.lastInsert}
              strained={engine.strainedPageId === page.page_id}
              highlightedKey={engine.highlightedKey}
              located={engine.located?.pageId === page.page_id}
              locatedSlotId={engine.located?.pageId === page.page_id
                ? (engine.located?.slotId ?? null)
                : null}
              onSelectSlot={handleSelectSlot}
              onFocusPage={handleFocusPage}
            />
          {/each}
          {#if engine.pages.length === 0}
            <div class="skeleton">Cargando losas…</div>
          {/if}
        {/if}
      </div>
    </section>

    <section class="lower">
      <div class="tree-col">
        <header class="col-head">
          <span class="panel-title"><Network size={13} /> Índice lógico</span>
          <span class="hint">arrastra para mover · rueda para zoom · clic en una clave</span>
        </header>
        {#if engine.massMode}
          <MassSummary
            tree={engine.tree}
            keys={engine.totalKeys}
            nodes={engine.treeNodeCount}
            height={engine.treeDepth}
            bulk={engine.bulk}
            degree={engine.degree}
            onReset={() => engine.resetEngine()}
          />
        {:else}
          <BTreeViewport
            treeData={engine.tree}
            highlightedNodeId={engine.highlightedNodeId}
            highlightedKey={engine.highlightedKey}
            lastSplitOccurred={engine.lastSplitOccurred}
            promotedKey={engine.promotedKey}
            newRoot={engine.lastNewRoot}
            speed={engine.playbackSpeed}
            isSearching={engine.isSearching}
            searchPath={engine.searchMetrics?.path ?? []}
            activeStepIndex={engine.activeStepIndex}
            degree={engine.degree}
            onSelectKey={handleSelectKey}
            onSelectNode={handleSelectNode}
          />
        {/if}
      </div>

      <aside class="side">
        <ScanBenchmark
          metrics={engine.searchMetrics}
          isSearching={engine.isSearching}
          onSearchPreset={(k) => engine.executeSearch(k)}
        />
        <HexByteViewer
          page={currentPage ?? emptyPage}
          {selectedSlot}
          {selectedTuple}
          archField={engine.archField}
        />
        <EventTerminal logs={engine.logs} onClearLogs={() => (engine.logs = [])} />
      </aside>
    </section>

    <section class="anatomy">
      <NodeAnatomy node={archNode} isRoot={archNode === engine.tree} degree={engine.degree} />
      <TupleAnatomy
        tuple={selectedTuple}
        pageId={engine.selectedPageId}
        slotId={engine.selectedSlotId}
        archField={engine.archField}
        onHoverField={(f) => (engine.archField = f)}
      />
    </section>
  </main>

  <footer>
    <span>kravidb · visualizador B-Tree &amp; slotted page</span>
    <span>capa visual desacoplada del núcleo C++ · mock en memoria</span>
  </footer>
</div>

<style>
  .app {
    width: 100%;
    max-width: 1920px;
    margin: 0 auto;
    padding: 8px clamp(16px, 2.5vw, 40px) 48px;
  }

  main {
    display: flex;
    flex-direction: column;
    gap: 26px;
  }

  .hero-head {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-end;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 16px;
  }
  .eyebrow {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    color: var(--sakura);
  }
  .hh-left h1 {
    margin-top: 6px;
    font-size: clamp(24px, 4vw, 40px);
    font-weight: 700;
    letter-spacing: -0.02em;
    line-height: 1;
    background: linear-gradient(100deg, var(--ink) 20%, var(--sakura-hi) 60%, var(--lavender));
    -webkit-background-clip: text;
    background-clip: text;
    color: transparent;
  }

  .chain {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 7px 12px;
    border-radius: 10px;
    border: 1px solid var(--line);
    background: var(--panel);
    color: var(--ink-mute);
  }
  .chain span {
    font-family: var(--font-mono);
    font-size: 13px;
  }
  .node-k {
    color: var(--gold);
    font-weight: 700;
  }
  .node-p {
    color: var(--mint);
    font-weight: 700;
  }
  .node-b {
    color: var(--sakura-hi);
    font-weight: 700;
  }

  .rack {
    position: relative;
    display: flex;
    align-items: flex-start;
    justify-content: safe center;
    gap: 26px;
    overflow-x: auto;
    padding: 4px 6px 22px;
    scrollbar-width: thin;
  }
  .rack::after {
    content: '';
    position: absolute;
    left: 10px;
    right: 10px;
    bottom: 8px;
    height: 1px;
    background: linear-gradient(
      90deg,
      transparent,
      var(--line-2) 15%,
      var(--line-2) 85%,
      transparent
    );
  }
  .skeleton {
    flex: 0 0 auto;
    width: 500px;
    height: 780px;
    display: grid;
    place-items: center;
    border-radius: 18px;
    border: 1px dashed var(--line-2);
    color: var(--ink-mute);
    font-family: var(--font-mono);
    font-size: 13px;
  }
  .mass-note {
    flex: 0 0 auto;
    width: min(560px, 90vw);
    min-height: 220px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    justify-content: center;
    padding: 26px;
    border-radius: 18px;
    border: 1px dashed color-mix(in srgb, var(--gold) 40%, var(--line-2));
    background: color-mix(in srgb, var(--gold) 6%, var(--panel));
    color: var(--ink-dim);
  }
  .mn-title {
    font-family: var(--font-mono);
    font-size: 11px;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    color: var(--gold);
  }
  .mn-body {
    font-size: 14px;
    line-height: 1.55;
  }

  .lower {
    display: grid;
    grid-template-columns: minmax(0, 2.2fr) minmax(390px, 1fr);
    gap: 24px;
    align-items: start;
  }
  .tree-col {
    display: flex;
    flex-direction: column;
    gap: 10px;
    min-width: 0;
  }
  .col-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
    flex-wrap: wrap;
  }
  .hint {
    font-family: var(--font-mono);
    font-size: 10.5px;
    color: var(--ink-mute);
  }

  .side {
    display: flex;
    flex-direction: column;
    gap: 16px;
    min-width: 0;
  }

  .anatomy {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 24px;
    align-items: start;
  }

  footer {
    display: flex;
    flex-wrap: wrap;
    gap: 6px 18px;
    justify-content: space-between;
    margin-top: 34px;
    padding-top: 16px;
    border-top: 1px solid var(--line);
    font-family: var(--font-mono);
    font-size: 10px;
    letter-spacing: 0.04em;
    color: var(--ink-mute);
  }

  @media (max-width: 1100px) {
    .lower {
      grid-template-columns: 1fr;
    }
    .anatomy {
      grid-template-columns: 1fr;
    }
  }
</style>
