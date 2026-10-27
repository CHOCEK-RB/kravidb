/// \file
/// \brief Buffer pool contract: pinned pages with content locks.

export module storage.buffer_pool;

import std;
export import storage.relation_file;
export import storage.page;

export namespace kravidb::storage {

/// \brief Identifies a block of a relation inside the buffer pool.
struct BufferTag {
  RelFileNode rel{};
  Fork fork{Fork::main};
  BlockNumber block{0};

  /// \brief Two tags address the same block.
  auto operator==(const BufferTag&) const -> bool = default;
};

/// \brief Counters exposed by the buffer pool.
struct BufferStats {
  std::size_t hits{0};
  std::size_t misses{0};
  std::size_t evictions{0};
  std::size_t writes{0};
  std::size_t fsyncs{0};
  std::size_t dirty_pages{0};
};

/// \brief Pinned page with an explicit content lock.
///
/// Destruction releases the content lock and unpins the buffer. A guard is
/// obtained from `BufferPool::read` or `BufferPool::new_page`.
class PageGuard {
  public:
  PageGuard() = default;
  PageGuard(const PageGuard&) = delete;
  auto operator=(const PageGuard&) -> PageGuard& = delete;
  PageGuard(PageGuard&&) = delete;
  auto operator=(PageGuard&&) -> PageGuard& = delete;
  virtual ~PageGuard() = default;

  /// \brief Page contents; valid while the guard is alive.
  [[nodiscard]] virtual auto page() noexcept -> Page& = 0;
  /// \brief Takes the content lock in shared mode.
  virtual auto lock_shared() -> void = 0;
  /// \brief Takes the content lock in exclusive mode.
  virtual auto lock_exclusive() -> void = 0;
  /// \brief Marks the page dirty so it is written before eviction.
  virtual auto mark_dirty() -> void = 0;
};

/// \brief Contract of the buffer pool.
class BufferPool {
  public:
  BufferPool() = default;
  BufferPool(const BufferPool&) = delete;
  auto operator=(const BufferPool&) -> BufferPool& = delete;
  BufferPool(BufferPool&&) = delete;
  auto operator=(BufferPool&&) -> BufferPool& = delete;
  virtual ~BufferPool() = default;

  /// \brief Pins `tag`, loading it from the source on a miss.
  [[nodiscard]] virtual auto read(BufferTag tag) -> std::unique_ptr<PageGuard> = 0;

  /// \brief Pins a zero-filled page for `tag`, ready to be written.
  [[nodiscard]] virtual auto new_page(BufferTag tag) -> std::unique_ptr<PageGuard> = 0;

  /// \brief Flushes every dirty page and fsyncs the underlying source.
  virtual auto flush_all() -> void = 0;

  /// \brief Accumulated buffer counters.
  [[nodiscard]] virtual auto stats() const noexcept -> BufferStats = 0;
};

}  // namespace kravidb::storage
