import { mockClient } from './mock'
import type { ApiClient, BTreeNode, InsertResult, SearchMetrics, SlottedPageData } from './types'

class HttpApiClient implements ApiClient {
  private baseUrl: string

  constructor(baseUrl = 'http://localhost:8080') {
    this.baseUrl = baseUrl
  }

  public async getBTree(): Promise<BTreeNode> {
    const res = await fetch(`${this.baseUrl}/api/v1/btree`)
    if (!res.ok) throw new Error(`HTTP Error: ${res.status}`)
    return res.json()
  }

  public async insertKey(key: number, payload: string): Promise<InsertResult> {
    const res = await fetch(`${this.baseUrl}/api/v1/btree/insert`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ key, payload }),
    })
    if (!res.ok) throw new Error(`HTTP Error: ${res.status}`)
    return res.json()
  }

  public async searchKey(key: number): Promise<SearchMetrics> {
    const res = await fetch(`${this.baseUrl}/api/v1/btree/search?key=${key}`)
    if (!res.ok) throw new Error(`HTTP Error: ${res.status}`)
    return res.json()
  }

  public async getPage(pageId: number): Promise<SlottedPageData> {
    const res = await fetch(`${this.baseUrl}/api/v1/page/${pageId}`)
    if (!res.ok) throw new Error(`HTTP Error: ${res.status}`)
    return res.json()
  }

  public async reset(): Promise<void> {
    const res = await fetch(`${this.baseUrl}/api/v1/reset`, { method: 'POST' })
    if (!res.ok) throw new Error(`HTTP Error: ${res.status}`)
  }
}

const useMock = import.meta.env.VITE_USE_MOCK !== 'false'
export const api: ApiClient = useMock ? mockClient : new HttpApiClient()
