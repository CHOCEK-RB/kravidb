/// \file
/// \brief Page source contract and its in-memory and file-backed adapters.

export module storage.page_source;

import std;
export import storage.relation_file;
export import storage.file_relation_file;

export namespace kravidb::storage {

/// \brief Contract of the block device used by the buffer pool.
///
/// Reads and writes whole physical pages at explicit block numbers. Every
/// operation names the relation, so one source can serve several relations.
class PageSource {
  public:
  PageSource() = default;
  PageSource(const PageSource&) = delete;
  auto operator=(const PageSource&) -> PageSource& = delete;
  PageSource(PageSource&&) = delete;
  auto operator=(PageSource&&) -> PageSource& = delete;
  virtual ~PageSource() = default;

  /// \brief Reads block `block` of `rel` into `out`.
  virtual auto read(const RelFileNode& rel, Fork fork, BlockNumber block,
                    std::span<std::byte, physical_page_size> out) -> void = 0;

  /// \brief Writes `input` into block `block` of `rel`.
  virtual auto write(const RelFileNode& rel, Fork fork, BlockNumber block,
                     std::span<const std::byte, physical_page_size> input) -> void = 0;

  /// \brief Grows `rel` by `blocks` blocks.
  virtual auto extend(const RelFileNode& rel, Fork fork, BlockNumber blocks) -> void = 0;

  /// \brief Truncates `rel` to `blocks` blocks.
  virtual auto truncate(const RelFileNode& rel, Fork fork, BlockNumber blocks) -> void = 0;

  /// \brief Flushes one relation to durable storage.
  virtual auto fsync(const RelFileNode& rel, Fork fork) -> void = 0;

  /// \brief Flushes every relation known to the source.
  virtual auto fsync_all() -> void = 0;

  /// \brief Number of blocks present in `rel`.
  [[nodiscard]] virtual auto block_count(const RelFileNode& rel, Fork fork) const
      -> BlockNumber = 0;
};

/// \brief Page source that keeps every block in RAM and touches no files.
///
/// Reading a block that was never written yields a zero-filled page, matching
/// a freshly extended sparse file. It is a test double, not a durable store.
class MemoryPageSource final : public PageSource {
  public:
  auto read(const RelFileNode& rel, Fork fork, BlockNumber block,
            std::span<std::byte, physical_page_size> out) -> void override;
  auto write(const RelFileNode& rel, Fork fork, BlockNumber block,
             std::span<const std::byte, physical_page_size> input) -> void override;
  auto extend(const RelFileNode& rel, Fork fork, BlockNumber blocks) -> void override;
  auto truncate(const RelFileNode& rel, Fork fork, BlockNumber blocks) -> void override;
  auto fsync(const RelFileNode&, Fork) -> void override {}
  auto fsync_all() -> void override {}
  [[nodiscard]] auto block_count(const RelFileNode& rel, Fork fork) const -> BlockNumber override;

  private:
  struct Key {
    RelFileNode rel{};
    Fork fork{Fork::main};
    BlockNumber block{0};

    auto operator<=>(const Key&) const = default;
  };

  std::map<Key, std::vector<std::byte>> pages_;
  std::map<std::pair<RelFileNode, Fork>, BlockNumber> sizes_;
};

/// \brief Page source backed by one relation file.
class FilePageSource final : public PageSource {
  public:
  FilePageSource(RelationFile& file, RelFileNode) : file_{file} {}

  auto read(const RelFileNode&, Fork fork, BlockNumber block,
            std::span<std::byte, physical_page_size> out) -> void override {
    file_.read(fork, block, out);
  }

  auto write(const RelFileNode&, Fork fork, BlockNumber block,
             std::span<const std::byte, physical_page_size> input) -> void override {
    file_.write(fork, block, input);
  }

  auto extend(const RelFileNode&, Fork fork, BlockNumber blocks) -> void override {
    file_.extend(fork, blocks);
  }
  auto truncate(const RelFileNode&, Fork fork, BlockNumber blocks) -> void override {
    file_.truncate(fork, blocks);
  }
  auto fsync(const RelFileNode&, Fork fork) -> void override { file_.fsync(fork); }
  auto fsync_all() -> void override { file_.fsync(Fork::main); }
  [[nodiscard]] auto block_count(const RelFileNode&, Fork fork) const -> BlockNumber override {
    return file_.block_count(fork);
  }

  private:
  RelationFile& file_;
};

/// \brief Page source that maps each relation to a file under one directory.
///
/// Files are named after `RelFileNode::relation` and are created lazily by
/// writes. `block_count` opens a relation on demand, so it can tell an absent
/// file from an empty one.
class DirectoryPageSource final : public PageSource {
  public:
  explicit DirectoryPageSource(std::filesystem::path root) : root_{std::move(root)} {}

  auto read(const RelFileNode& rel, Fork fork, BlockNumber block,
            std::span<std::byte, physical_page_size> out) -> void override;
  auto write(const RelFileNode& rel, Fork fork, BlockNumber block,
             std::span<const std::byte, physical_page_size> input) -> void override;
  auto extend(const RelFileNode& rel, Fork fork, BlockNumber blocks) -> void override;
  auto truncate(const RelFileNode& rel, Fork fork, BlockNumber blocks) -> void override;
  auto fsync(const RelFileNode& rel, Fork fork) -> void override;
  auto fsync_all() -> void override;
  [[nodiscard]] auto block_count(const RelFileNode& rel, Fork fork) const -> BlockNumber override;

  private:
  [[nodiscard]] auto file_for(const RelFileNode& rel) const -> FileRelationFile&;

  std::filesystem::path root_;
  mutable std::map<RelFileNode, std::unique_ptr<FileRelationFile>> files_;
};

inline auto MemoryPageSource::read(const RelFileNode& rel, Fork fork, BlockNumber block,
                                   std::span<std::byte, physical_page_size> out) -> void {
  const auto size = sizes_.find(std::pair{rel, fork});
  if (size == sizes_.end() || block >= size->second) {
    std::ranges::fill(out, std::byte{0});
    return;
  }
  const auto page = pages_.find(Key{rel, fork, block});
  if (page == pages_.end()) {
    std::ranges::fill(out, std::byte{0});
    return;
  }
  std::ranges::copy(page->second, out.begin());
}

inline auto MemoryPageSource::write(const RelFileNode& rel, Fork fork, BlockNumber block,
                                    std::span<const std::byte, physical_page_size> input) -> void {
  if (block == std::numeric_limits<BlockNumber>::max()) {
    throw std::length_error{"MemoryPageSource: block number overflows"};
  }
  auto& size = sizes_.try_emplace(std::pair{rel, fork}, BlockNumber{0}).first->second;
  size = std::max(size, static_cast<BlockNumber>(block + 1U));
  auto [page, inserted] = pages_.try_emplace(Key{rel, fork, block});
  if (inserted) {
    page->second.resize(physical_page_size);
  }
  std::ranges::copy(input, page->second.begin());
}

inline auto MemoryPageSource::extend(const RelFileNode& rel, Fork fork, BlockNumber blocks)
    -> void {
  auto& size = sizes_.try_emplace(std::pair{rel, fork}, BlockNumber{0}).first->second;
  const auto target = static_cast<std::uint64_t>(size) + blocks;
  if (target > std::numeric_limits<BlockNumber>::max()) {
    throw std::length_error{"MemoryPageSource: extend overflows the block number"};
  }
  size = static_cast<BlockNumber>(target);
}

inline auto MemoryPageSource::truncate(const RelFileNode& rel, Fork fork, BlockNumber blocks)
    -> void {
  sizes_.try_emplace(std::pair{rel, fork}, BlockNumber{0}).first->second = blocks;
  for (auto page = pages_.begin(); page != pages_.end();) {
    if (page->first.rel == rel && page->first.fork == fork && page->first.block >= blocks) {
      page = pages_.erase(page);
    } else {
      ++page;
    }
  }
}

inline auto MemoryPageSource::block_count(const RelFileNode& rel, Fork fork) const -> BlockNumber {
  const auto size = sizes_.find(std::pair{rel, fork});
  return size == sizes_.end() ? 0 : size->second;
}

inline auto DirectoryPageSource::file_for(const RelFileNode& rel) const -> FileRelationFile& {
  auto [entry, inserted] = files_.try_emplace(rel, std::unique_ptr<FileRelationFile>{});
  static_cast<void>(inserted);
  if (entry->second == nullptr) {
    entry->second = std::make_unique<FileRelationFile>(root_, rel);
  }
  return *entry->second;
}

inline auto DirectoryPageSource::read(const RelFileNode& rel, Fork fork, BlockNumber block,
                                      std::span<std::byte, physical_page_size> out) -> void {
  file_for(rel).read(fork, block, out);
}

inline auto DirectoryPageSource::write(const RelFileNode& rel, Fork fork, BlockNumber block,
                                       std::span<const std::byte, physical_page_size> input)
    -> void {
  file_for(rel).write(fork, block, input);
}

inline auto DirectoryPageSource::extend(const RelFileNode& rel, Fork fork, BlockNumber blocks)
    -> void {
  file_for(rel).extend(fork, blocks);
}

inline auto DirectoryPageSource::truncate(const RelFileNode& rel, Fork fork, BlockNumber blocks)
    -> void {
  file_for(rel).truncate(fork, blocks);
}

inline auto DirectoryPageSource::fsync(const RelFileNode& rel, Fork fork) -> void {
  file_for(rel).fsync(fork);
}

inline auto DirectoryPageSource::fsync_all() -> void {
  for (auto& [rel, file] : files_) {
    static_cast<void>(rel);
    if (file != nullptr) {
      file->fsync(Fork::main);
    }
  }
}

inline auto DirectoryPageSource::block_count(const RelFileNode& rel, Fork fork) const
    -> BlockNumber {
  return file_for(rel).block_count(fork);
}

}  // namespace kravidb::storage
