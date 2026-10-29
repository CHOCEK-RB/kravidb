#include <gtest/gtest.h>

import std;
import kravidb;
import storage.crc32c;

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using kravidb::storage::ControlFile;
using kravidb::storage::ControlState;
using kravidb::storage::crc32c;
using kravidb::storage::DataDirectory;
using kravidb::storage::decode_control;
using kravidb::storage::default_data_directory;
using kravidb::storage::encode_control;
using kravidb::storage::kd_control_magic;
using kravidb::storage::kd_control_size;
using kravidb::storage::kd_format_version;
using kravidb::storage::kd_version_contents;
using kravidb::storage::WriteMode;

constexpr std::uint64_t test_system_identifier = 42;
constexpr std::uint64_t test_checkpoint_lsn = 1234;
constexpr std::uint64_t test_redo_lsn = 5678;
constexpr std::uint32_t test_timeline = 3;
constexpr std::uint32_t test_next_xid = 99;
constexpr std::uint32_t test_oldest_xid = 7;
constexpr std::uint64_t test_base_id = 11;
constexpr std::uint64_t test_previous_base = 22;
constexpr std::uint64_t test_checkpoint_time = 123456789;
constexpr std::size_t control_crc_offset = 84;
constexpr std::size_t control_data_offset = 12;
constexpr std::size_t control_format_offset = 8;
constexpr std::uint8_t flip_mask = 0xFFU;
constexpr std::uint8_t corrupt_byte = 0xAB;
constexpr std::uint32_t unknown_format_version = 2;
constexpr std::uint32_t invalid_enum_value = 9;
constexpr std::uint32_t updated_next_xid = 42;
constexpr std::uint32_t known_crc32c = 0xE3069283U;
constexpr std::uint32_t bits_per_byte = 8;

class TempDirectory {
  public:
  TempDirectory()
      : path_{std::filesystem::temp_directory_path() /
              ("kravidb-control-" + std::to_string(next_id_++))} {
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

auto make_control() -> ControlFile {
  ControlFile control{};
  control.system_identifier = test_system_identifier;
  control.checkpoint_lsn = test_checkpoint_lsn;
  control.redo_lsn = test_redo_lsn;
  control.timeline = test_timeline;
  control.state = ControlState::recovery;
  control.full_page_writes = 0;
  control.next_xid = test_next_xid;
  control.oldest_xid = test_oldest_xid;
  control.write_mode = WriteMode::readonly;
  control.base_id = test_base_id;
  control.previous_base = test_previous_base;
  control.checkpoint_time = test_checkpoint_time;
  return control;
}

auto read_bytes(const std::filesystem::path& path) -> std::vector<std::byte> {
  std::ifstream stream{path, std::ios::binary};
  if (!stream) {
    throw std::runtime_error{"cannot open " + path.string()};
  }
  std::vector<char> raw{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  std::vector<std::byte> bytes(raw.size());
  std::ranges::transform(raw, bytes.begin(),
                         [](char value) { return std::byte{static_cast<unsigned char>(value)}; });
  return bytes;
}

auto read_text(const std::filesystem::path& path) -> std::string {
  std::ifstream stream{path, std::ios::binary};
  if (!stream) {
    throw std::runtime_error{"cannot open " + path.string()};
  }
  return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

auto write_text(const std::filesystem::path& path, std::string_view text) -> void {
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  if (!stream) {
    throw std::runtime_error{"cannot open " + path.string()};
  }
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  stream.flush();
  if (!stream) {
    throw std::runtime_error{"cannot write " + path.string()};
  }
}

auto flip_byte(const std::filesystem::path& path, std::uint64_t offset) -> void {
  std::fstream stream{path, std::ios::in | std::ios::out | std::ios::binary};
  if (!stream) {
    throw std::runtime_error{"cannot open " + path.string()};
  }
  stream.seekg(static_cast<std::streamoff>(offset));
  char value = 0;
  stream.read(&value, 1);
  if (!stream) {
    throw std::runtime_error{"cannot read " + path.string()};
  }
  value = static_cast<char>(static_cast<unsigned char>(value) ^ flip_mask);
  stream.seekp(static_cast<std::streamoff>(offset));
  stream.write(&value, 1);
  stream.flush();
  if (!stream) {
    throw std::runtime_error{"cannot write " + path.string()};
  }
}

auto read_u32_le(std::span<const std::byte> bytes, std::size_t offset) -> std::uint32_t {
  std::uint32_t value = 0;
  std::uint32_t shift = 0;
  for (const std::byte item : bytes.subspan(offset, sizeof(std::uint32_t))) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(item)) << shift;
    shift += bits_per_byte;
  }
  return value;
}

template <typename Mutator>
auto expect_invalid_control(const Mutator& mutate) -> void {
  auto control = make_control();
  mutate(control);
  const auto bytes = encode_control(control);
  EXPECT_THROW(static_cast<void>(decode_control(bytes)), std::runtime_error);
}

TEST(Crc32c, MatchesKnownVector) {
  const std::string text = "123456789";
  std::vector<std::byte> bytes(text.size());
  std::ranges::transform(text, bytes.begin(),
                         [](char value) { return std::byte{static_cast<unsigned char>(value)}; });
  EXPECT_EQ(crc32c(bytes), known_crc32c);
  EXPECT_EQ(crc32c(std::span<const std::byte>{}), 0U);
}

TEST(ControlFile, EncodingRoundTrips) {
  const auto control = make_control();
  const auto bytes = encode_control(control);
  ASSERT_EQ(bytes.size(), kd_control_size);

  const auto decoded = decode_control(bytes);
  EXPECT_EQ(decoded.magic, control.magic);
  EXPECT_EQ(decoded.format_version, control.format_version);
  EXPECT_EQ(decoded.system_identifier, control.system_identifier);
  EXPECT_EQ(decoded.checkpoint_lsn, control.checkpoint_lsn);
  EXPECT_EQ(decoded.redo_lsn, control.redo_lsn);
  EXPECT_EQ(decoded.timeline, control.timeline);
  EXPECT_EQ(static_cast<std::uint32_t>(decoded.state), static_cast<std::uint32_t>(control.state));
  EXPECT_EQ(decoded.full_page_writes, control.full_page_writes);
  EXPECT_EQ(decoded.next_xid, control.next_xid);
  EXPECT_EQ(decoded.oldest_xid, control.oldest_xid);
  EXPECT_EQ(static_cast<std::uint32_t>(decoded.write_mode),
            static_cast<std::uint32_t>(control.write_mode));
  EXPECT_EQ(decoded.base_id, control.base_id);
  EXPECT_EQ(decoded.previous_base, control.previous_base);
  EXPECT_EQ(decoded.checkpoint_time, control.checkpoint_time);
  EXPECT_EQ(decoded.crc, read_u32_le(bytes, control_crc_offset));
}

TEST(ControlFile, RejectsBadMagic) {
  auto bytes = encode_control(make_control());
  bytes.at(0) = std::byte{0};
  EXPECT_THROW(static_cast<void>(decode_control(bytes)), std::runtime_error);
}

TEST(ControlFile, RejectsBadCrc) {
  auto bytes = encode_control(make_control());
  bytes.at(control_data_offset) = std::byte{corrupt_byte};
  EXPECT_THROW(static_cast<void>(decode_control(bytes)), std::runtime_error);
}

TEST(ControlFile, RejectsUnknownFormat) {
  auto bytes = encode_control(make_control());
  bytes.at(control_format_offset) = std::byte{unknown_format_version};
  EXPECT_THROW(static_cast<void>(decode_control(bytes)), std::runtime_error);
}

TEST(ControlFile, RejectsWrongSize) {
  std::array<std::byte, kd_control_size - 1> short_bytes{};
  EXPECT_THROW(static_cast<void>(decode_control(short_bytes)), std::runtime_error);
  std::array<std::byte, kd_control_size + 1> long_bytes{};
  EXPECT_THROW(static_cast<void>(decode_control(long_bytes)), std::runtime_error);
}

TEST(ControlFile, RejectsInvalidFields) {
  expect_invalid_control([](ControlFile& control) { control.system_identifier = 0; });
  expect_invalid_control([](ControlFile& control) { control.timeline = 0; });
  expect_invalid_control([](ControlFile& control) { control.next_xid = 0; });
  expect_invalid_control([](ControlFile& control) { control.oldest_xid = 0; });
  expect_invalid_control([](ControlFile& control) { control.full_page_writes = 2; });
  expect_invalid_control(
      [](ControlFile& control) { control.state = static_cast<ControlState>(invalid_enum_value); });
  expect_invalid_control([](ControlFile& control) {
    control.write_mode = static_cast<WriteMode>(invalid_enum_value);
  });
}

TEST(DataDirectory, BootstrapCreatesLayout) {
  TempDirectory directory;
  auto data = DataDirectory::bootstrap(directory.path());
  static_cast<void>(data);

  for (const auto name : {"base", "generations", "kd_wal", "kd_xact", "tmp"}) {
    EXPECT_TRUE(std::filesystem::is_directory(directory.path() / name));
  }
  EXPECT_TRUE(std::filesystem::is_regular_file(directory.path() / "KD_VERSION"));
  EXPECT_TRUE(std::filesystem::is_regular_file(directory.path() / "kd_control"));
  EXPECT_TRUE(std::filesystem::is_regular_file(directory.path() / "kd_lock"));
  EXPECT_EQ(read_text(directory.path() / "KD_VERSION"), std::string{kd_version_contents});

  const auto bytes = read_bytes(directory.path() / "kd_control");
  ASSERT_EQ(bytes.size(), kd_control_size);
  const auto control = decode_control(bytes);
  EXPECT_EQ(control.magic, kd_control_magic);
  EXPECT_EQ(control.format_version, kd_format_version);
  EXPECT_NE(control.system_identifier, 0U);
  EXPECT_EQ(control.timeline, 1U);
  EXPECT_EQ(static_cast<std::uint32_t>(control.state),
            static_cast<std::uint32_t>(ControlState::clean));
  EXPECT_EQ(control.full_page_writes, 1U);
  EXPECT_EQ(control.next_xid, 1U);
  EXPECT_EQ(control.oldest_xid, 1U);
  EXPECT_EQ(static_cast<std::uint32_t>(control.write_mode),
            static_cast<std::uint32_t>(WriteMode::autocommit));
  EXPECT_GT(control.checkpoint_time, 0U);
}

TEST(DataDirectory, BootstrapIsIdempotent) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  const auto control_before = read_bytes(directory.path() / "kd_control");
  const auto version_before = read_bytes(directory.path() / "KD_VERSION");
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  EXPECT_TRUE(read_bytes(directory.path() / "kd_control") == control_before);
  EXPECT_TRUE(read_bytes(directory.path() / "KD_VERSION") == version_before);
  EXPECT_FALSE(std::filesystem::exists(directory.path() / "kd_control.tmp"));
}

TEST(DataDirectory, BootstrapCompletesMissingVersion) {
  TempDirectory directory;
  std::uint64_t system_identifier = 0;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    system_identifier = data.control().system_identifier;
  }
  ASSERT_TRUE(std::filesystem::remove(directory.path() / "KD_VERSION"));
  {
    auto data = DataDirectory::bootstrap(directory.path());
    EXPECT_EQ(data.control().system_identifier, system_identifier);
  }
  EXPECT_TRUE(std::filesystem::exists(directory.path() / "KD_VERSION"));
}

TEST(DataDirectory, BootstrapRejectsVersionWithoutControl) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  ASSERT_TRUE(std::filesystem::remove(directory.path() / "kd_control"));
  EXPECT_THROW(static_cast<void>(DataDirectory::bootstrap(directory.path())), std::runtime_error);
}

TEST(DataDirectory, BootstrapRejectsDifferentWriteMode) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  EXPECT_THROW(static_cast<void>(DataDirectory::bootstrap(directory.path(), WriteMode::readonly)),
               std::runtime_error);
}

TEST(DataDirectory, BootstrapRejectsCorruptControl) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  flip_byte(directory.path() / "kd_control", control_data_offset);
  const auto corrupted = read_bytes(directory.path() / "kd_control");
  EXPECT_THROW(static_cast<void>(DataDirectory::bootstrap(directory.path())), std::runtime_error);
  EXPECT_TRUE(read_bytes(directory.path() / "kd_control") == corrupted);
}

TEST(DataDirectory, OpenRejectsUnknownVersion) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  for (const auto version : {"99\n", "abc\n", ""}) {
    write_text(directory.path() / "KD_VERSION", version);
    EXPECT_THROW(static_cast<void>(DataDirectory::open(directory.path())), std::runtime_error);
  }
}

TEST(DataDirectory, OpenRejectsCorruptControl) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  flip_byte(directory.path() / "kd_control", control_data_offset);
  EXPECT_THROW(static_cast<void>(DataDirectory::open(directory.path())), std::runtime_error);
}

TEST(DataDirectory, OpenUninitializedFails) {
  TempDirectory directory;
  EXPECT_THROW(static_cast<void>(DataDirectory::open(directory.path())), std::runtime_error);
}

TEST(DataDirectory, LockReleasedOnDestruction) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    static_cast<void>(data);
  }
  EXPECT_NO_THROW({
    auto data = DataDirectory::open(directory.path());
    EXPECT_EQ(data.path(), directory.path());
  });
}

TEST(DataDirectory, LockRejectsSecondProcess) {
#if defined(_WIN32)
  GTEST_SKIP() << "fork is POSIX-only";
#else
  TempDirectory directory;
  {
    auto owner = DataDirectory::bootstrap(directory.path());
    static_cast<void>(owner);
  }

  std::array<int, 2> ready{};
  std::array<int, 2> release{};
  ASSERT_EQ(::pipe(ready.data()), 0);
  ASSERT_EQ(::pipe(release.data()), 0);

  const auto pid = ::fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    static_cast<void>(::close(ready.at(0)));
    static_cast<void>(::close(release.at(1)));
    try {
      auto child = DataDirectory::open(directory.path());
      static_cast<void>(child);
      const char ready_byte = '1';
      static_cast<void>(::write(ready.at(1), &ready_byte, 1));
      char release_byte = 0;
      static_cast<void>(::read(release.at(0), &release_byte, 1));
      ::_exit(0);
    } catch (...) {
      ::_exit(2);
    }
  }

  static_cast<void>(::close(ready.at(1)));
  static_cast<void>(::close(release.at(0)));
  char ready_byte = 0;
  const auto ready_read = ::read(ready.at(0), &ready_byte, 1);
  if (ready_read != 1 || ready_byte != '1') {
    const char release_byte = '1';
    static_cast<void>(::write(release.at(1), &release_byte, 1));
    int status = 0;
    static_cast<void>(::waitpid(pid, &status, 0));
    FAIL() << "child did not acquire the lock";
  }

  EXPECT_THROW(static_cast<void>(DataDirectory::open(directory.path())), std::runtime_error);

  const char release_byte = '1';
  static_cast<void>(::write(release.at(1), &release_byte, 1));
  int status = 0;
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  EXPECT_NO_THROW({
    auto reopened = DataDirectory::open(directory.path());
    EXPECT_EQ(reopened.path(), directory.path());
  });
#endif
}

TEST(DataDirectory, WriteControlReplacesAtomically) {
  TempDirectory directory;
  {
    auto data = DataDirectory::bootstrap(directory.path());
    auto control = data.control();
    control.next_xid = updated_next_xid;
    data.write_control(control);
    EXPECT_FALSE(std::filesystem::exists(directory.path() / "kd_control.tmp"));
  }
  auto reopened = DataDirectory::open(directory.path());
  EXPECT_EQ(reopened.control().next_xid, updated_next_xid);
}

TEST(DataDirectory, WriteControlRejectsInvalidControl) {
  TempDirectory directory;
  auto data = DataDirectory::bootstrap(directory.path());
  const auto before = read_bytes(directory.path() / "kd_control");
  auto control = data.control();
  control.system_identifier = 0;
  EXPECT_THROW(data.write_control(control), std::runtime_error);
  EXPECT_TRUE(read_bytes(directory.path() / "kd_control") == before);
}

TEST(DataDirectory, DefaultDataDirectory) {
  const auto path = default_data_directory();
  const char* configured = std::getenv("KRAVIDB_DATA");
  if (configured != nullptr && *configured != '\0') {
    EXPECT_EQ(path, std::filesystem::path{configured});
  } else {
    EXPECT_EQ(path.filename(), "kd_data");
  }
}

}  // namespace
