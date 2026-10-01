<script lang="ts">
  interface Props {
    totalKeys: number
    nodes: number
    depth: number
    pageCount: number
    selectedPageId: number
    t?: number
  }

  let { totalKeys, nodes, depth, pageCount, selectedPageId, t = 2 }: Props = $props()

  const readouts = $derived([
    { k: 'claves', v: String(totalKeys) },
    { k: 'nodos', v: String(nodes) },
    { k: 'prof.', v: String(depth) },
    { k: 'losas', v: String(pageCount) },
    { k: 'grado t', v: String(t) },
    { k: 'página', v: `#${selectedPageId}` },
  ])
</script>

<div class="bar">
  <span class="live">
    <i></i>
    <span>EN LÍNEA</span>
  </span>

  <div class="reads">
    {#each readouts as r (r.k)}
      <span class="read">
        <b>{r.v}</b>
        <em>{r.k}</em>
      </span>
    {/each}
  </div>
</div>

<style>
  .bar {
    display: flex;
    align-items: center;
    gap: 18px;
    width: 100%;
    max-width: 1920px;
    margin: 0 auto;
    padding: 12px clamp(16px, 2.5vw, 40px);
    flex-wrap: wrap;
  }
  .live {
    display: flex;
    align-items: center;
    gap: 8px;
    font-family: var(--font-mono);
    font-size: 11.5px;
    font-weight: 700;
    letter-spacing: 0.2em;
    color: var(--ok);
  }
  .live i {
    width: 8px;
    height: 8px;
    border-radius: 99px;
    background: var(--ok);
    box-shadow: 0 0 10px var(--ok);
    animation: beat 1.9s ease-in-out infinite;
  }
  @keyframes beat {
    0%,
    100% {
      opacity: 1;
      transform: scale(1);
    }
    50% {
      opacity: 0.4;
      transform: scale(0.75);
    }
  }

  .reads {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 4px 24px;
  }
  .read {
    display: flex;
    align-items: baseline;
    gap: 8px;
    padding-left: 24px;
    border-left: 1px solid var(--line);
  }
  .read:first-child {
    padding-left: 0;
    border-left: 0;
  }
  .read b {
    font-family: var(--font-mono);
    font-size: 21px;
    font-weight: 700;
    color: var(--ink);
    line-height: 1;
    font-variant-numeric: tabular-nums;
  }
  .read em {
    font-family: var(--font-mono);
    font-style: normal;
    font-size: 12px;
    letter-spacing: 0.16em;
    text-transform: uppercase;
    color: var(--ink-mute);
  }
</style>
