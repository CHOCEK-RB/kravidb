/// \file
/// \brief Page source contract and its in-memory adapter.

export module storage.page_source;

import std;
export import storage.relation_file;

export namespace kravidb::storage {

/// \brief Contract of the block device used by the buffer pool.
///
/// Reads and writes whole physical pages at explicit block numbers. The
/// durable engine uses a file-backed source; logic tests use
/// `MemoryPageSource`.
class PageSource {
  public:
  PageSource() = default;
  PageSource(const PageSource&) = delete;
  auto operator=(const PageSource&) -> PageSource& = delete;
  PageSource(PageSource&&) = delete;
  auto operator=(PageSource&&) -> PageSource& = delete;
  virtual ~PageSource() = default;

  /// \brief Reads block `block` of `fork` into `out`.
  virtual auto read(Fork fork, BlockNumber block, std::span<std::byte, physical_page_size> out)
      -> void = 0;

  /// \brief Writes `input` into block `block` of `fork`.
  virtual auto write(Fork fork, BlockNumber block,
                     std::span<const std::byte, physical_page_size> input) -> void = 0;

  /// \brief Grows `fork` by `blocks` blocks.
  virtual auto extend(Fork fork, BlockNumber blocks) -> void = 0;

  /// \brief Truncates `fork` to `blocks` blocks.
  virtual auto truncate(Fork fork, BlockNumber blocks) -> void = 0;

  /// \brief Flushes `fork` to durable storage.
  virtual auto fsync(Fork fork) -> void = 0;
};

/// \brief Page source that keeps every block in RAM and touches no files.
///
/// Reading a block that was never written yields a zero-filled page, matching
/// a freshly extended sparse file. It is a test double, not a durable store.
class MemoryPageSource final : public PageSource {
  public:
  auto read(Fork fork, BlockNumber block, std::span<std::byte, physical_page_size> out)
      -> void override;
  auto write(Fork fork, BlockNumber block, std::span<const std::byte, physical_page_size> input)
      -> void override;
  auto extend(Fork fork, BlockNumber blocks) -> void override;
  auto truncate(Fork fork, BlockNumber blocks) -> void override;
  auto fsync(Fork) -> void override {}

  private:
  using Key = std::pair<Fork, BlockNumber>;
  std::map<Key, std::vector<std::byte>> pages_;
  std::map<Fork, BlockNumber> sizes_;
};

/// \brief Page source backed by a relation file.
class FilePageSource final : public PageSource {
  public:
  explicit FilePageSource(RelationFile& file) : file_{file} {}

  auto read(Fork fork, BlockNumber block, std::span<std::byte, physical_page_size> out)
      -> void override {
    file_.read(fork, block, out);
  }

  auto write(Fork fork, BlockNumber block, std::span<const std::byte, physical_page_size> input)
      -> void override {
    file_.write(fork, block, input);
  }

  auto extend(Fork fork, BlockNumber blocks) -> void override { file_.extend(fork, blocks); }
  auto truncate(Fork fork, BlockNumber blocks) -> void override { file_.truncate(fork, blocks); }
  auto fsync(Fork fork) -> void override { file_.fsync(fork); }

  private:
  RelationFile& file_;
};

inline auto MemoryPageSource::read(Fork fork, BlockNumber block,
                                   std::span<std::byte, physical_page_size> out) -> void {
  const auto size = sizes_.find(fork);
  if (size == sizes_.end() || block >= size->second) {
    std::ranges::fill(out, std::byte{0});
    return;
  }
  const auto page = pages_.find(Key{fork, block});
  if (page == pages_.end()) {
    std::ranges::fill(out, std::byte{0});
    return;
  }
  std::ranges::copy(page->second, out.begin());
}

inline auto MemoryPageSource::write(Fork fork, BlockNumber block,
                                    std::span<const std::byte, physical_page_size> input) -> void {
  if (block == std::numeric_limits<BlockNumber>::max()) {
    throw std::length_error{"MemoryPageSource: block number overflows"};
  }
  auto& size = sizes_.try_emplace(fork, BlockNumber{0}).first->second;
  size = std::max(size, static_cast<BlockNumber>(block + 1));
  auto [page, inserted] = pages_.try_emplace(Key{fork, block});
  if (inserted) {
    page->second.resize(physical_page_size);
  }
  std::ranges::copy(input, page->second.begin());
}

inline auto MemoryPageSource::extend(Fork fork, BlockNumber blocks) -> void {
  auto& size = sizes_.try_emplace(fork, BlockNumber{0}).first->second;
  const auto target = static_cast<std::uint64_t>(size) + blocks;
  if (target > std::numeric_limits<BlockNumber>::max()) {
    throw std::length_error{"MemoryPageSource: extend overflows the block number"};
  }
  size = static_cast<BlockNumber>(target);
}

inline auto MemoryPageSource::truncate(Fork fork, BlockNumber blocks) -> void {
  sizes_.try_emplace(fork, BlockNumber{0}).first->second = blocks;
  for (auto page = pages_.begin(); page != pages_.end();) {
    if (page->first.first == fork && page->first.second >= blocks) {
      page = pages_.erase(page);
    } else {
      ++page;
    }
  }
}

}  // namespace kravidb::storage
