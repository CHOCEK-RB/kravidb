import { SvelteDate } from 'svelte/reactivity'
import { api } from '$lib/api/client'
import type { BTreeNode, SearchMetrics, SlottedPageData } from '$lib/api/types'

export interface LogEntry {
  id: string
  time: string
  type: 'insert' | 'split' | 'search' | 'info' | 'match' | 'miss'
  title: string
  detail: string
}

/** Marca temporal de la última escritura, usada por la losa para animar la caída. */
export interface InsertMark {
  pageId: number
  slotId: number
  key: number
  at: number
}

/** Hallazgo de una clave por Index Scan: ilumina la losa y la tupla destino (RowID). */
export interface LocateMark {
  pageId: number
  slotId: number
  key: number
  at: number
}

/** Estado de una carga masiva: progreso, divisiones y throughput medido. */
export interface BulkState {
  current: number
  total: number
  splits: number
  elapsedMs: number
  opsPerSec: number
  done: boolean
}

/** Campo de la anatomía de tupla que el cursor señala (sincroniza con la lente de bytes). */
export type ArchField = 'pageId' | 'slotId' | 'payload'

export class EngineStore {
  public tree = $state<BTreeNode | null>(null)
  public pages = $state<SlottedPageData[]>([])
  public currentPage = $state<SlottedPageData | null>(null)
  public selectedPageId = $state<number>(1)
  public selectedSlotId = $state<number | null>(null)
  public searchMetrics = $state<SearchMetrics | null>(null)
  public activeStepIndex = $state<number>(-1)
  public isSearching = $state<boolean>(false)
  public highlightedNodeId = $state<string | null>(null)
  public highlightedKey = $state<number | null>(null)
  public lastSplitOccurred = $state<boolean>(false)
  public promotedKey = $state<number | null>(null)
  /** Página que acaba de desbordar: dispara la sacudida de tensión. */
  public strainedPageId = $state<number | null>(null)
  /** Última escritura: dispara la caída + asentamiento de la tupla. */
  public lastInsert = $state<InsertMark | null>(null)
  /** La raíz acaba de dividirse: el árbol gana un nivel (crecimiento en altura). */
  public lastNewRoot = $state<boolean>(false)
  /** El Index Scan acaba de localizar una clave: resalta su RowID en la losa física. */
  public located = $state<LocateMark | null>(null)
  /** Progreso del lote (playback): `current/total` mientras se inserta en serie. */
  public batch = $state<{ current: number; total: number } | null>(null)
  /** Carga masiva en curso o recién terminada (progreso + throughput). */
  public bulk = $state<BulkState | null>(null)
  /** Modo masa: el árbol es demasiado grande para el render interactivo. */
  public massMode = $state<boolean>(false)
  /** Campo resaltado en el panel de anatomía (sincroniza con la lente de bytes). */
  public archField = $state<ArchField | null>(null)
  /** Grado mínimo del árbol: cada nodo guarda hasta 2t-1 claves. */
  public degree = $state<number>(2)
  public playbackSpeed = $state<number>(1)
  public isLoading = $state<boolean>(false)
  public logs = $state<LogEntry[]>([])

  private strainTimer: ReturnType<typeof setTimeout> | null = null
  private insertTimer: ReturnType<typeof setTimeout> | null = null
  private rootTimer: ReturnType<typeof setTimeout> | null = null
  private locateTimer: ReturnType<typeof setTimeout> | null = null

  constructor() {
    void this.bootstrap()
  }

  /** Lee la configuracion del motor y arranca el estado inicial. */
  private async bootstrap() {
    try {
      const config = await api.getConfig()
      this.degree = config.degree
    } catch {
      // Sin configuracion disponible se conserva el grado por defecto.
    }
    await this.init()
  }

  private getTime(): string {
    const d = new SvelteDate()
    return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}.${d.getMilliseconds().toString().padStart(3, '0')}`
  }

  public addLog(type: LogEntry['type'], title: string, detail: string) {
    const entry: LogEntry = {
      id: `log-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
      time: this.getTime(),
      type,
      title,
      detail,
    }
    this.logs = [entry, ...this.logs.slice(0, 49)]
  }

  /** Ids de página referenciados por el árbol, ordenados. */
  public get pageIds(): number[] {
    const ids: number[] = []
    const add = (id: number) => {
      if (!ids.includes(id)) ids.push(id)
    }
    const walk = (node: BTreeNode | null) => {
      if (!node) return
      for (const k of node.keys) add(k.page_id)
      for (const child of node.children) walk(child)
    }
    walk(this.tree)
    if (ids.length === 0) add(1)
    add(this.selectedPageId)
    return ids.sort((a, b) => a - b)
  }

  public get totalKeys(): number {
    let n = 0
    const walk = (node: BTreeNode | null) => {
      if (!node) return
      n += node.keys.length
      for (const child of node.children) walk(child)
    }
    walk(this.tree)
    return n
  }

  public get treeDepth(): number {
    let depth = 0
    let node = this.tree
    while (node && node.children.length > 0) {
      depth += 1
      node = node.children[0]
    }
    return this.tree ? depth + 1 : 0
  }

  public get treeNodeCount(): number {
    let n = 0
    const walk = (node: BTreeNode | null) => {
      if (!node) return
      n += 1
      for (const child of node.children) walk(child)
    }
    walk(this.tree)
    return n
  }

  public async init() {
    await this.refreshTree()
    await this.refreshPages()
    this.addLog(
      'info',
      'Motor Inicializado',
      `Árbol B (t=${this.degree}) y buffer de páginas de 4 KB cargados en memoria.`,
    )
  }

  public async refreshTree() {
    this.isLoading = true
    try {
      this.tree = await api.getBTree()
    } finally {
      this.isLoading = false
    }
  }

  public async refreshPages() {
    // En modo masa no se materializan las losas: con t=2 serían miles.
    if (this.massMode) return
    const ids = this.pageIds
    const loaded = await Promise.all(ids.map((id) => api.getPage(id)))
    this.pages = loaded
    if (this.currentPage) {
      this.currentPage = loaded.find((p) => p.page_id === this.selectedPageId) ?? this.currentPage
    }
  }

  public async selectPage(pageId: number, slotId: number | null = null) {
    this.selectedPageId = pageId
    this.selectedSlotId = slotId
    this.currentPage = await api.getPage(pageId)
    if (!this.pages.some((p) => p.page_id === pageId)) {
      await this.refreshPages()
    }
  }

  public selectSlot(pageId: number, slotId: number | null) {
    if (this.selectedPageId !== pageId) {
      this.selectedPageId = pageId
      void this.selectPage(pageId, slotId)
      return
    }
    this.selectedSlotId = slotId
  }

  public async insertKey(key: number, payload: string) {
    this.isLoading = true
    const previousIds = this.pageIds
    try {
      const result = await api.insertKey(key, payload)
      this.lastSplitOccurred = result.split_occurred
      this.promotedKey = result.promoted_key ?? null
      if (result.new_root_created) this.markRoot()

      if (result.split_occurred) {
        const median = result.promoted_key ?? '—'
        this.addLog(
          'split',
          `División de Nodo [Clave ${key}]`,
          `Nodo saturado (2t-1 = ${2 * this.degree - 1} claves). Mediana ${median} promovida al padre. Nueva raíz: ${result.new_root_created ? 'sí' : 'no'}.`,
        )
      } else {
        this.addLog(
          'insert',
          `Clave ${key} escrita`,
          `Registro asentado en Página #${result.target_page_id}, Slot #${result.target_slot_id}.`,
        )
      }

      await this.refreshTree()

      // Escenificación en dos tiempos: primero corre la animación estructural
      // (mediana, split, crecimiento en altura) y recién después aterriza la
      // tupla en la losa, para que la primera vista no se lea de golpe.
      if (result.split_occurred) {
        await new Promise((r) => setTimeout(r, 900 / this.playbackSpeed))
      }

      this.selectedPageId = result.target_page_id
      await this.refreshPages()

      // ¿Se materializó una losa nueva? La anterior fue la que desbordó.
      const previousMax = previousIds.length ? Math.max(...previousIds) : 1
      if (!previousIds.includes(result.target_page_id) && result.target_page_id !== previousMax) {
        this.strain(previousMax)
      }

      this.markInsert(result.target_page_id, result.target_slot_id, key)
      this.selectedSlotId = result.target_slot_id
      return result
    } finally {
      this.isLoading = false
    }
  }

  private strain(pageId: number) {
    if (this.strainTimer) clearTimeout(this.strainTimer)
    this.strainedPageId = pageId
    this.strainTimer = setTimeout(() => {
      this.strainedPageId = null
    }, 900)
  }

  private markInsert(pageId: number, slotId: number, key: number) {
    if (this.insertTimer) clearTimeout(this.insertTimer)
    this.lastInsert = { pageId, slotId, key, at: Date.now() }
    this.insertTimer = setTimeout(() => {
      this.lastInsert = null
    }, 1400)
  }

  /** Marca el instante en que nace una raíz nueva (un nivel más de altura). */
  private markRoot() {
    if (this.rootTimer) clearTimeout(this.rootTimer)
    this.lastNewRoot = true
    this.rootTimer = setTimeout(() => {
      this.lastNewRoot = false
    }, 1200)
  }

  /** Marca el RowID localizado por el Index Scan para iluminarlo en la losa. */
  private locate(pageId: number, slotId: number, key: number) {
    if (this.locateTimer) clearTimeout(this.locateTimer)
    this.located = { pageId, slotId, key, at: Date.now() }
    this.locateTimer = setTimeout(() => {
      this.located = null
    }, 2400)
  }

  public async executeSearch(key: number) {
    this.isSearching = true
    this.searchMetrics = null
    this.activeStepIndex = -1
    this.highlightedNodeId = null
    this.highlightedKey = null
    this.selectedSlotId = null

    this.addLog(
      'search',
      `Búsqueda O(log n) [Clave ${key}]`,
      `Descendiendo por punteros de nodo en disco.`,
    )

    const metrics = await api.searchKey(key)
    this.searchMetrics = metrics

    const delay = 350 / this.playbackSpeed
    for (let i = 0; i < metrics.path.length; i += 1) {
      this.activeStepIndex = i
      this.highlightedNodeId = metrics.path[i].node_id
      await new Promise((r) => setTimeout(r, delay))
    }

    if (metrics.found) {
      this.highlightedKey = key
      this.addLog(
        'match',
        `Coincidencia: clave ${key}`,
        `Latencia ${metrics.index_scan.latency_ns} ns con ${metrics.index_scan.page_ios} I/O frente a ${metrics.full_scan.latency_ns} µs del Full Scan (~${Math.round((metrics.full_scan.latency_ns * 1000) / Math.max(1, metrics.index_scan.latency_ns))}× más rápido).`,
      )
      if (metrics.target && !this.massMode) {
        await this.selectPage(metrics.target.page_id, metrics.target.slot_id)
        this.locate(metrics.target.page_id, metrics.target.slot_id, key)
      } else if (metrics.target) {
        this.highlightedKey = key
      }
    } else {
      this.highlightedKey = key
      this.addLog(
        'miss',
        `Clave ${key} no encontrada`,
        `Se recorrieron ${metrics.index_scan.nodes_visited} nodos hasta la hoja sin coincidencia.`,
      )
    }

    this.isSearching = false
    return metrics
  }

  /**
   * Carga masiva en bloque: inserta N tuplas sin materializar el render por
   * operación (con t=2, N=50k daría ~25k nodos y ~4k losas). Cede al event
   * loop cada trozo para mantener la UI viva y reporta throughput real.
   */
  public async bulkLoad(total: number) {
    if (this.isLoading) return
    this.isLoading = true
    this.massMode = false
    this.batch = null
    const started = performance.now()
    let splits = 0
    this.bulk = { current: 0, total, splits: 0, elapsedMs: 0, opsPerSec: 0, done: false }
    this.addLog(
      'info',
      `Carga masiva [N = ${total.toLocaleString('es-ES')}]`,
      'Inserción en bloque: se miden divisiones, altura y throughput real.',
    )

    // Permutación sembrada: claves distintas en orden aleatorio (fuerza splits variados).
    const order = Array.from({ length: total }, (_, i) => i)
    let seed = 0x9e3779b9
    const rnd = () => {
      seed = (seed + 0x6d2b79f5) | 0
      let x = Math.imul(seed ^ (seed >>> 15), 1 | seed)
      x = (x + Math.imul(x ^ (x >>> 7), 61 | x)) ^ x
      return ((x ^ (x >>> 14)) >>> 0) / 4294967296
    }
    for (let i = order.length - 1; i > 0; i -= 1) {
      const j = Math.floor(rnd() * (i + 1))
      const tmp = order[i]
      order[i] = order[j]
      order[j] = tmp
    }

    const CHUNK = 400
    let current = 0
    try {
      while (current < total) {
        const end = Math.min(current + CHUNK, total)
        for (; current < end; current += 1) {
          const key = 100000 + order[current]
          const payload = `{"id":${key},"table":"users","name":"row-${key}","bio":"${'x'.repeat(120)}"}`
          const res = await api.insertKey(key, payload)
          if (res.split_occurred) splits += 1
        }
        const elapsed = performance.now() - started
        this.bulk = {
          current,
          total,
          splits,
          elapsedMs: elapsed,
          opsPerSec: Math.round((current / Math.max(1, elapsed)) * 1000),
          done: false,
        }
        await new Promise((r) => setTimeout(r, 0))
      }

      await this.refreshTree()
      const elapsed = performance.now() - started
      this.bulk = {
        current: total,
        total,
        splits,
        elapsedMs: elapsed,
        opsPerSec: Math.round((total / Math.max(1, elapsed)) * 1000),
        done: true,
      }
      this.massMode = true
      this.addLog(
        'insert',
        'Carga masiva completada',
        `${total.toLocaleString('es-ES')} tuplas en ${Math.round(elapsed)} ms · ${splits.toLocaleString('es-ES')} divisiones · ${this.bulk.opsPerSec.toLocaleString('es-ES')} ops/s.`,
      )
    } finally {
      this.isLoading = false
    }
  }

  /** Reconstruye el árbol con otro grado t conservando las tuplas almacenadas. */
  public async setDegree(degree: number) {
    if (degree === this.degree || this.isLoading) {
      return
    }
    this.isLoading = true
    try {
      const config = await api.setDegree(degree)
      this.degree = config.degree
      this.searchMetrics = null
      this.activeStepIndex = -1
      this.highlightedNodeId = null
      this.highlightedKey = null
      this.lastSplitOccurred = false
      this.promotedKey = null
      this.lastNewRoot = false
      this.located = null
      this.selectedSlotId = null
      await this.refreshTree()
      await this.refreshPages()
      this.addLog(
        'info',
        `Árbol reconstruido con t = ${this.degree}`,
        `Capacidad por nodo: ${2 * this.degree - 1} claves y ${2 * this.degree} hijos. Las tuplas se conservan.`,
      )
    } finally {
      this.isLoading = false
    }
  }

  public async resetEngine() {
    await api.reset()
    this.searchMetrics = null
    this.activeStepIndex = -1
    this.highlightedNodeId = null
    this.highlightedKey = null
    this.selectedSlotId = null
    this.lastSplitOccurred = false
    this.promotedKey = null
    this.strainedPageId = null
    this.lastInsert = null
    this.lastNewRoot = false
    this.located = null
    this.batch = null
    this.bulk = null
    this.massMode = false
    this.archField = null
    this.logs = []
    this.selectedPageId = 1
    await this.refreshTree()
    await this.refreshPages()
    this.addLog('info', 'Reinicio del motor', 'Árbol B y páginas restablecidos al estado semilla.')
  }
}

export const engine = new EngineStore()
