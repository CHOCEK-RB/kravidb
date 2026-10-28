module storage.buffer_pool;

import std;
import storage.page;
import storage.page_source;
import storage.relation_file;
import storage.wal;

namespace kravidb::storage {

namespace {

/// \brief Usage count saturates at this value on every pin.
inline constexpr std::uint32_t max_usage_count = 5;

/// \brief Number of full clock passes before declaring every frame pinned.
inline constexpr std::size_t victim_scan_factor = 2;

/// \brief Mix constant used by the buffer tag hash.
inline constexpr std::size_t hash_mix_constant = 0x9E3779B9U;
inline constexpr std::size_t hash_left_shift = 6U;
inline constexpr std::size_t hash_right_shift = 2U;

/// \brief Hash for buffer tags; combines every identity field.
struct BufferTagHash {
  [[nodiscard]] auto operator()(const BufferTag& tag) const noexcept -> std::size_t {
    auto seed = std::hash<std::uint32_t>{}(tag.rel.tablespace);
    const auto combine = [&seed](std::size_t value) noexcept {
      seed ^= value + hash_mix_constant + (seed << hash_left_shift) + (seed >> hash_right_shift);
    };
    combine(std::hash<std::uint32_t>{}(tag.rel.database));
    combine(std::hash<std::uint32_t>{}(tag.rel.relation));
    combine(std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(tag.fork)));
    combine(std::hash<BlockNumber>{}(tag.block));
    return seed;
  }
};

/// \brief One buffer frame; the clock sweep moves over an array of these.
struct BufferDesc {
  BufferTag tag{};
  std::atomic<std::uint32_t> pin_count{0};
  std::atomic<std::uint32_t> usage_count{0};
  std::atomic<bool> dirty{false};
  std::atomic<bool> valid{false};
  std::atomic<bool> io_in_progress{false};
  std::uint64_t page_lsn{0};
  std::shared_mutex content_lock;
  std::mutex state_lock;
  std::condition_variable io_done;
  Page page{};
};

}  // namespace

class ClockSweepBufferPool::Impl {
  public:
  Impl(std::size_t capacity, PageSource& source, Wal& wal) : source_{source}, wal_{wal} {
    if (capacity == 0) {
      throw std::invalid_argument{"buffer pool capacity must be positive"};
    }
    frames_.reserve(capacity);
    for (std::size_t index = 0; index < capacity; ++index) {
      frames_.push_back(std::make_unique<BufferDesc>());
    }
    map_.reserve(capacity);
  }

  class Guard;

  auto read(BufferTag tag) -> std::unique_ptr<PageGuard>;
  auto new_page(BufferTag tag) -> std::unique_ptr<PageGuard>;
  auto flush(BufferTag tag) -> void;
  auto flush_all() -> void;
  [[nodiscard]] auto stats() const noexcept -> BufferStats;

  private:
  friend class Guard;

  [[nodiscard]] auto frame(std::size_t index) noexcept -> BufferDesc& { return *frames_.at(index); }
  [[nodiscard]] auto frame(std::size_t index) const noexcept -> const BufferDesc& {
    return *frames_.at(index);
  }

  auto pin(BufferDesc& desc) noexcept -> void {
    if (desc.pin_count.fetch_add(1, std::memory_order_relaxed) == 0) {
      pinned_pages_.fetch_add(1, std::memory_order_relaxed);
    }
    auto usage = desc.usage_count.load(std::memory_order_relaxed);
    while (usage < max_usage_count &&
           !desc.usage_count.compare_exchange_weak(usage, usage + 1, std::memory_order_relaxed)) {
    }
  }

  auto unpin(std::size_t index) noexcept -> void {
    auto& desc = frame(index);
    if (desc.pin_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
      pinned_pages_.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  auto wait_for_io(BufferDesc& desc) -> void {
    if (!desc.io_in_progress.load(std::memory_order_acquire)) {
      return;
    }
    std::unique_lock state_guard{desc.state_lock};
    desc.io_done.wait(state_guard,
                      [&desc] { return !desc.io_in_progress.load(std::memory_order_acquire); });
  }

  // Publish completion under the lock wait_for_io uses, so a waiter cannot miss the wakeup.
  auto finish_io(BufferDesc& desc, bool valid) -> void {
    {
      const std::lock_guard state_guard{desc.state_lock};
      desc.valid.store(valid, std::memory_order_release);
      desc.io_in_progress.store(false, std::memory_order_release);
    }
    desc.io_done.notify_all();
  }

  auto mark_dirty(std::size_t index) -> void {
    auto& desc = frame(index);
    {
      std::lock_guard state_guard{desc.state_lock};
      desc.page_lsn = desc.page.header().pd_lsn;
    }
    if (!desc.dirty.exchange(true, std::memory_order_relaxed)) {
      dirty_pages_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  auto flush_frame(std::size_t index) -> void;

  /// Requires `clock_lock_` and `map_lock_` in exclusive mode.
  [[nodiscard]] auto choose_victim_locked() -> std::size_t {
    const auto attempts = frames_.size() * victim_scan_factor;
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
      const auto index = next_victim_;
      next_victim_ = (next_victim_ + 1) % frames_.size();
      auto& desc = frame(index);
      if (desc.pin_count.load(std::memory_order_relaxed) > 0) {
        continue;
      }
      if (!desc.valid.load(std::memory_order_relaxed)) {
        return index;
      }
      const auto usage = desc.usage_count.load(std::memory_order_relaxed);
      if (usage > 0) {
        desc.usage_count.store(usage - 1, std::memory_order_relaxed);
        continue;
      }
      if (desc.dirty.load(std::memory_order_relaxed)) {
        flush_frame(index);
      }
      return index;
    }
    throw std::runtime_error{"buffer pool: every frame is pinned"};
  }

  std::vector<std::unique_ptr<BufferDesc>> frames_;
  std::unordered_map<BufferTag, std::size_t, BufferTagHash> map_;
  std::shared_mutex map_lock_;
  std::mutex clock_lock_;
  std::size_t next_victim_{0};
  PageSource& source_;
  Wal& wal_;
  std::atomic<std::size_t> hits_{0};
  std::atomic<std::size_t> misses_{0};
  std::atomic<std::size_t> evictions_{0};
  std::atomic<std::size_t> writes_{0};
  std::atomic<std::size_t> fsyncs_{0};
  std::atomic<std::size_t> dirty_pages_{0};
  std::atomic<std::size_t> pinned_pages_{0};
};

class ClockSweepBufferPool::Impl::Guard final : public PageGuard {
  public:
  Guard(Impl& pool, std::size_t index) : pool_{pool}, index_{index} {}
  Guard(const Guard&) = delete;
  auto operator=(const Guard&) -> Guard& = delete;
  Guard(Guard&&) = delete;
  auto operator=(Guard&&) -> Guard& = delete;

  ~Guard() override {
    unlock_held();
    pool_.unpin(index_);
  }

  [[nodiscard]] auto page() noexcept -> Page& override { return pool_.frame(index_).page; }

  auto lock_shared() -> void override {
    if (lock_state_ == LockState::shared) {
      return;
    }
    unlock_held();
    pool_.frame(index_).content_lock.lock_shared();
    lock_state_ = LockState::shared;
  }

  auto lock_exclusive() -> void override {
    if (lock_state_ == LockState::exclusive) {
      return;
    }
    unlock_held();
    pool_.frame(index_).content_lock.lock();
    lock_state_ = LockState::exclusive;
  }

  auto mark_dirty() -> void override { pool_.mark_dirty(index_); }

  private:
  enum class LockState : std::uint8_t { none, shared, exclusive };

  auto unlock_held() noexcept -> void {
    if (lock_state_ == LockState::shared) {
      pool_.frame(index_).content_lock.unlock_shared();
      lock_state_ = LockState::none;
    } else if (lock_state_ == LockState::exclusive) {
      pool_.frame(index_).content_lock.unlock();
      lock_state_ = LockState::none;
    }
  }

  Impl& pool_;
  std::size_t index_;
  LockState lock_state_{LockState::none};
};

auto ClockSweepBufferPool::Impl::read(BufferTag tag) -> std::unique_ptr<PageGuard> {
  auto retry = false;
  {
    std::shared_lock map_guard{map_lock_};
    const auto found = map_.find(tag);
    if (found != map_.end()) {
      const auto index = found->second;
      auto& desc = frame(index);
      pin(desc);
      wait_for_io(desc);
      if (desc.valid.load(std::memory_order_acquire)) {
        hits_.fetch_add(1, std::memory_order_relaxed);
        return std::make_unique<Guard>(*this, index);
      }
      unpin(index);
      retry = true;
    }
  }
  if (retry) {
    return read(tag);
  }

  std::unique_lock clock_guard{clock_lock_};
  std::unique_lock map_guard{map_lock_};
  const auto existing = map_.find(tag);
  if (existing != map_.end()) {
    const auto index = existing->second;
    auto& desc = frame(index);
    pin(desc);
    hits_.fetch_add(1, std::memory_order_relaxed);
    map_guard.unlock();
    clock_guard.unlock();
    wait_for_io(desc);
    if (!desc.valid.load(std::memory_order_acquire)) {
      unpin(index);
      return read(tag);
    }
    return std::make_unique<Guard>(*this, index);
  }

  const auto index = choose_victim_locked();
  auto& desc = frame(index);
  if (desc.valid.load(std::memory_order_relaxed)) {
    map_.erase(desc.tag);
    evictions_.fetch_add(1, std::memory_order_relaxed);
  }
  desc.tag = tag;
  desc.page = Page{};
  desc.page_lsn = 0;
  desc.dirty.store(false, std::memory_order_relaxed);
  desc.valid.store(false, std::memory_order_relaxed);
  desc.io_in_progress.store(true, std::memory_order_relaxed);
  map_.emplace(tag, index);
  pin(desc);
  misses_.fetch_add(1, std::memory_order_relaxed);
  map_guard.unlock();
  clock_guard.unlock();

  try {
    source_.read(tag.fork, tag.block, desc.page.mutable_bytes());
  } catch (...) {
    finish_io(desc, false);
    {
      std::unique_lock failed_lock{map_lock_};
      const auto failed = map_.find(tag);
      if (failed != map_.end() && failed->second == index) {
        map_.erase(failed);
      }
    }
    unpin(index);
    throw;
  }

  finish_io(desc, true);
  return std::make_unique<Guard>(*this, index);
}

auto ClockSweepBufferPool::Impl::new_page(BufferTag tag) -> std::unique_ptr<PageGuard> {
  std::unique_lock clock_guard{clock_lock_};
  std::unique_lock map_guard{map_lock_};
  if (map_.contains(tag)) {
    throw std::invalid_argument{"buffer pool: page is already cached"};
  }

  const auto index = choose_victim_locked();
  auto& desc = frame(index);
  if (desc.valid.load(std::memory_order_relaxed)) {
    map_.erase(desc.tag);
    evictions_.fetch_add(1, std::memory_order_relaxed);
  }
  desc.tag = tag;
  desc.page = Page{};
  desc.page_lsn = 0;
  desc.dirty.store(true, std::memory_order_relaxed);
  desc.valid.store(true, std::memory_order_relaxed);
  desc.io_in_progress.store(false, std::memory_order_relaxed);
  map_.emplace(tag, index);
  pin(desc);
  dirty_pages_.fetch_add(1, std::memory_order_relaxed);
  return std::make_unique<Guard>(*this, index);
}

auto ClockSweepBufferPool::Impl::flush(BufferTag tag) -> void {
  std::shared_lock map_guard{map_lock_};
  const auto found = map_.find(tag);
  if (found == map_.end()) {
    return;
  }
  flush_frame(found->second);
}

auto ClockSweepBufferPool::Impl::flush_all() -> void {
  std::shared_lock map_guard{map_lock_};
  for (std::size_t index = 0; index < frames_.size(); ++index) {
    const auto& desc = frame(index);
    if (desc.valid.load(std::memory_order_acquire) && desc.dirty.load(std::memory_order_acquire)) {
      flush_frame(index);
    }
  }
  source_.fsync(Fork::main);
  fsyncs_.fetch_add(1, std::memory_order_relaxed);
}

auto ClockSweepBufferPool::Impl::stats() const noexcept -> BufferStats {
  return BufferStats{
      hits_.load(std::memory_order_relaxed),        misses_.load(std::memory_order_relaxed),
      evictions_.load(std::memory_order_relaxed),   writes_.load(std::memory_order_relaxed),
      fsyncs_.load(std::memory_order_relaxed),      dirty_pages_.load(std::memory_order_relaxed),
      pinned_pages_.load(std::memory_order_relaxed)};
}

auto ClockSweepBufferPool::Impl::flush_frame(std::size_t index) -> void {
  auto& desc = frame(index);
  if (!desc.valid.load(std::memory_order_acquire)) {
    return;
  }
  std::unique_lock content_guard{desc.content_lock};
  if (!desc.dirty.load(std::memory_order_acquire)) {
    return;
  }

  auto page_lsn = std::uint64_t{0};
  {
    std::lock_guard state_guard{desc.state_lock};
    page_lsn = desc.page_lsn;
  }
  if (page_lsn != invalid_lsn && wal_.needs_flush(page_lsn)) {
    wal_.flush(page_lsn);
  }

  desc.page.set_checksum();
  source_.write(desc.tag.fork, desc.tag.block, desc.page.bytes());
  if (desc.dirty.exchange(false, std::memory_order_acq_rel)) {
    dirty_pages_.fetch_sub(1, std::memory_order_relaxed);
  }
  writes_.fetch_add(1, std::memory_order_relaxed);
}

ClockSweepBufferPool::ClockSweepBufferPool(std::size_t capacity, PageSource& source, Wal& wal)
    : impl_{std::make_unique<Impl>(capacity, source, wal)} {}

ClockSweepBufferPool::~ClockSweepBufferPool() = default;

auto ClockSweepBufferPool::read(BufferTag tag) -> std::unique_ptr<PageGuard> {
  return impl_->read(tag);
}

auto ClockSweepBufferPool::new_page(BufferTag tag) -> std::unique_ptr<PageGuard> {
  return impl_->new_page(tag);
}

auto ClockSweepBufferPool::flush(BufferTag tag) -> void {
  impl_->flush(tag);
}

auto ClockSweepBufferPool::flush_all() -> void {
  impl_->flush_all();
}

auto ClockSweepBufferPool::stats() const noexcept -> BufferStats {
  return impl_->stats();
}

}  // namespace kravidb::storage
