import type {
  ApiClient,
  BTreeKey,
  BTreeNode,
  EngineConfig,
  InsertResult,
  PageHeader,
  PageSlot,
  PageTuple,
  SearchMetrics,
  SearchPathStep,
  SlottedPageData,
} from './types'

export class MockEngine implements ApiClient {
  private degree = 2 // Grado mínimo t = 2 (máximo 2t - 1 = 3 claves por nodo)
  private root: BTreeNode
  private nodeCounter = 0
  private pages: Map<number, SlottedPageData> = new Map()
  private currentPageId = 1
  private totalTuplesCount = 0
  private splitCount = 0
  private lastPromotedKey: number | undefined

  constructor() {
    this.root = this.createEmptyNode(true)
    this.initPage(1)
    this.seedInitialData()
  }

  private nextNodeId(): string {
    this.nodeCounter += 1
    return `node-${this.nodeCounter}`
  }

  private createEmptyNode(isLeaf: boolean): BTreeNode {
    return {
      id: this.nextNodeId(),
      keys: [],
      children: [],
      is_leaf: isLeaf,
    }
  }

  private initPage(pageId: number): SlottedPageData {
    const header: PageHeader = {
      page_id: pageId,
      lsn: 1000 + pageId,
      slot_count: 0,
      free_space_offset: 64, // Cabecera fija de 64 bytes
      free_space_end: 4096, // Fondo de la página de 4KB
    }
    const page: SlottedPageData = {
      page_id: pageId,
      header,
      slots: [],
      tuples: [],
      total_bytes: 4096,
      free_bytes: 4096 - 64,
    }
    this.pages.set(pageId, page)
    return page
  }

  private appendToSlottedPage(key: number, payload: string): { page_id: number; slot_id: number } {
    let page = this.pages.get(this.currentPageId)
    if (!page) {
      page = this.initPage(this.currentPageId)
    }

    const tupleBytes = 8 + payload.length // 8 bytes clave + tamaño payload
    const neededBytes = 4 + tupleBytes // 4 bytes slot + datos

    if (page.free_bytes < neededBytes) {
      this.currentPageId += 1
      page = this.initPage(this.currentPageId)
    }

    const slotId = page.header.slot_count
    const targetOffset = page.header.free_space_end - tupleBytes

    const slot: PageSlot = {
      slot_id: slotId,
      offset: targetOffset,
      length: tupleBytes,
      is_deleted: false,
    }

    const tuple: PageTuple = {
      slot_id: slotId,
      offset: targetOffset,
      key,
      data: payload,
      size_bytes: tupleBytes,
    }

    page.slots.push(slot)
    page.tuples.push(tuple)
    page.header.slot_count += 1
    page.header.free_space_offset += 4
    page.header.free_space_end = targetOffset
    page.free_bytes = page.header.free_space_end - page.header.free_space_offset
    this.totalTuplesCount += 1

    return { page_id: page.page_id, slot_id: slotId }
  }

  private splitChild(parent: BTreeNode, childIndex: number) {
    const t = this.degree
    const fullChild = parent.children[childIndex]
    const newSibling = this.createEmptyNode(fullChild.is_leaf)

    // Clave mediana que sube al padre
    const medianKey = fullChild.keys[t - 1]

    // Rastro observable para la capa visual (mediana promovida y nº de divisiones)
    this.lastPromotedKey = medianKey.key
    this.splitCount += 1

    // newSibling toma las claves a la derecha de la mediana
    newSibling.keys = fullChild.keys.slice(t)
    // fullChild se queda con las claves a la izquierda
    fullChild.keys = fullChild.keys.slice(0, t - 1)

    // Si no es hoja, repartir hijos
    if (!fullChild.is_leaf) {
      newSibling.children = fullChild.children.slice(t)
      fullChild.children = fullChild.children.slice(0, t)
    }

    // Insertar hermano en la lista de hijos del padre
    parent.children.splice(childIndex + 1, 0, newSibling)
    // Insertar mediana en las claves del padre
    parent.keys.splice(childIndex, 0, medianKey)
  }

  private insertNonFull(node: BTreeNode, keyItem: BTreeKey) {
    let i = node.keys.length - 1

    if (node.is_leaf) {
      while (i >= 0 && keyItem.key < node.keys[i].key) {
        i -= 1
      }
      node.keys.splice(i + 1, 0, keyItem)
    } else {
      while (i >= 0 && keyItem.key < node.keys[i].key) {
        i -= 1
      }
      i += 1

      if (node.children[i].keys.length === 2 * this.degree - 1) {
        this.splitChild(node, i)
        if (keyItem.key > node.keys[i].key) {
          i += 1
        }
      }
      this.insertNonFull(node.children[i], keyItem)
    }
  }

  public async getBTree(): Promise<BTreeNode> {
    return JSON.parse(JSON.stringify(this.root))
  }

  public async insertKey(key: number, payload: string): Promise<InsertResult> {
    // Si la clave ya existe, actualizar el registro en disco sin corromper el árbol
    const existing = await this.searchKey(key)
    if (existing.found && existing.target) {
      existing.target.value = payload
      const p = this.pages.get(existing.target.page_id)
      if (p) {
        const tup = p.tuples.find((t) => t.slot_id === existing.target!.slot_id)
        if (tup) tup.data = payload
      }
      return {
        split_occurred: false,
        promoted_key: undefined,
        new_root_created: false,
        target_page_id: existing.target.page_id,
        target_slot_id: existing.target.slot_id,
      }
    }

    const inserted = this.insertPair(key, payload)

    return {
      split_occurred: this.splitCount > 0,
      promoted_key: this.lastPromotedKey,
      new_root_created: inserted.new_root_created,
      target_page_id: inserted.page_id,
      target_slot_id: inserted.slot_id,
    }
  }

  // Inserta una tupla nueva (pagina + arbol) sin comprobar duplicados.
  private insertPair(
    key: number,
    payload: string,
  ): { page_id: number; slot_id: number; new_root_created: boolean } {
    const loc = this.appendToSlottedPage(key, payload)
    const keyItem: BTreeKey = {
      key,
      value: payload,
      page_id: loc.page_id,
      slot_id: loc.slot_id,
    }

    this.splitCount = 0
    this.lastPromotedKey = undefined

    const t = this.degree
    let newRootCreated = false
    if (this.root.keys.length === 2 * t - 1) {
      const oldRoot = this.root
      const newRoot = this.createEmptyNode(false)
      newRoot.children.push(oldRoot)

      this.splitChild(newRoot, 0)
      this.root = newRoot
      newRootCreated = true

      let i = 0
      if (newRoot.keys[0].key < keyItem.key) {
        i += 1
      }
      this.insertNonFull(newRoot.children[i], keyItem)
    } else {
      this.insertNonFull(this.root, keyItem)
    }

    return { page_id: loc.page_id, slot_id: loc.slot_id, new_root_created: newRootCreated }
  }

  public async searchKey(searchKey: number): Promise<SearchMetrics> {
    const path: SearchPathStep[] = []
    let current: BTreeNode | null = this.root
    let foundKey: BTreeKey | undefined
    let nodesVisited = 0

    while (current) {
      nodesVisited += 1
      let i = 0
      let match = false

      while (i < current.keys.length) {
        const item = current.keys[i]
        if (searchKey === item.key) {
          path.push({ node_id: current.id, key_index_checked: i, comparison: 'equal' })
          foundKey = item
          match = true
          break
        }
        if (searchKey < item.key) {
          path.push({ node_id: current.id, key_index_checked: i, comparison: 'less' })
          break
        }
        path.push({ node_id: current.id, key_index_checked: i, comparison: 'greater' })
        i += 1
      }

      if (match) break

      if (current.is_leaf) {
        current = null
      } else {
        current = current.children[i] || null
      }
    }

    // Cálculo analítico de métricas: O(log n) vs O(n)
    const indexLatency = Math.round(180 + nodesVisited * 90 + Math.random() * 40) // ~270-450 ns
    const fullScanLatency = Math.max(
      12,
      Math.round(this.totalTuplesCount * 1.8 + this.pages.size * 8.5),
    ) // µs

    return {
      key: searchKey,
      found: Boolean(foundKey),
      target: foundKey,
      path,
      index_scan: {
        latency_ns: indexLatency,
        page_ios: Math.min(nodesVisited, 4),
        nodes_visited: nodesVisited,
      },
      full_scan: {
        latency_ns: fullScanLatency,
        page_ios: this.pages.size,
        tuples_scanned: this.totalTuplesCount,
      },
    }
  }

  public async getPage(pageId: number): Promise<SlottedPageData> {
    const page = this.pages.get(pageId)
    if (!page) {
      return this.initPage(pageId)
    }
    return JSON.parse(JSON.stringify(page))
  }

  public async getConfig(): Promise<EngineConfig> {
    return { degree: this.degree, page_size: 4096 }
  }

  public async setDegree(degree: number): Promise<EngineConfig> {
    // Reconstruye el arbol con el nuevo grado conservando las tuplas.
    const rows = this.collectTuples()
    this.degree = degree
    this.root = this.createEmptyNode(true)
    this.pages.clear()
    this.currentPageId = 1
    this.totalTuplesCount = 0
    this.splitCount = 0
    this.lastPromotedKey = undefined
    this.initPage(1)
    for (const row of rows) {
      this.insertPair(row.key, row.value)
    }
    return this.getConfig()
  }

  // Pares clave/valor en orden de insercion, para reconstruir el arbol.
  private collectTuples(): { key: number; value: string }[] {
    const rows: { key: number; value: string }[] = []
    for (const page of this.pages.values()) {
      for (const tuple of page.tuples) {
        rows.push({ key: tuple.key, value: tuple.data })
      }
    }
    return rows
  }

  public async reset(): Promise<void> {
    this.root = this.createEmptyNode(true)
    this.pages.clear()
    this.currentPageId = 1
    this.totalTuplesCount = 0
    this.initPage(1)
    this.seedInitialData()
  }

  private seedInitialData() {
    const people = [
      { key: 10, name: 'Ada Lovelace' },
      { key: 20, name: 'Alan Turing' },
      { key: 5, name: 'Grace Hopper' },
      { key: 15, name: 'Linus Torvalds' },
      { key: 25, name: 'Dennis Ritchie' },
      { key: 30, name: 'Ken Thompson' },
      { key: 35, name: 'Edsger Dijkstra' },
    ]
    // Payload ~300 B para que una losa de 4 KB aloje ~12 tuplas, como en disco.
    const seeds = people.map(({ key, name }) => ({
      key,
      val: JSON.stringify({ id: key, table: 'users', name, bio: 'x'.repeat(255) }),
    }))
    for (const item of seeds) {
      this.insertPair(item.key, item.val)
    }
  }
}

export const mockClient = new MockEngine()
