/// \file
/// \brief Relation file contract: block-oriented I/O per relation.

export module storage.relation_file;

import std;

export namespace kravidb::storage {

/// \brief Physical block number inside a relation.
using BlockNumber = std::uint32_t;

/// \brief Size of the physical page exchanged with files and the buffer pool.
///
/// The paged record store still uses its own 4 KiB default; the durable heap
/// adopts this page size and retires the old one.
inline constexpr std::size_t physical_page_size = std::size_t{8} * 1024;

/// \brief Fork of a relation. The first version only uses the main fork.
enum class Fork : std::uint8_t {
  /// \brief Main fork with the relation data.
  main,
};

/// \brief Physical identity of a relation file.
struct RelFileNode {
  std::uint32_t tablespace{0};
  std::uint32_t database{0};
  std::uint32_t relation{0};

  /// \brief Two nodes point to the same relation file.
  auto operator==(const RelFileNode&) const -> bool = default;
};

/// \brief Contract of a relation file over fixed-size blocks.
///
/// Block offsets are absolute: implementations use positional I/O and keep no
/// global file cursor. The class is consumed by reference.
class RelationFile {
  public:
  RelationFile() = default;
  RelationFile(const RelationFile&) = delete;
  auto operator=(const RelationFile&) -> RelationFile& = delete;
  RelationFile(RelationFile&&) = delete;
  auto operator=(RelationFile&&) -> RelationFile& = delete;
  virtual ~RelationFile() = default;

  /// \brief Reads block `block` of `fork` into `out`.
  virtual auto read(Fork fork, BlockNumber block, std::span<std::byte, physical_page_size> out)
      -> void = 0;

  /// \brief Writes `input` into block `block` of `fork`.
  virtual auto write(Fork fork, BlockNumber block,
                     std::span<const std::byte, physical_page_size> input) -> void = 0;

  /// \brief Grows `fork` by `blocks` blocks.
  virtual auto extend(Fork fork, BlockNumber blocks) -> void = 0;

  /// \brief Flushes `fork` to durable storage.
  virtual auto fsync(Fork fork) -> void = 0;

  /// \brief Truncates `fork` to `blocks` blocks.
  virtual auto truncate(Fork fork, BlockNumber blocks) -> void = 0;
};

}  // namespace kravidb::storage
