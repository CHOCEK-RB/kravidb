#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

import std;
import kravidb;

namespace {

using kravidb::storage::BlockNumber;
using kravidb::storage::FilePageSource;
using kravidb::storage::FileRelationFile;
using kravidb::storage::Fork;
using kravidb::storage::physical_page_size;
using kravidb::storage::RelFileNode;

constexpr std::uint32_t tablespace_oid = 1;
constexpr std::uint32_t database_oid = 2;
constexpr std::uint32_t relation_oid = 42;
constexpr std::uint32_t many_pages = 100;
constexpr std::uint32_t crossing_block = 3;
constexpr std::uint32_t next_segment_block = 4;
constexpr std::uint32_t written_block = 5;
constexpr std::uint32_t extended_pages = 5;
constexpr std::uint32_t truncated_pages = 2;
constexpr std::uint32_t page_source_block = 2;
constexpr std::uint32_t pattern_modulus = 251;
constexpr std::uint32_t byte_shift = 8;
constexpr std::uint32_t byte_mask = 0xFF;
constexpr std::size_t many_segment_pages = 32;
constexpr std::size_t crossing_segment_pages = 4;
constexpr std::size_t page_source_segment_pages = 4;

auto segment_bytes(std::size_t pages) -> std::size_t {
  return pages * physical_page_size;
}

auto make_node() -> RelFileNode {
  return RelFileNode{
      .tablespace = tablespace_oid,
      .database = database_oid,
      .relation = relation_oid,
  };
}

auto make_page(BlockNumber block) -> std::array<std::byte, physical_page_size> {
  std::array<std::byte, physical_page_size> page{};
  page.fill(static_cast<std::byte>(block % pattern_modulus));
  page.at(0) = static_cast<std::byte>((block >> byte_shift) & byte_mask);
  page.at(physical_page_size - 1) = static_cast<std::byte>(block & byte_mask);
  return page;
}

auto segment_path(const std::filesystem::path& directory, BlockNumber segment)
    -> std::filesystem::path {
  auto name = std::to_string(relation_oid);
  if (segment > 0) {
    name += "." + std::to_string(segment);
  }
  return directory / name;
}

auto read_page(FileRelationFile& file, BlockNumber block)
    -> std::array<std::byte, physical_page_size> {
  std::array<std::byte, physical_page_size> page{};
  file.read(Fork::main, block, page);
  return page;
}

void write_page(FileRelationFile& file, BlockNumber block) {
  const auto page = make_page(block);
  file.write(Fork::main, block, page);
}

auto is_zero(std::span<const std::byte> page) -> bool {
  return std::ranges::all_of(page, [](std::byte value) { return value == std::byte{0}; });
}

class TempDirectory {
  public:
  TempDirectory()
      : path_{std::filesystem::temp_directory_path() /
              ("kravidb-relation-" + std::to_string(next_id_.fetch_add(1)))} {
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TempDirectory(const TempDirectory&) = delete;
  auto operator=(const TempDirectory&) -> TempDirectory& = delete;
  TempDirectory(TempDirectory&&) = delete;
  auto operator=(TempDirectory&&) -> TempDirectory& = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

  private:
  static inline std::atomic<std::uint64_t> next_id_{0};
  std::filesystem::path path_;
};

TEST(FileRelationFile, WritesAndReadsOneHundredPages) {
  TempDirectory directory;
  const auto node = make_node();
  const auto segment_size = segment_bytes(many_segment_pages);
  {
    FileRelationFile file{directory.path(), node, segment_size};
    for (BlockNumber block = 0; block < many_pages; ++block) {
      write_page(file, block);
    }
    file.fsync(Fork::main);
  }

  FileRelationFile reopened{directory.path(), node, segment_size};
  for (BlockNumber block = 0; block < many_pages; ++block) {
    EXPECT_EQ(read_page(reopened, block), make_page(block));
  }

  EXPECT_TRUE(std::filesystem::exists(segment_path(directory.path(), 0)));
  EXPECT_TRUE(std::filesystem::exists(segment_path(directory.path(), 3)));
  EXPECT_FALSE(std::filesystem::exists(segment_path(directory.path(), 4)));
}

TEST(FileRelationFile, CrossesSegmentWithoutLosingPages) {
  TempDirectory directory;
  const auto node = make_node();
  const auto segment_size = segment_bytes(crossing_segment_pages);
  FileRelationFile file{directory.path(), node, segment_size};

  write_page(file, crossing_block);
  write_page(file, next_segment_block);
  file.fsync(Fork::main);

  EXPECT_EQ(read_page(file, crossing_block), make_page(crossing_block));
  EXPECT_EQ(read_page(file, next_segment_block), make_page(next_segment_block));
  EXPECT_TRUE(std::filesystem::exists(segment_path(directory.path(), 0)));
  EXPECT_TRUE(std::filesystem::exists(segment_path(directory.path(), 1)));
}

TEST(FileRelationFile, CreatesFileOnFirstWrite) {
  TempDirectory directory;
  const auto node = make_node();
  const auto segment_size = segment_bytes(crossing_segment_pages);
  FileRelationFile file{directory.path(), node, segment_size};

  EXPECT_FALSE(std::filesystem::exists(segment_path(directory.path(), 0)));
  write_page(file, 0);
  EXPECT_TRUE(std::filesystem::exists(segment_path(directory.path(), 0)));
  EXPECT_EQ(read_page(file, 0), make_page(0));
}

TEST(FileRelationFile, ReopenRebuildsLogicalSize) {
  TempDirectory directory;
  const auto node = make_node();
  const auto segment_size = segment_bytes(crossing_segment_pages);
  {
    FileRelationFile file{directory.path(), node, segment_size};
    write_page(file, written_block);
    file.fsync(Fork::main);
  }

  FileRelationFile reopened{directory.path(), node, segment_size};
  EXPECT_EQ(read_page(reopened, written_block), make_page(written_block));
  std::array<std::byte, physical_page_size> beyond{};
  reopened.read(Fork::main, written_block + 1, beyond);
  EXPECT_TRUE(is_zero(beyond));
}

TEST(FileRelationFile, ExtendsAndTruncatesSegments) {
  TempDirectory directory;
  const auto node = make_node();
  const auto segment_size = segment_bytes(crossing_segment_pages);
  FileRelationFile file{directory.path(), node, segment_size};

  file.extend(Fork::main, extended_pages);
  EXPECT_EQ(std::filesystem::file_size(segment_path(directory.path(), 0)),
            segment_bytes(crossing_segment_pages));
  EXPECT_EQ(std::filesystem::file_size(segment_path(directory.path(), 1)),
            segment_bytes(extended_pages - crossing_segment_pages));

  file.truncate(Fork::main, truncated_pages);
  EXPECT_EQ(std::filesystem::file_size(segment_path(directory.path(), 0)),
            segment_bytes(truncated_pages));
  EXPECT_FALSE(std::filesystem::exists(segment_path(directory.path(), 1)));

  std::array<std::byte, physical_page_size> beyond{};
  file.read(Fork::main, next_segment_block, beyond);
  EXPECT_TRUE(is_zero(beyond));

  file.truncate(Fork::main, 0);
  EXPECT_FALSE(std::filesystem::exists(segment_path(directory.path(), 0)));
}

TEST(FilePageSource, ForwardsAllOperations) {
  TempDirectory directory;
  const auto node = make_node();
  const auto segment_size = segment_bytes(page_source_segment_pages);
  FileRelationFile file{directory.path(), node, segment_size};
  FilePageSource source{file};

  const auto page = make_page(page_source_block);
  source.write(Fork::main, page_source_block, page);
  source.fsync(Fork::main);
  std::array<std::byte, physical_page_size> read{};
  source.read(Fork::main, page_source_block, read);
  EXPECT_EQ(read, page);

  source.extend(Fork::main, extended_pages);
  source.truncate(Fork::main, page_source_block + 1);
  EXPECT_EQ(read_page(file, page_source_block), page);
}

#if !defined(_WIN32)
TEST(FileRelationFile, ReadsAfterFsyncFromAnotherProcess) {
  TempDirectory directory;
  const auto node = make_node();
  const auto segment_size = segment_bytes(crossing_segment_pages);
  {
    FileRelationFile file{directory.path(), node, segment_size};
    write_page(file, written_block);
    file.fsync(Fork::main);
  }

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    FileRelationFile file{directory.path(), node, segment_size};
    const auto page = read_page(file, written_block);
    ::_exit(page == make_page(written_block) ? 0 : 1);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}
#endif

}  // namespace
