<script lang="ts">
  import type { PageSlot, PageTuple, SlottedPageData } from '$lib/api/types'
  import type { ArchField } from '$lib/stores/engine.svelte'
  import { Binary } from '@lucide/svelte'

  interface Props {
    page: SlottedPageData
    selectedSlot: PageSlot | null
    selectedTuple: PageTuple | null
    archField?: ArchField | null
  }

  let { page, selectedSlot, selectedTuple, archField = null }: Props = $props()

  // Sobre de tupla: 8 B de RowID (PageID 32 + SlotID 32, big-endian) + payload.
  const rowIdBytes = $derived.by(() => {
    if (!selectedSlot) return [] as number[]
    const i32be = (n: number) => [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]
    return [...i32be(page.page_id), ...i32be(selectedSlot.slot_id)]
  })

  const bytes = $derived(
    selectedTuple
      ? [...rowIdBytes, ...Array.from(new TextEncoder().encode(selectedTuple.data))]
      : [],
  )

  const lba = $derived(selectedSlot ? (page.page_id - 1) * 4096 + selectedSlot.offset : 0)

  const hex4 = (n: number) => n.toString(16).toUpperCase().padStart(4, '0')
  const hex2 = (n: number) => n.toString(16).toUpperCase().padStart(2, '0')

  const rows = $derived.by(() => {
    const out: { off: number; cells: string[]; ascii: string }[] = []
    for (let i = 0; i < bytes.length; i += 16) {
      const slice = bytes.slice(i, i + 16)
      out.push({
        off: i,
        cells: slice.map(hex2),
        ascii: slice.map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : '·')).join(''),
      })
    }
    return out
  })
</script>

<div class="panel lens">
  <header class="head">
    <span class="panel-title">
      <Binary size={13} />
      Lente de bytes
    </span>
    {#if selectedSlot}
      <span class="lba">LBA 0x{hex4(lba)}</span>
    {/if}
  </header>

  {#if !selectedSlot || !selectedTuple}
    <div class="idle-state">
      <div class="idle-icon"><Binary size={22} /></div>
      <p class="idle-title">Sin tupla seleccionada</p>
      <p class="idle-desc">
        Toca cualquier tupla o ranura en la página para inspeccionar su volcado binario.
      </p>
    </div>
  {:else}
    <div class="meta">
      <span><i>clave</i><b>{selectedTuple.key}</b></span>
      <span><i>slot</i><b>#{selectedSlot.slot_id}</b></span>
      <span><i>offset</i><b>0x{hex4(selectedSlot.offset)}</b></span>
      <span><i>tamaño</i><b>{selectedTuple.size_bytes} B</b></span>
    </div>

    <div class="ascii">
      <span class="label">ASCII</span>
      <p>{selectedTuple.data}</p>
    </div>

    <div class="dump">
      {#each rows as row (row.off)}
        <div class="drow">
          <span class="doff">{hex4(row.off)}</span>
          <span class="dhex">
            {#each row.cells as c, i (i)}
              {@const abs = row.off + i}
              <b
                class:hdr={abs < 8}
                class:page={archField === 'pageId' && abs < 4}
                class:slot={archField === 'slotId' && abs >= 4 && abs < 8}
                class:body={archField === 'payload' && abs >= 8}>{c}</b
              >
            {/each}
          </span>
          <span class="dasc">{row.ascii}</span>
        </div>
      {/each}
    </div>
  {/if}
</div>

<style>
  .lens {
    --tint: var(--mint);
    display: flex;
    flex-direction: column;
    min-height: 0;
  }
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 13px 16px;
    border-bottom: 1px solid var(--line);
  }
  .lba {
    font-family: var(--font-mono);
    font-size: 13px;
    color: var(--sakura-hi);
    letter-spacing: 0.06em;
  }
  .idle-state {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    text-align: center;
    padding: 42px 18px;
    gap: 12px;
  }
  .idle-icon {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 48px;
    height: 48px;
    border-radius: 12px;
    background: #141124;
    border: 1px solid var(--line);
    color: var(--mint);
  }
  .idle-title {
    font-size: 14.5px;
    font-weight: 700;
    color: var(--ink-dim);
  }
  .idle-desc {
    font-size: 12.5px;
    line-height: 1.45;
    color: var(--ink-mute);
    max-width: 250px;
  }

  .meta {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 1px;
    background: var(--line);
    border-bottom: 1px solid var(--line);
  }
  .meta span {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 10px;
    padding: 9px 14px;
    background: var(--panel);
  }
  .meta i {
    font-family: var(--font-mono);
    font-style: normal;
    font-size: 10.5px;
    letter-spacing: 0.14em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
  .meta b {
    font-family: var(--font-mono);
    font-size: 15.5px;
    color: var(--ink);
  }

  .ascii {
    padding: 12px 14px;
    border-bottom: 1px solid var(--line);
  }
  .label {
    font-family: var(--font-mono);
    font-size: 10.5px;
    letter-spacing: 0.16em;
    color: var(--ink-mute);
  }
  .ascii p {
    margin-top: 5px;
    font-family: var(--font-mono);
    font-size: 13.5px;
    line-height: 1.5;
    color: var(--mint-hi);
    word-break: break-all;
    max-height: 80px;
    overflow-y: auto;
  }

  .dump {
    padding: 12px 14px 14px;
    font-family: var(--font-mono);
    font-size: 12.5px;
    line-height: 1.8;
    max-height: 260px;
    overflow: auto;
  }
  .drow {
    display: flex;
    gap: 12px;
    white-space: nowrap;
  }
  .doff {
    color: var(--ink-mute);
  }
  .dhex {
    display: flex;
    gap: 5px;
    color: var(--ink-dim);
  }
  .dhex b {
    font-weight: 400;
  }
  .dhex b.hdr {
    color: var(--sakura-hi);
  }
  .dhex b.page {
    color: var(--gold);
    font-weight: 700;
  }
  .dhex b.slot {
    color: var(--mint-hi);
    font-weight: 700;
  }
  .dhex b.body {
    color: var(--lavender);
  }
  .dasc {
    color: var(--ink-mute);
    letter-spacing: 0.08em;
  }
</style>
