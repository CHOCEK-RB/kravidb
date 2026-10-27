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

  private:
  using Key = std::pair<Fork, BlockNumber>;
  std::map<Key, std::vector<std::byte>> pages_;
};

inline auto MemoryPageSource::read(Fork fork, BlockNumber block,
                                   std::span<std::byte, physical_page_size> out) -> void {
  const auto page = pages_.find(Key{fork, block});
  if (page == pages_.end()) {
    std::ranges::fill(out, std::byte{0});
    return;
  }
  std::ranges::copy(page->second, out.begin());
}

inline auto MemoryPageSource::write(Fork fork, BlockNumber block,
                                    std::span<const std::byte, physical_page_size> input) -> void {
  auto [page, inserted] = pages_.try_emplace(Key{fork, block});
  if (inserted) {
    page->second.resize(physical_page_size);
  }
  std::ranges::copy(input, page->second.begin());
}

}  // namespace kravidb::storage
