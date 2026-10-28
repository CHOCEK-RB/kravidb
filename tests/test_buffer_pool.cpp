#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using namespace kravidb::storage;

constexpr std::uint32_t test_tablespace = 1;
constexpr std::uint32_t test_database = 2;
constexpr std::uint32_t test_relation = 3;
constexpr std::byte test_marker{0x5A};
constexpr std::byte second_marker{0x6B};
constexpr BlockNumber missing_block = 9;
constexpr LSN test_lsn = 77;

auto make_tag(BlockNumber block) -> BufferTag {
  return BufferTag{RelFileNode{test_tablespace, test_database, test_relation}, Fork::main, block};
}

class CountingSource final : public PageSource {
  public:
  auto read(Fork fork, BlockNumber block, std::span<std::byte, physical_page_size> out)
      -> void override {
    ++reads_;
    if (before_read_) {
      before_read_();
    }
    inner_.read(fork, block, out);
  }

  auto write(Fork fork, BlockNumber block, std::span<const std::byte, physical_page_size> input)
      -> void override {
    ++writes_;
    inner_.write(fork, block, input);
  }

  auto extend(Fork fork, BlockNumber blocks) -> void override { inner_.extend(fork, blocks); }
  auto truncate(Fork fork, BlockNumber blocks) -> void override { inner_.truncate(fork, blocks); }
  auto fsync(Fork fork) -> void override {
    ++fsyncs_;
    inner_.fsync(fork);
  }

  [[nodiscard]] auto reads() const noexcept -> std::size_t { return reads_; }
  [[nodiscard]] auto writes() const noexcept -> std::size_t { return writes_; }
  [[nodiscard]] auto fsyncs() const noexcept -> std::size_t { return fsyncs_; }

  auto read_block(BlockNumber block) -> std::vector<std::byte> {
    std::vector<std::byte> bytes(physical_page_size);
    inner_.read(Fork::main, block, std::span<std::byte, physical_page_size>{bytes});
    return bytes;
  }

  auto set_before_read(std::function<void()> callback) -> void {
    before_read_ = std::move(callback);
  }

  private:
  MemoryPageSource inner_;
  std::function<void()> before_read_;
  std::size_t reads_{0};
  std::size_t writes_{0};
  std::size_t fsyncs_{0};
};

class RecordingWal final : public Wal {
  public:
  auto insert(const WalRecord&) -> LSN override { return ++inserted_; }

  auto flush(LSN upto) -> void override {
    ++flush_calls_;
    flushed_ = std::max(flushed_, upto);
  }

  [[nodiscard]] auto needs_flush(LSN upto) const -> bool override { return upto > flushed_; }

  auto read(LSN) -> WalRecord override {
    throw std::logic_error{"RecordingWal does not store records"};
  }

  auto switch_segment() -> void override {}
  auto recycle(LSN) -> void override {}
  auto close() -> void override {}

  [[nodiscard]] auto flushed() const noexcept -> LSN { return flushed_; }
  [[nodiscard]] auto flush_calls() const noexcept -> std::size_t { return flush_calls_; }

  private:
  LSN inserted_{0};
  LSN flushed_{0};
  std::size_t flush_calls_{0};
};

TEST(BufferPool, ReadsHitAndMiss) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{2, source, wal};
  const auto tag = make_tag(0);

  auto first = pool.read(tag);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(pool.stats().misses, 1U);
  EXPECT_EQ(pool.stats().hits, 0U);
  EXPECT_EQ(source.reads(), 1U);

  {
    auto second = pool.read(tag);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(pool.stats().misses, 1U);
    EXPECT_EQ(pool.stats().hits, 1U);
    EXPECT_EQ(source.reads(), 1U);
  }

  first.reset();
  EXPECT_EQ(pool.stats().pinned_pages, 0U);
}

TEST(BufferPool, PinnedFrameIsNeverEvicted) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{1, source, wal};

  auto pinned = pool.read(make_tag(0));
  ASSERT_NE(pinned, nullptr);
  EXPECT_THROW(static_cast<void>(pool.read(make_tag(1))), std::runtime_error);

  pinned.reset();
  auto replacement = pool.read(make_tag(1));
  ASSERT_NE(replacement, nullptr);
  EXPECT_EQ(pool.stats().evictions, 1U);
}

TEST(BufferPool, DirtyFrameIsWrittenBeforeReuse) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{1, source, wal};

  auto page = pool.new_page(make_tag(0));
  ASSERT_NE(page, nullptr);
  std::ranges::fill(page->page().mutable_bytes().subspan(page_header_size, 1), test_marker);
  page.reset();

  auto replacement = pool.read(make_tag(1));
  ASSERT_NE(replacement, nullptr);
  EXPECT_EQ(source.writes(), 1U);
  EXPECT_EQ(pool.stats().writes, 1U);
  EXPECT_EQ(pool.stats().dirty_pages, 0U);

  const auto written = source.read_block(0);
  EXPECT_EQ(written.at(page_header_size), test_marker);
}

TEST(BufferPool, FlushWritesOnlyDirtyPagesAndFlushAllFsyncs) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{2, source, wal};
  const auto first_tag = make_tag(0);
  const auto second_tag = make_tag(1);

  auto first = pool.new_page(first_tag);
  std::ranges::fill(first->page().mutable_bytes().subspan(page_header_size, 1), test_marker);
  first.reset();
  auto second = pool.new_page(second_tag);
  std::ranges::fill(second->page().mutable_bytes().subspan(page_header_size, 1), second_marker);
  second.reset();

  pool.flush(first_tag);
  EXPECT_EQ(source.writes(), 1U);
  EXPECT_EQ(pool.stats().writes, 1U);
  EXPECT_EQ(pool.stats().dirty_pages, 1U);

  pool.flush(make_tag(missing_block));
  EXPECT_EQ(source.writes(), 1U);

  pool.flush_all();
  EXPECT_EQ(source.writes(), 2U);
  EXPECT_EQ(source.fsyncs(), 1U);
  EXPECT_EQ(pool.stats().fsyncs, 1U);
  EXPECT_EQ(pool.stats().dirty_pages, 0U);
}

TEST(BufferPool, FlushesWalBeforeWritingDirtyPage) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  const auto tag = make_tag(0);

  auto page = pool.new_page(tag);
  ASSERT_NE(page, nullptr);
  auto header = page->page().header();
  header.pd_lsn = test_lsn;
  page->page().set_header(header);
  page->mark_dirty();
  page.reset();

  pool.flush(tag);
  EXPECT_EQ(wal.flush_calls(), 1U);
  EXPECT_EQ(wal.flushed(), test_lsn);
  EXPECT_EQ(source.writes(), 1U);

  pool.flush(tag);
  EXPECT_EQ(wal.flush_calls(), 1U);
}

TEST(BufferPool, RejectsDuplicateNewPage) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{2, source, wal};
  const auto tag = make_tag(0);

  auto page = pool.new_page(tag);
  ASSERT_NE(page, nullptr);
  EXPECT_THROW(static_cast<void>(pool.new_page(tag)), std::invalid_argument);
}

TEST(BufferPool, ContentLockCanBeTakenAndReleased) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{1, source, wal};
  const auto tag = make_tag(0);

  auto page = pool.read(tag);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(pool.stats().pinned_pages, 1U);
  page->lock_shared();
  page->lock_exclusive();
  page->mark_dirty();
  EXPECT_EQ(pool.stats().dirty_pages, 1U);
  page.reset();
  EXPECT_EQ(pool.stats().pinned_pages, 0U);
}

TEST(BufferPool, TwoReadersLoadThePageOnce) {
  CountingSource source;
  RecordingWal wal;
  ClockSweepBufferPool pool{2, source, wal};
  const auto tag = make_tag(0);

  std::latch read_entered{1};
  std::latch release_read{1};
  source.set_before_read([&read_entered, &release_read] {
    read_entered.count_down();
    release_read.wait();
  });

  std::thread first{[&pool, tag] {
    auto page = pool.read(tag);
    EXPECT_NE(page, nullptr);
  }};
  read_entered.wait();
  std::thread second{[&pool, tag] {
    auto page = pool.read(tag);
    EXPECT_NE(page, nullptr);
  }};
  release_read.count_down();
  first.join();
  second.join();

  EXPECT_EQ(source.reads(), 1U);
  EXPECT_EQ(pool.stats().misses, 1U);
  EXPECT_EQ(pool.stats().hits, 1U);
  EXPECT_EQ(pool.stats().pinned_pages, 0U);
}

}  // namespace
