#include <gtest/gtest.h>

import std;
import kravidb;

namespace {

using kravidb::storage::FileWal;
using kravidb::storage::Fork;
using kravidb::storage::invalid_lsn;
using kravidb::storage::LSN;
using kravidb::storage::wal_page_size;
using kravidb::storage::WalBlockRef;
using kravidb::storage::WalRecord;
using kravidb::storage::WalSegmentId;

constexpr std::size_t test_segment_size = 2 * wal_page_size;
constexpr std::uint32_t test_timeline = 1;
constexpr std::size_t main_data_size = 4000;
constexpr std::size_t small_payload_size = 16;
constexpr std::size_t segment_name_length = 24;
constexpr std::size_t wal_record_header_size = 24;
constexpr std::size_t wal_record_crc_offset = 20;
constexpr std::uint32_t known_empty_record_crc = 0xC17351D0U;
constexpr std::size_t wal_main_data_marker_size = 1;
constexpr std::size_t corruption_offset_in_main_data = 2;
constexpr std::size_t prev_offset_in_record = 8;
constexpr std::uint8_t byte_mask = 0xFF;
constexpr std::uint32_t default_xid = 7;
constexpr std::uint32_t second_xid = 9;
constexpr std::uint32_t round_trip_xid = 5;
constexpr std::uint32_t first_block = 11;
constexpr std::uint32_t second_block = 12;
constexpr std::uint8_t main_data_first = 0xAA;
constexpr std::uint8_t main_data_second = 0xBB;
constexpr std::size_t bits_per_byte = 8;

class TempDirectory {
  public:
  TempDirectory()
      : path_{std::filesystem::temp_directory_path() /
              ("kravidb-wal-" + std::to_string(next_id_++))} {
    static_cast<void>(std::filesystem::create_directories(path_));
  }

  ~TempDirectory() {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(path_, error));
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto make_record(std::size_t payload_size, std::uint32_t xid = default_xid) -> WalRecord {
  WalRecord record{.rmid = 1, .info = 2, .xid = xid};
  record.main_data.resize(payload_size);
  for (std::size_t index = 0; index < payload_size; ++index) {
    record.main_data.at(index) = std::byte{static_cast<std::uint8_t>(index)};
  }
  return record;
}

auto segment_files(const std::filesystem::path& directory) -> std::vector<std::filesystem::path> {
  std::vector<std::filesystem::path> result;
  for (const auto& entry : std::filesystem::directory_iterator{directory}) {
    if (entry.is_regular_file() && entry.path().filename().string().size() == segment_name_length) {
      result.push_back(entry.path());
    }
  }
  std::ranges::sort(result);
  return result;
}

auto read_u64_le(const std::filesystem::path& path, std::uint64_t offset) -> std::uint64_t {
  std::ifstream stream{path, std::ios::binary};
  if (!stream) {
    throw std::runtime_error{"cannot open WAL segment"};
  }
  stream.seekg(static_cast<std::streamoff>(offset));
  std::array<char, sizeof(std::uint64_t)> bytes{};
  stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    throw std::runtime_error{"cannot read WAL segment"};
  }
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes.at(index)))
             << (index * bits_per_byte);
  }
  return value;
}

TEST(WalSegmentId, NamesTimelineLogAndSegment) {
  const WalSegmentId id{.timeline = 2, .log = 3, .seg = 4};
  EXPECT_EQ(id.timeline, 2U);
  EXPECT_EQ(id.log, 3U);
  EXPECT_EQ(id.seg, 4U);
  EXPECT_EQ(id, (WalSegmentId{.timeline = 2, .log = 3, .seg = 4}));
}

TEST(FileWal, RoundTripsBlocksAndMainData) {
  TempDirectory directory;
  FileWal wal{directory.path()};
  const std::array<std::byte, 4> data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  const std::vector<std::byte> image{std::byte{9}, std::byte{8}, std::byte{7}};
  WalRecord record{.rmid = 3, .info = 4, .xid = round_trip_xid};
  record.blocks.push_back(WalBlockRef{.id = 0,
                                      .fork = Fork::main,
                                      .block = first_block,
                                      .data = std::span<const std::byte>{data},
                                      .full_image = std::optional<std::vector<std::byte>>{image}});
  record.blocks.push_back(WalBlockRef{.id = 1,
                                      .fork = Fork::main,
                                      .block = second_block,
                                      .data = std::span<const std::byte>{},
                                      .full_image = std::nullopt});
  record.main_data = {std::byte{main_data_first}, std::byte{main_data_second}};

  const LSN start = wal.insert(record);
  EXPECT_EQ(start, wal_page_size);
  EXPECT_TRUE(wal.needs_flush(start));
  wal.flush(start);
  EXPECT_FALSE(wal.needs_flush(start));

  const auto decoded = wal.read(start);
  EXPECT_EQ(decoded.rmid, record.rmid);
  EXPECT_EQ(decoded.info, record.info);
  EXPECT_EQ(decoded.xid, record.xid);
  ASSERT_EQ(decoded.blocks.size(), 2U);
  EXPECT_EQ(decoded.blocks.at(0).id, 0);
  EXPECT_EQ(decoded.blocks.at(0).fork, Fork::main);
  EXPECT_EQ(decoded.blocks.at(0).block, 11U);
  EXPECT_TRUE(std::ranges::equal(decoded.blocks.at(0).data, data));
  ASSERT_TRUE(decoded.blocks.at(0).full_image.has_value());
  EXPECT_TRUE(std::ranges::equal(decoded.blocks.at(0).full_image.value(), image));
  EXPECT_TRUE(decoded.blocks.at(1).data.empty());
  EXPECT_FALSE(decoded.blocks.at(1).full_image.has_value());
  EXPECT_EQ(decoded.main_data, record.main_data);
}

TEST(FileWal, WritesKnownCrc32c) {
  TempDirectory directory;
  FileWal wal{directory.path()};
  const WalRecord record{.rmid = 0, .info = 0, .xid = 0};
  const LSN start = wal.insert(record);
  wal.flush(start);

  const auto files = segment_files(directory.path());
  ASSERT_EQ(files.size(), 1U);
  const auto crc =
      read_u64_le(files.front(), static_cast<std::uint64_t>(start) + wal_record_crc_offset);
  EXPECT_EQ(crc, static_cast<std::uint64_t>(known_empty_record_crc));
}

TEST(FileWal, ReadsBufferedRecordBeforeFlush) {
  TempDirectory directory;
  FileWal wal{directory.path()};
  const auto payload = make_record(small_payload_size);
  const LSN start = wal.insert(payload);
  EXPECT_TRUE(wal.needs_flush(start));
  EXPECT_EQ(wal.read(start).main_data, payload.main_data);
  EXPECT_TRUE(wal.needs_flush(start));
}

TEST(FileWal, PartialFlushIsVisibleToAnotherInstance) {
  TempDirectory directory;
  FileWal writer{directory.path()};
  const auto first_record = make_record(64);
  const auto second_record = make_record(80);
  const LSN first_lsn = writer.insert(first_record);
  const LSN second_lsn = writer.insert(second_record);
  writer.flush(first_lsn);
  EXPECT_FALSE(writer.needs_flush(first_lsn));
  EXPECT_TRUE(writer.needs_flush(second_lsn));

  FileWal reader{directory.path()};
  EXPECT_EQ(reader.read(first_lsn).main_data, first_record.main_data);
  EXPECT_THROW(static_cast<void>(reader.read(second_lsn)), std::out_of_range);

  writer.flush(second_lsn);
  FileWal reopened{directory.path()};
  EXPECT_EQ(reopened.read(first_lsn).main_data, first_record.main_data);
  EXPECT_EQ(reopened.read(second_lsn).main_data, second_record.main_data);
}

TEST(FileWal, ReopenContinuesAfterLastRecord) {
  TempDirectory directory;
  const auto first_record = make_record(32);
  const auto second_record = make_record(48, second_xid);
  LSN first_lsn{invalid_lsn};
  {
    FileWal wal{directory.path()};
    first_lsn = wal.insert(first_record);
    wal.flush(first_lsn);
  }

  FileWal reopened{directory.path()};
  const LSN second_lsn = reopened.insert(second_record);
  EXPECT_GT(second_lsn, first_lsn);
  reopened.flush(second_lsn);
  EXPECT_EQ(reopened.read(first_lsn).main_data, first_record.main_data);
  EXPECT_EQ(reopened.read(second_lsn).main_data, second_record.main_data);
}

TEST(FileWal, CrossesSegmentWithoutLosingRecords) {
  TempDirectory directory;
  FileWal wal{directory.path(), test_timeline, test_segment_size};
  const auto payload = make_record(main_data_size);
  const LSN first_lsn = wal.insert(payload);
  const LSN second_lsn = wal.insert(payload);
  const LSN third_lsn = wal.insert(payload);
  EXPECT_EQ(first_lsn, wal_page_size);
  EXPECT_GT(second_lsn, first_lsn);
  EXPECT_GT(third_lsn, second_lsn);
  EXPECT_GT(third_lsn / test_segment_size, first_lsn / test_segment_size);

  wal.flush(third_lsn);
  EXPECT_EQ(wal.read(first_lsn).main_data, payload.main_data);
  EXPECT_EQ(wal.read(second_lsn).main_data, payload.main_data);
  EXPECT_EQ(wal.read(third_lsn).main_data, payload.main_data);
  EXPECT_GE(segment_files(directory.path()).size(), 2U);
}

TEST(FileWal, SwitchSegmentForcesNextRecordToBoundary) {
  TempDirectory directory;
  FileWal wal{directory.path(), test_timeline, test_segment_size};
  const auto payload = make_record(small_payload_size);
  const LSN first_lsn = wal.insert(payload);
  wal.switch_segment();
  const LSN second_lsn = wal.insert(payload);
  EXPECT_EQ(second_lsn % test_segment_size, 0U);
  EXPECT_GT(second_lsn, first_lsn);

  wal.flush(second_lsn);
  EXPECT_EQ(wal.read(first_lsn).main_data, payload.main_data);
  EXPECT_EQ(wal.read(second_lsn).main_data, payload.main_data);
}

TEST(FileWal, RecycleRemovesSegmentsBeforeLsn) {
  TempDirectory directory;
  FileWal wal{directory.path(), test_timeline, test_segment_size};
  const auto payload = make_record(main_data_size);
  const LSN first_lsn = wal.insert(payload);
  wal.switch_segment();
  const LSN second_lsn = wal.insert(payload);
  wal.switch_segment();
  const LSN third_lsn = wal.insert(payload);
  wal.flush(third_lsn);

  wal.recycle(second_lsn);
  EXPECT_THROW(static_cast<void>(wal.read(first_lsn)), std::runtime_error);
  EXPECT_EQ(wal.read(second_lsn).main_data, payload.main_data);
  EXPECT_EQ(wal.read(third_lsn).main_data, payload.main_data);
  EXPECT_EQ(segment_files(directory.path()).size(), 2U);
}

TEST(FileWal, DetectsCorruptedRecord) {
  TempDirectory directory;
  FileWal wal{directory.path()};
  const auto payload = make_record(small_payload_size);
  const LSN start = wal.insert(payload);
  wal.flush(start);

  const auto files = segment_files(directory.path());
  ASSERT_EQ(files.size(), 1U);
  const auto offset = static_cast<std::streamoff>(
      start + wal_record_header_size + wal_main_data_marker_size + corruption_offset_in_main_data);
  std::fstream stream{files.front(), std::ios::in | std::ios::out | std::ios::binary};
  ASSERT_TRUE(stream.is_open());
  stream.seekg(offset);
  char original = 0;
  stream.read(&original, 1);
  ASSERT_TRUE(stream.good());
  const auto corrupted = static_cast<char>(static_cast<unsigned char>(original) ^ byte_mask);
  stream.seekp(offset);
  stream.write(&corrupted, 1);
  stream.flush();
  ASSERT_TRUE(stream.good());

  EXPECT_THROW(static_cast<void>(wal.read(start)), std::runtime_error);
}

TEST(FileWal, LinksRecordsWithPreviousLsn) {
  TempDirectory directory;
  FileWal wal{directory.path()};
  const LSN first_lsn = wal.insert(make_record(8));
  const LSN second_lsn = wal.insert(make_record(8));
  wal.flush(second_lsn);

  const auto files = segment_files(directory.path());
  ASSERT_EQ(files.size(), 1U);
  EXPECT_EQ(read_u64_le(files.front(), first_lsn + prev_offset_in_record), 0U);
  EXPECT_EQ(read_u64_le(files.front(), second_lsn + prev_offset_in_record), first_lsn);
}

TEST(FileWal, RejectsRecordLargerThanSegment) {
  TempDirectory directory;
  FileWal wal{directory.path(), test_timeline, test_segment_size};
  const auto payload = make_record(test_segment_size + 1);
  EXPECT_THROW(static_cast<void>(wal.insert(payload)), std::length_error);
}

}  // namespace
