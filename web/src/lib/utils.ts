/**
 * Rellena un payload hasta un tamaño realista (~300 B) para que la losa de 4 KB
 * se llene a un ritmo comparable al de una página real en disco.
 */
export function padPayload(key: number, raw: string, target = 300): string {
  let body = raw.trim()
  if (!body.startsWith('{')) body = JSON.stringify({ role: body || 'active' })
  try {
    const obj = JSON.parse(body) as Record<string, unknown>
    let size = body.length
    let i = 0
    while (size < target) {
      const filler = `payload_${i}`
      obj.pad = obj.pad ? `${obj.pad} ${filler}` : filler
      body = JSON.stringify({ id: key, ...obj })
      size = body.length
      i += 1
    }
    return JSON.stringify({ id: key, ...obj })
  } catch {
    return JSON.stringify({ id: key, text: body, pad: 'x'.repeat(target) })
  }
}
