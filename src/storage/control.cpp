module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

module storage.control;

import std;
import storage.crc32c;

namespace kravidb::storage {

namespace {

constexpr std::size_t control_magic_offset = 0;
constexpr std::size_t control_format_offset = 8;
constexpr std::size_t control_system_identifier_offset = 12;
constexpr std::size_t control_checkpoint_lsn_offset = 20;
constexpr std::size_t control_redo_lsn_offset = 28;
constexpr std::size_t control_timeline_offset = 36;
constexpr std::size_t control_state_offset = 40;
constexpr std::size_t control_full_page_writes_offset = 44;
constexpr std::size_t control_next_xid_offset = 48;
constexpr std::size_t control_oldest_xid_offset = 52;
constexpr std::size_t control_write_mode_offset = 56;
constexpr std::size_t control_base_id_offset = 60;
constexpr std::size_t control_previous_base_offset = 68;
constexpr std::size_t control_checkpoint_time_offset = 76;
constexpr std::size_t control_crc_offset = 84;
constexpr std::size_t control_data_size = control_crc_offset;
constexpr std::size_t control_header_size = kd_control_size;

constexpr std::uint32_t bits_per_byte = 8;
constexpr std::uint32_t byte_mask = 0xFF;

constexpr std::size_t control_u32_bytes = 4;
constexpr std::size_t control_u64_bytes = 8;

constexpr std::uint32_t control_full_page_writes_enabled = 1;
constexpr std::uint32_t control_state_recovery = static_cast<std::uint32_t>(ControlState::recovery);
constexpr std::uint32_t control_write_mode_readonly =
    static_cast<std::uint32_t>(WriteMode::readonly);

constexpr std::array<std::string_view, 5> data_subdirectories = {"base", "generations", "kd_wal",
                                                                 "kd_xact", "tmp"};

constexpr std::string_view control_filename = "kd_control";
constexpr std::string_view version_filename = "KD_VERSION";
constexpr std::string_view lock_filename = "kd_lock";

#if defined(_WIN32)
auto last_error(const std::filesystem::path& path) -> std::system_error {
  return std::system_error{static_cast<int>(::GetLastError()), std::system_category(),
                           path.string()};
}
#else
auto last_error(const std::filesystem::path& path) -> std::system_error {
  return std::system_error{errno, std::generic_category(), path.string()};
}
#endif

auto store_u32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) -> void {
  for (std::size_t index = 0; index < control_u32_bytes; ++index) {
    bytes.subspan(offset + index, 1).front() =
        std::byte{static_cast<std::uint8_t>(value & byte_mask)};
    value >>= bits_per_byte;
  }
}

auto store_u64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) -> void {
  for (std::size_t index = 0; index < control_u64_bytes; ++index) {
    bytes.subspan(offset + index, 1).front() =
        std::byte{static_cast<std::uint8_t>(value & byte_mask)};
    value >>= bits_per_byte;
  }
}

auto read_u32(std::span<const std::byte> bytes, std::size_t offset) -> std::uint32_t {
  std::uint32_t value = 0;
  std::uint32_t shift = 0;
  for (const std::byte item : bytes.subspan(offset, control_u32_bytes)) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(item)) << shift;
    shift += bits_per_byte;
  }
  return value;
}

auto read_u64(std::span<const std::byte> bytes, std::size_t offset) -> std::uint64_t {
  std::uint64_t value = 0;
  std::uint32_t shift = 0;
  for (const std::byte item : bytes.subspan(offset, control_u64_bytes)) {
    value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(item)) << shift;
    shift += bits_per_byte;
  }
  return value;
}

auto validate_control(const ControlFile& control) -> void {
  if (control.system_identifier == 0) {
    throw std::runtime_error{"kd_control: system_identifier is zero"};
  }
  if (control.timeline == 0) {
    throw std::runtime_error{"kd_control: timeline is zero"};
  }
  if (control.next_xid == 0) {
    throw std::runtime_error{"kd_control: next_xid is zero"};
  }
  if (control.oldest_xid == 0) {
    throw std::runtime_error{"kd_control: oldest_xid is zero"};
  }
  if (control.full_page_writes > control_full_page_writes_enabled) {
    throw std::runtime_error{"kd_control: invalid full_page_writes value"};
  }
  if (static_cast<std::uint32_t>(control.state) > control_state_recovery) {
    throw std::runtime_error{"kd_control: invalid state value"};
  }
  if (static_cast<std::uint32_t>(control.write_mode) > control_write_mode_readonly) {
    throw std::runtime_error{"kd_control: invalid write_mode value"};
  }
}

auto random_system_identifier() -> std::uint64_t {
  std::random_device device;
  std::seed_seq seed{device(), device(), device(), device()};
  std::mt19937_64 engine{seed};
  auto value = engine();
  while (value == 0) {
    value = engine();
  }
  return value;
}

auto make_initial_control(WriteMode mode) -> ControlFile {
  ControlFile control{};
  control.system_identifier = random_system_identifier();
  control.write_mode = mode;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  control.checkpoint_time =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
  return control;
}

#if defined(_WIN32)
auto write_all(HANDLE handle, std::span<const std::byte> bytes, const std::filesystem::path& path)
    -> void {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const auto remaining = bytes.size() - done;
    const auto chunk =
        static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
    DWORD written = 0;
    if (::WriteFile(handle, bytes.subspan(done).data(), chunk, &written, nullptr) == 0) {
      throw last_error(path);
    }
    if (written == 0) {
      throw std::runtime_error{"kd_control: WriteFile wrote zero bytes"};
    }
    done += static_cast<std::size_t>(written);
  }
}
#else
auto write_all(int descriptor, std::span<const std::byte> bytes, const std::filesystem::path& path)
    -> void {
  std::size_t done = 0;
  while (done < bytes.size()) {
    const auto count = ::write(descriptor, bytes.subspan(done).data(), bytes.size() - done);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw last_error(path);
    }
    if (count == 0) {
      throw std::runtime_error{"kd_control: write wrote zero bytes"};
    }
    done += static_cast<std::size_t>(count);
  }
}
#endif

auto remove_file(const std::filesystem::path& path) noexcept -> void {
  std::error_code error;
  static_cast<void>(std::filesystem::remove(path, error));
}

#if defined(_WIN32)
auto fsync_directory(const std::filesystem::path& directory) -> void {
  static_cast<void>(directory);
}
#else
auto fsync_directory(const std::filesystem::path& directory) -> void {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (descriptor < 0) {
    throw last_error(directory);
  }
  const int result = ::fsync(descriptor);
  const int saved_errno = errno;
  static_cast<void>(::close(descriptor));
  if (result != 0) {
    throw std::system_error{saved_errno, std::generic_category(), directory.string()};
  }
}
#endif

auto write_file_atomic(const std::filesystem::path& target, std::span<const std::byte> bytes)
    -> void {
  auto temp = target;
  temp += ".tmp";

#if defined(_WIN32)
  const HANDLE handle = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    throw last_error(temp);
  }
  HANDLE open_handle = handle;
  try {
    write_all(open_handle, bytes, temp);
    if (::FlushFileBuffers(open_handle) == 0) {
      throw last_error(temp);
    }
    if (::CloseHandle(open_handle) == 0) {
      open_handle = INVALID_HANDLE_VALUE;
      throw last_error(temp);
    }
    open_handle = INVALID_HANDLE_VALUE;
  } catch (...) {
    if (open_handle != INVALID_HANDLE_VALUE) {
      static_cast<void>(::CloseHandle(open_handle));
    }
    static_cast<void>(::DeleteFileW(temp.c_str()));
    throw;
  }
  if (::MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    const auto error = ::GetLastError();
    static_cast<void>(::DeleteFileW(temp.c_str()));
    throw std::system_error{static_cast<int>(error), std::system_category(), target.string()};
  }
#else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int descriptor = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (descriptor < 0) {
    throw last_error(temp);
  }
  bool open_descriptor = true;
  try {
    write_all(descriptor, bytes, temp);
    if (::fsync(descriptor) != 0) {
      throw last_error(temp);
    }
    if (::close(descriptor) != 0) {
      open_descriptor = false;
      throw last_error(temp);
    }
    open_descriptor = false;
  } catch (...) {
    if (open_descriptor) {
      static_cast<void>(::close(descriptor));
    }
    remove_file(temp);
    throw;
  }
  std::error_code rename_error;
  std::filesystem::rename(temp, target, rename_error);
  if (rename_error) {
    remove_file(temp);
    throw std::system_error{rename_error, target.string()};
  }
  fsync_directory(target.parent_path());
#endif
}

auto read_control_file(const std::filesystem::path& path) -> ControlFile {
  std::ifstream stream{path, std::ios::binary};
  if (!stream) {
    throw std::runtime_error{"cannot open kd_control '" + path.string() + "'"};
  }
  stream.seekg(0, std::ios::end);
  const auto size = stream.tellg();
  stream.seekg(0, std::ios::beg);
  if (size != static_cast<std::streamoff>(control_header_size)) {
    throw std::runtime_error{"kd_control '" + path.string() + "' has an invalid size"};
  }
  std::array<char, control_header_size> raw{};
  stream.read(raw.data(), static_cast<std::streamsize>(raw.size()));
  if (!stream) {
    throw std::runtime_error{"cannot read kd_control '" + path.string() + "'"};
  }
  std::array<std::byte, control_header_size> bytes{};
  std::ranges::transform(raw, bytes.begin(),
                         [](char value) { return std::byte{static_cast<unsigned char>(value)}; });
  return decode_control(bytes);
}

auto validate_version_file(const std::filesystem::path& path) -> void {
  std::ifstream stream{path, std::ios::binary};
  if (!stream) {
    throw std::runtime_error{"cannot open KD_VERSION '" + path.string() + "'"};
  }
  std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  if (contents != kd_version_contents) {
    throw std::runtime_error{"KD_VERSION '" + path.string() +
                             "' has unsupported contents (expected '1\\n')"};
  }
}

auto write_version_file(const std::filesystem::path& path) -> void {
  std::array<std::byte, kd_version_contents.size()> bytes{};
  std::ranges::transform(kd_version_contents, bytes.begin(),
                         [](char value) { return std::byte{static_cast<unsigned char>(value)}; });
  write_file_atomic(path, bytes);
}

auto ensure_directory(const std::filesystem::path& directory) -> void {
  std::error_code error;
  const bool created = std::filesystem::create_directories(directory, error);
  if (error) {
    throw std::system_error{error, directory.string()};
  }
  static_cast<void>(created);
#if !defined(_WIN32)
  if (created) {
    std::error_code permission_error;
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, permission_error);
    if (permission_error) {
      throw std::system_error{permission_error, directory.string()};
    }
  }
#endif
}

auto create_layout(const std::filesystem::path& directory) -> void {
  ensure_directory(directory);
  for (const auto name : data_subdirectories) {
    ensure_directory(directory / name);
  }
}

class LockHandle {
  public:
  static auto acquire(const std::filesystem::path& lock_path) -> std::unique_ptr<LockHandle> {
#if defined(_WIN32)
    const HANDLE handle = ::CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      throw last_error(lock_path);
    }
    OVERLAPPED overlapped{};
    if (::LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD,
                     MAXDWORD, &overlapped) == 0) {
      const auto error = ::GetLastError();
      static_cast<void>(::CloseHandle(handle));
      if (error == ERROR_LOCK_VIOLATION) {
        throw std::runtime_error{"data directory '" + lock_path.parent_path().string() +
                                 "' is already in use (kd_lock)"};
      }
      throw std::system_error{static_cast<int>(error), std::system_category(), lock_path.string()};
    }
    auto result = std::unique_ptr<LockHandle>{new LockHandle{handle}};
    result->write_pid();
    return result;
#else
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0) {
      throw last_error(lock_path);
    }
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
      const int error = errno;
      static_cast<void>(::close(descriptor));
      if (error == EWOULDBLOCK || error == EAGAIN) {
        throw std::runtime_error{"data directory '" + lock_path.parent_path().string() +
                                 "' is already in use (kd_lock)"};
      }
      throw std::system_error{error, std::generic_category(), lock_path.string()};
    }
    auto result = std::unique_ptr<LockHandle>{new LockHandle{descriptor}};
    result->write_pid();
    return result;
#endif
  }

  ~LockHandle() {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) {
      OVERLAPPED overlapped{};
      static_cast<void>(::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped));
      static_cast<void>(::CloseHandle(handle_));
    }
#else
    if (descriptor_ >= 0) {
      static_cast<void>(::flock(descriptor_, LOCK_UN));
      static_cast<void>(::close(descriptor_));
    }
#endif
  }

  LockHandle(const LockHandle&) = delete;
  auto operator=(const LockHandle&) -> LockHandle& = delete;
  LockHandle(LockHandle&&) = delete;
  auto operator=(LockHandle&&) -> LockHandle& = delete;

  private:
#if defined(_WIN32)
  explicit LockHandle(HANDLE handle) : handle_{handle} {}
#else
  explicit LockHandle(int descriptor) : descriptor_{descriptor} {}
#endif

  auto write_pid() -> void {
#if defined(_WIN32)
    const auto pid = std::to_string(::GetCurrentProcessId());
#else
    const auto pid = std::to_string(::getpid());
#endif
    const auto contents = pid + "\n";
#if defined(_WIN32)
    static_cast<void>(::SetFilePointerEx(handle_, LARGE_INTEGER{}, nullptr, FILE_BEGIN));
    static_cast<void>(::SetEndOfFile(handle_));
    DWORD written = 0;
    static_cast<void>(::WriteFile(handle_, contents.data(), static_cast<DWORD>(contents.size()),
                                  &written, nullptr));
#else
    static_cast<void>(::ftruncate(descriptor_, 0));
    static_cast<void>(::pwrite(descriptor_, contents.data(), contents.size(), 0));
#endif
  }

#if defined(_WIN32)
  HANDLE handle_{INVALID_HANDLE_VALUE};
#else
  int descriptor_{-1};
#endif
};

}  // namespace

auto encode_control(const ControlFile& control) -> std::array<std::byte, kd_control_size> {
  std::array<std::byte, kd_control_size> bytes{};
  store_u64(bytes, control_magic_offset, control.magic);
  store_u32(bytes, control_format_offset, control.format_version);
  store_u64(bytes, control_system_identifier_offset, control.system_identifier);
  store_u64(bytes, control_checkpoint_lsn_offset, control.checkpoint_lsn);
  store_u64(bytes, control_redo_lsn_offset, control.redo_lsn);
  store_u32(bytes, control_timeline_offset, control.timeline);
  store_u32(bytes, control_state_offset, static_cast<std::uint32_t>(control.state));
  store_u32(bytes, control_full_page_writes_offset, control.full_page_writes);
  store_u32(bytes, control_next_xid_offset, control.next_xid);
  store_u32(bytes, control_oldest_xid_offset, control.oldest_xid);
  store_u32(bytes, control_write_mode_offset, static_cast<std::uint32_t>(control.write_mode));
  store_u64(bytes, control_base_id_offset, control.base_id);
  store_u64(bytes, control_previous_base_offset, control.previous_base);
  store_u64(bytes, control_checkpoint_time_offset, control.checkpoint_time);
  const auto crc = crc32c(std::span<const std::byte>{bytes.data(), control_data_size});
  store_u32(bytes, control_crc_offset, crc);
  return bytes;
}

auto decode_control(std::span<const std::byte> bytes) -> ControlFile {
  if (bytes.size() != control_header_size) {
    throw std::runtime_error{"kd_control: invalid size"};
  }
  if (read_u64(bytes, control_magic_offset) != kd_control_magic) {
    throw std::runtime_error{"kd_control: invalid magic"};
  }
  if (read_u32(bytes, control_format_offset) != kd_format_version) {
    throw std::runtime_error{"kd_control: unsupported format version"};
  }
  const auto stored_crc = read_u32(bytes, control_crc_offset);
  const auto computed_crc = crc32c(bytes.first(control_data_size));
  if (stored_crc != computed_crc) {
    throw std::runtime_error{"kd_control: CRC mismatch"};
  }

  ControlFile control{};
  control.magic = read_u64(bytes, control_magic_offset);
  control.format_version = read_u32(bytes, control_format_offset);
  control.system_identifier = read_u64(bytes, control_system_identifier_offset);
  control.checkpoint_lsn = read_u64(bytes, control_checkpoint_lsn_offset);
  control.redo_lsn = read_u64(bytes, control_redo_lsn_offset);
  control.timeline = read_u32(bytes, control_timeline_offset);
  control.state = static_cast<ControlState>(read_u32(bytes, control_state_offset));
  control.full_page_writes = read_u32(bytes, control_full_page_writes_offset);
  control.next_xid = read_u32(bytes, control_next_xid_offset);
  control.oldest_xid = read_u32(bytes, control_oldest_xid_offset);
  control.write_mode = static_cast<WriteMode>(read_u32(bytes, control_write_mode_offset));
  control.base_id = read_u64(bytes, control_base_id_offset);
  control.previous_base = read_u64(bytes, control_previous_base_offset);
  control.checkpoint_time = read_u64(bytes, control_checkpoint_time_offset);
  control.crc = stored_crc;
  validate_control(control);
  return control;
}

auto default_data_directory() -> std::filesystem::path {
  const char* configured = std::getenv("KRAVIDB_DATA");
  if (configured != nullptr && *configured != '\0') {
    return configured;
  }
  return "kd_data";
}

struct DataDirectory::Impl {
  Impl(std::filesystem::path directory, ControlFile control_file,
       std::unique_ptr<LockHandle> instance_lock)
      : directory{std::move(directory)}, control{control_file}, lock{std::move(instance_lock)} {}

  std::filesystem::path directory;
  ControlFile control;
  std::unique_ptr<LockHandle> lock;
};

auto DataDirectory::bootstrap(std::filesystem::path directory, WriteMode mode) -> DataDirectory {
  create_layout(directory);
  auto lock = LockHandle::acquire(directory / lock_filename);
  const auto version_path = directory / version_filename;
  const auto control_path = directory / control_filename;
  const bool has_version = std::filesystem::exists(version_path);
  const bool has_control = std::filesystem::exists(control_path);

  if (has_version) {
    validate_version_file(version_path);
  }
  if (has_control) {
    auto control = read_control_file(control_path);
    if (control.write_mode != mode) {
      throw std::runtime_error{"data directory '" + directory.string() +
                               "' was bootstrapped with a different write mode"};
    }
    if (!has_version) {
      write_version_file(version_path);
    }
    return DataDirectory{std::make_unique<Impl>(std::move(directory), control, std::move(lock))};
  }
  if (has_version) {
    throw std::runtime_error{"data directory '" + directory.string() +
                             "' is incomplete: KD_VERSION exists but kd_control is missing"};
  }

  auto control = make_initial_control(mode);
  write_file_atomic(control_path, encode_control(control));
  write_version_file(version_path);
  return DataDirectory{std::make_unique<Impl>(std::move(directory), control, std::move(lock))};
}

auto DataDirectory::open(std::filesystem::path directory) -> DataDirectory {
  const auto version_path = directory / version_filename;
  const auto control_path = directory / control_filename;
  if (!std::filesystem::exists(version_path)) {
    throw std::runtime_error{"data directory '" + directory.string() +
                             "' is not initialized: KD_VERSION is missing"};
  }
  validate_version_file(version_path);
  if (!std::filesystem::exists(control_path)) {
    throw std::runtime_error{"data directory '" + directory.string() +
                             "' is not initialized: kd_control is missing"};
  }
  auto lock = LockHandle::acquire(directory / lock_filename);
  auto control = read_control_file(control_path);
  return DataDirectory{std::make_unique<Impl>(std::move(directory), control, std::move(lock))};
}

DataDirectory::DataDirectory(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

DataDirectory::~DataDirectory() = default;

DataDirectory::DataDirectory(DataDirectory&&) noexcept = default;

auto DataDirectory::operator=(DataDirectory&&) noexcept -> DataDirectory& = default;

auto DataDirectory::path() const -> const std::filesystem::path& {
  return impl_->directory;
}

auto DataDirectory::control() const -> const ControlFile& {
  return impl_->control;
}

auto DataDirectory::write_control(const ControlFile& control) -> void {
  const auto encoded = encode_control(control);
  const auto validated = decode_control(encoded);
  write_file_atomic(impl_->directory / control_filename, encoded);
  impl_->control = validated;
}

}  // namespace kravidb::storage
