<script lang="ts">
  import type { PageTuple } from '$lib/api/types'
  import type { ArchField } from '$lib/stores/engine.svelte'
  import { Binary } from '@lucide/svelte'

  interface Props {
    tuple: PageTuple | null
    pageId: number
    slotId: number | null
    archField: ArchField | null
    onHoverField?: (field: ArchField | null) => void
  }

  let { tuple, pageId, slotId, archField, onHoverField }: Props = $props()

  const payloadBytes = $derived(tuple ? new TextEncoder().encode(tuple.data).length : 0)
  const hx = (n: number) => (n >>> 0).toString(16).toUpperCase().padStart(8, '0')
  const rowId = $derived(
    slotId === null
      ? '—'
      : `0x${((pageId >>> 0) * 0x100000000 + (slotId >>> 0)).toString(16).toUpperCase()}`,
  )

  function enter(field: ArchField) {
    onHoverField?.(field)
  }
  function leave() {
    onHoverField?.(null)
  }
</script>

<div class="panel anatomy">
  <header class="head">
    <span class="panel-title"><Binary size={13} /> Tupla &amp; RowID</span>
    {#if tuple}<span class="badge">slot #{slotId}</span>{/if}
  </header>

  {#if !tuple}
    <p class="idle">
      Selecciona una clave en el índice o una tupla en la losa para descomponer su RowID.
    </p>
  {:else}
    <div class="bar">
      <button
        type="button"
        class="seg hdr"
        class:on={archField === 'pageId'}
        onmouseenter={() => enter('pageId')}
        onmouseleave={leave}
        onfocus={() => enter('pageId')}
        onblur={leave}
      >
        <span class="s-name">PageID</span>
        <span class="s-bits">32 bits</span>
        <span class="s-val">0x{hx(pageId)}</span>
      </button>
      <button
        type="button"
        class="seg hdr"
        class:on={archField === 'slotId'}
        onmouseenter={() => enter('slotId')}
        onmouseleave={leave}
        onfocus={() => enter('slotId')}
        onblur={leave}
      >
        <span class="s-name">SlotID</span>
        <span class="s-bits">32 bits</span>
        <span class="s-val">{slotId === null ? '—' : `0x${hx(slotId)}`}</span>
      </button>
      <button
        type="button"
        class="seg body"
        class:on={archField === 'payload'}
        onmouseenter={() => enter('payload')}
        onmouseleave={leave}
        onfocus={() => enter('payload')}
        onblur={leave}
      >
        <span class="s-name">Payload</span>
        <span class="s-bits">{payloadBytes} B</span>
      </button>
    </div>

    <div class="formula">
      <span class="f-label">RowID · 64 bits</span>
      <code>PageID ≪ 32 | SlotID = {rowId}</code>
    </div>

    <ul class="facts">
      <li><span>Clave</span><b>{tuple.key}</b></li>
      <li><span>Página física</span><b>#{pageId}</b></li>
      <li><span>Ranura</span><b>#{slotId}</b></li>
      <li><span>Tamaño de tupla</span><b>{tuple.size_bytes} B</b></li>
    </ul>
  {/if}
</div>

<style>
  .anatomy {
    --tint: var(--mint);
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
    color: var(--mint);
  }
  .idle {
    padding: 22px 16px;
    font-size: 13px;
    line-height: 1.55;
    color: var(--ink-mute);
  }

  .bar {
    display: flex;
    gap: 3px;
    padding: 16px;
    border-bottom: 1px solid var(--line);
  }
  .seg {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 2px;
    padding: 9px 10px;
    border-radius: 9px;
    border: 1px solid var(--line-2);
    background: #141124;
    cursor: pointer;
    transition:
      border-color 180ms ease,
      background 180ms ease,
      transform 180ms ease;
  }
  .seg:hover,
  .seg.on {
    transform: translateY(-2px);
  }
  .seg.hdr {
    flex: 0 0 auto;
  }
  .seg.hdr:hover,
  .seg.hdr.on {
    border-color: var(--gold);
    background: #241d38;
  }
  .seg.body {
    flex: 1 1 auto;
    min-width: 0;
  }
  .seg.body:hover,
  .seg.body.on {
    border-color: var(--mint);
    background: #162a26;
  }
  .s-name {
    font-family: var(--font-mono);
    font-size: 12px;
    font-weight: 700;
    color: var(--ink);
  }
  .s-bits {
    font-family: var(--font-mono);
    font-size: 10px;
    color: var(--ink-mute);
  }
  .s-val {
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--gold);
  }

  .formula {
    display: flex;
    flex-direction: column;
    gap: 5px;
    padding: 13px 16px;
    border-bottom: 1px solid var(--line);
  }
  .f-label {
    font-family: var(--font-mono);
    font-size: 10px;
    letter-spacing: 0.14em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .formula code {
    font-family: var(--font-mono);
    font-size: 13px;
    color: var(--mint-hi);
    word-break: break-all;
  }

  .facts {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 1px;
    margin: 0;
    padding: 0;
    list-style: none;
    background: var(--line);
  }
  .facts li {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 10px;
    padding: 9px 14px;
    background: var(--panel);
  }
  .facts span {
    font-family: var(--font-mono);
    font-size: 10.5px;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .facts b {
    font-family: var(--font-mono);
    font-size: 15px;
    color: var(--ink);
  }
</style>
