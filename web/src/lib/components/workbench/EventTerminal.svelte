<script lang="ts">
  import type { LogEntry } from '$lib/stores/engine.svelte'
  import { Trash, ScrollText } from '@lucide/svelte'

  interface Props {
    logs: LogEntry[]
    onClearLogs: () => void
  }

  let { logs, onClearLogs }: Props = $props()

  type Filter = 'all' | 'split' | 'match' | 'insert' | 'search' | 'miss'
  let filterType = $state<Filter>('all')

  const filteredLogs = $derived(
    filterType === 'all' ? logs : logs.filter((l) => l.type === filterType),
  )

  const TONE: Record<string, string> = {
    split: 'var(--ember)',
    match: 'var(--mint)',
    miss: 'var(--gold)',
    insert: 'var(--sakura)',
    search: 'var(--mint)',
    info: 'var(--ink-mute)',
  }
  const LABEL: Record<string, string> = {
    split: 'SPLIT',
    match: 'HIT',
    miss: 'MISS',
    insert: 'WRITE',
    search: 'SCAN',
    info: 'INFO',
  }

  const filters: { id: Filter; label: string }[] = [
    { id: 'all', label: 'Todo' },
    { id: 'split', label: 'Splits' },
    { id: 'search', label: 'Scans' },
    { id: 'miss', label: 'Misses' },
  ]
</script>

<div class="panel term">
  <header class="head">
    <span class="panel-title">
      <ScrollText size={13} />
      Bitácora
      <span class="count">{logs.length}</span>
    </span>
    <div class="tools">
      <div class="chips">
        {#each filters as f (f.id)}
          <button class="chip" class:on={filterType === f.id} onclick={() => (filterType = f.id)}>
            {f.label}
          </button>
        {/each}
      </div>
      <button class="clear" onclick={onClearLogs} aria-label="Limpiar bitácora">
        <Trash size={13} />
      </button>
    </div>
  </header>

  <div class="stream">
    {#if filteredLogs.length === 0}
      <p class="empty">Sin eventos en el búfer.</p>
    {:else}
      {#each filteredLogs as log (log.id)}
        <div class="event" style="--tone: {TONE[log.type] ?? TONE.info}">
          <span class="badge">{LABEL[log.type] ?? 'INFO'}</span>
          <div class="body">
            <div class="row">
              <strong>{log.title}</strong>
              <time>{log.time}</time>
            </div>
            <p>{log.detail}</p>
          </div>
        </div>
      {/each}
    {/if}
  </div>
</div>

<style>
  .term {
    --tint: var(--sakura);
    display: flex;
    flex-direction: column;
    min-height: 200px;
  }
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 13px 16px;
    border-bottom: 1px solid var(--line);
  }
  .count {
    padding: 2px 7px;
    border-radius: 99px;
    background: var(--panel-3);
    color: var(--ink-dim);
    font-size: 11px;
    letter-spacing: 0.05em;
  }
  .tools {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .chips {
    display: flex;
    gap: 3px;
    padding: 3px;
    border-radius: 9px;
    border: 1px solid var(--line);
    background: #0f0e0b;
  }
  .chip {
    border: 0;
    background: transparent;
    color: var(--ink-mute);
    font-size: 11.5px;
    padding: 4px 9px;
    border-radius: 6px;
    cursor: pointer;
    transition:
      background 150ms ease,
      color 150ms ease;
  }
  .chip:hover {
    color: var(--ink-dim);
  }
  .chip.on {
    background: var(--panel-3);
    color: var(--ink);
    font-weight: 600;
  }
  .clear {
    display: grid;
    place-items: center;
    width: 32px;
    height: 32px;
    border-radius: 8px;
    border: 1px solid var(--line);
    background: transparent;
    color: var(--ink-mute);
    cursor: pointer;
    transition:
      color 160ms ease,
      border-color 160ms ease;
  }
  .clear:hover {
    color: var(--ember);
    border-color: color-mix(in srgb, var(--ember) 40%, var(--line));
  }

  .stream {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 12px;
    max-height: 360px;
    min-height: 120px;
    overflow-y: auto;
  }
  .empty {
    padding: 28px 0;
    text-align: center;
    font-family: var(--font-mono);
    font-size: 12px;
    color: var(--ink-mute);
  }

  .event {
    display: flex;
    gap: 10px;
    padding: 9px 12px;
    border-radius: 9px;
    border: 1px solid var(--line);
    border-left: 2px solid var(--tone);
    background: #100e0b;
    animation: log-in 350ms var(--ease-out) both;
  }
  @keyframes log-in {
    from {
      opacity: 0;
      transform: translateX(-8px);
    }
  }
  .badge {
    align-self: flex-start;
    padding: 3px 7px;
    border-radius: 4px;
    background: color-mix(in srgb, var(--tone) 18%, transparent);
    color: var(--tone);
    font-family: var(--font-mono);
    font-size: 10.5px;
    font-weight: 700;
    letter-spacing: 0.1em;
  }
  .body {
    flex: 1;
    min-width: 0;
  }
  .row {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 10px;
  }
  .row strong {
    font-size: 14px;
    font-weight: 600;
    color: var(--ink);
  }
  .row time {
    flex: none;
    font-family: var(--font-mono);
    font-size: 11px;
    color: var(--ink-mute);
  }
  .body p {
    margin-top: 3px;
    font-family: var(--font-mono);
    font-size: 12px;
    line-height: 1.5;
    color: var(--ink-dim);
  }
</style>
