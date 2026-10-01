export interface BTreeKey {
  key: number
  value: string
  page_id: number
  slot_id: number
}

export interface BTreeNode {
  id: string
  keys: BTreeKey[]
  children: BTreeNode[]
  is_leaf: boolean
}

export interface PageHeader {
  page_id: number
  lsn: number
  slot_count: number
  free_space_offset: number
  free_space_end: number
}

export interface PageSlot {
  slot_id: number
  offset: number
  length: number
  is_deleted: boolean
}

export interface PageTuple {
  slot_id: number
  offset: number
  key: number
  data: string
  size_bytes: number
}

export interface SlottedPageData {
  page_id: number
  header: PageHeader
  slots: PageSlot[]
  tuples: PageTuple[]
  total_bytes: number
  free_bytes: number
}

export interface SearchPathStep {
  node_id: string
  key_index_checked: number
  comparison: 'less' | 'equal' | 'greater'
}

export interface SearchMetrics {
  key: number
  found: boolean
  target?: BTreeKey
  path: SearchPathStep[]
  index_scan: {
    latency_ns: number
    page_ios: number
    nodes_visited: number
  }
  full_scan: {
    latency_ns: number
    page_ios: number
    tuples_scanned: number
  }
}

export interface InsertResult {
  split_occurred: boolean
  promoted_key?: number
  new_root_created: boolean
  target_page_id: number
  target_slot_id: number
}

export interface EngineConfig {
  degree: number
  page_size: number
}

export interface ApiClient {
  getBTree(): Promise<BTreeNode>
  insertKey(key: number, payload: string): Promise<InsertResult>
  searchKey(key: number): Promise<SearchMetrics>
  getPage(pageId: number): Promise<SlottedPageData>
  getConfig(): Promise<EngineConfig>
  setDegree(degree: number): Promise<EngineConfig>
  reset(): Promise<void>
}
