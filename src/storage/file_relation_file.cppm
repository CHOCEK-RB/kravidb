/// \file
/// \brief File-backed relation file with segmented storage.

export module storage.file_relation_file;

import std;
export import storage.relation_file;

export namespace kravidb::storage {

/// \brief Default size of one relation segment (1 GiB).
inline constexpr std::size_t default_relation_segment_size = std::size_t{1} << 30U;

/// \brief Relation file backed by one or more segment files.
///
/// Segment names are `relation`, `relation.1`, `relation.2`, ... under
/// `base_directory`. The constructor only inspects existing segments; files
/// are created lazily by writes, `extend`, or `truncate`.
class FileRelationFile final : public RelationFile {
  public:
  explicit FileRelationFile(std::filesystem::path base_directory, RelFileNode node,
                            std::size_t segment_size = default_relation_segment_size);
  ~FileRelationFile() override;

  FileRelationFile(const FileRelationFile&) = delete;
  auto operator=(const FileRelationFile&) -> FileRelationFile& = delete;
  FileRelationFile(FileRelationFile&&) = delete;
  auto operator=(FileRelationFile&&) -> FileRelationFile& = delete;

  auto read(Fork fork, BlockNumber block, std::span<std::byte, physical_page_size> out)
      -> void override;
  auto write(Fork fork, BlockNumber block, std::span<const std::byte, physical_page_size> input)
      -> void override;
  auto extend(Fork fork, BlockNumber blocks) -> void override;
  auto fsync(Fork fork) -> void override;
  auto truncate(Fork fork, BlockNumber blocks) -> void override;
  [[nodiscard]] auto block_count(Fork fork) const -> BlockNumber override;

  private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kravidb::storage
