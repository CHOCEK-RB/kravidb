module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

module storage.file_wal;

import std;
import storage.crc32c;
import storage.wal;

namespace kravidb::storage {

namespace {

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

constexpr std::size_t record_header_size = 24;
constexpr std::size_t record_total_len_offset = 0;
constexpr std::size_t record_xid_offset = 4;
constexpr std::size_t record_info_offset = 16;
constexpr std::size_t record_rmid_offset = 17;
constexpr std::size_t record_padding_offset = 18;
constexpr std::size_t record_padding_size = 2;
constexpr std::size_t record_crc_offset = 20;
constexpr std::size_t payload_marker_size = 1;

constexpr std::size_t block_header_size = 16;
constexpr std::size_t block_fork_offset = 1;
constexpr std::size_t block_flags_offset = 2;
constexpr std::size_t block_number_offset = 4;
constexpr std::size_t block_data_length_offset = 8;
constexpr std::size_t block_image_length_offset = 12;

constexpr std::uint16_t block_flag_data = 0x0001;
constexpr std::uint16_t block_flag_image = 0x0002;
constexpr std::uint16_t block_flags_known =
    static_cast<std::uint16_t>(block_flag_data | block_flag_image);

constexpr std::uint8_t main_data_marker = 0xFF;
constexpr std::size_t max_block_refs = 5;
constexpr std::size_t max_payload_size = std::numeric_limits<std::uint32_t>::max();

constexpr std::uint32_t bits_per_byte = 8;
constexpr std::uint32_t bits_per_u32 = 32;
constexpr std::uint32_t bytes_per_u16 = 2;
constexpr std::uint32_t bytes_per_u32 = 4;
constexpr std::uint32_t bytes_per_u64 = 8;
constexpr std::uint32_t byte_mask = 0xFF;

constexpr std::uint32_t crc_initial = 0xFFFFFFFFU;

constexpr std::size_t segment_parts = 3;
constexpr std::size_t hex_digits_per_part = 8;
constexpr std::size_t segment_name_length = segment_parts * hex_digits_per_part;
constexpr std::uint32_t hex_shift = 4;
constexpr std::size_t timeline_part = 0;
constexpr std::size_t log_part = 1;
constexpr std::size_t segment_part = 2;
constexpr std::uint32_t hex_a_value = 10;

auto byte_at(std::span<const std::byte> bytes, std::size_t offset) -> std::byte {
  return bytes.subspan(offset, 1).front();
}

auto read_u16(std::span<const std::byte> bytes, std::size_t offset) -> std::uint16_t {
  std::uint32_t value = 0;
  std::uint32_t shift = 0;
  for (const std::byte item : bytes.subspan(offset, bytes_per_u16)) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(item)) << shift;
    shift += bits_per_byte;
  }
  return static_cast<std::uint16_t>(value);
}

auto read_u32(std::span<const std::byte> bytes, std::size_t offset) -> std::uint32_t {
  std::uint32_t value = 0;
  std::uint32_t shift = 0;
  for (const std::byte item : bytes.subspan(offset, bytes_per_u32)) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(item)) << shift;
    shift += bits_per_byte;
  }
  return value;
}

auto read_u64(std::span<const std::byte> bytes, std::size_t offset) -> std::uint64_t {
  std::uint64_t value = 0;
  std::uint32_t shift = 0;
  for (const std::byte item : bytes.subspan(offset, bytes_per_u64)) {
    value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(item)) << shift;
    shift += bits_per_byte;
  }
  return value;
}

auto append_u16(std::vector<std::byte>& bytes, std::uint16_t value) -> void {
  for (std::uint32_t index = 0; index < bytes_per_u16; ++index) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(value & byte_mask)});
    value = static_cast<std::uint16_t>(value >> bits_per_byte);
  }
}

auto append_u32(std::vector<std::byte>& bytes, std::uint32_t value) -> void {
  for (std::uint32_t index = 0; index < bytes_per_u32; ++index) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(value & byte_mask)});
    value >>= bits_per_byte;
  }
}

auto append_u64(std::vector<std::byte>& bytes, std::uint64_t value) -> void {
  for (std::uint32_t index = 0; index < bytes_per_u64; ++index) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(value & byte_mask)});
    value >>= bits_per_byte;
  }
}

auto store_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) -> void {
  for (std::uint32_t index = 0; index < bytes_per_u32; ++index) {
    bytes.at(offset + index) = std::byte{static_cast<std::uint8_t>(value & byte_mask)};
    value >>= bits_per_byte;
  }
}

constexpr std::size_t page_magic_offset = 0;
constexpr std::size_t page_info_offset = 2;
constexpr std::size_t page_timeline_offset = 4;
constexpr std::size_t page_addr_offset = 8;
constexpr std::size_t page_rem_len_offset = 16;
constexpr std::size_t page_reserved_offset = 20;
constexpr std::size_t long_page_sysid_offset = 24;
constexpr std::size_t long_page_segment_size_offset = 32;
constexpr std::size_t long_page_block_size_offset = 36;
constexpr std::uint16_t page_info_known =
    static_cast<std::uint16_t>(wal_page_info_long_header | wal_page_info_continuation);

auto page_start(LSN lsn) -> LSN {
  return lsn - (lsn % static_cast<LSN>(wal_page_size));
}

auto encode_page_header(const WalPageHeader& header)
    -> std::array<std::byte, wal_page_header_size> {
  std::vector<std::byte> bytes;
  bytes.reserve(wal_page_header_size);
  append_u16(bytes, header.magic);
  append_u16(bytes, header.info);
  append_u32(bytes, header.timeline);
  append_u64(bytes, header.pageaddr);
  append_u32(bytes, header.rem_len);
  append_u32(bytes, header.reserved);
  std::array<std::byte, wal_page_header_size> result{};
  std::ranges::copy(bytes, result.begin());
  return result;
}

auto decode_page_header(std::span<const std::byte> bytes) -> WalPageHeader {
  if (bytes.size() < wal_page_header_size) {
    throw std::invalid_argument{"FileWal: truncated page header"};
  }
  return WalPageHeader{.magic = read_u16(bytes, page_magic_offset),
                       .info = read_u16(bytes, page_info_offset),
                       .timeline = read_u32(bytes, page_timeline_offset),
                       .pageaddr = read_u64(bytes, page_addr_offset),
                       .rem_len = read_u32(bytes, page_rem_len_offset),
                       .reserved = read_u32(bytes, page_reserved_offset)};
}

auto encode_long_page_header(const WalLongPageHeader& header)
    -> std::array<std::byte, wal_long_page_header_size> {
  const auto page = encode_page_header(header.page);
  std::vector<std::byte> bytes(page.begin(), page.end());
  append_u64(bytes, header.sysid);
  append_u32(bytes, header.segment_size);
  append_u32(bytes, header.block_size);
  std::array<std::byte, wal_long_page_header_size> result{};
  std::ranges::copy(bytes, result.begin());
  return result;
}

auto decode_long_page_header(std::span<const std::byte> bytes) -> WalLongPageHeader {
  if (bytes.size() < wal_long_page_header_size) {
    throw std::invalid_argument{"FileWal: truncated long page header"};
  }
  return WalLongPageHeader{.page = decode_page_header(bytes.first(wal_page_header_size)),
                           .sysid = read_u64(bytes, long_page_sysid_offset),
                           .segment_size = read_u32(bytes, long_page_segment_size_offset),
                           .block_size = read_u32(bytes, long_page_block_size_offset)};
}

auto compute_crc(std::span<const std::byte> bytes) -> std::uint32_t {
  std::uint32_t crc = crc_initial;
  crc = crc32c_update(crc, bytes.subspan(0, record_crc_offset));
  const std::array<std::byte, bytes_per_u32> zero_crc{};
  crc = crc32c_update(crc, zero_crc);
  crc = crc32c_update(crc, bytes.subspan(record_crc_offset + bytes_per_u32));
  return ~crc;
}

auto hex_value(char digit) -> std::optional<std::uint32_t> {
  if (digit >= '0' && digit <= '9') {
    return static_cast<std::uint32_t>(digit - '0');
  }
  if (digit >= 'a' && digit <= 'f') {
    return static_cast<std::uint32_t>(digit - 'a') + hex_a_value;
  }
  return std::nullopt;
}

auto parse_segment_name(std::string_view name, std::uint32_t timeline)
    -> std::optional<std::uint64_t> {
  if (name.size() != segment_name_length) {
    return std::nullopt;
  }
  std::array<std::uint32_t, segment_parts> parts{};
  for (std::size_t part = 0; part < parts.size(); ++part) {
    std::uint32_t value = 0;
    for (std::size_t digit = 0; digit < hex_digits_per_part; ++digit) {
      const auto nibble = hex_value(name.at(part * hex_digits_per_part + digit));
      if (!nibble.has_value()) {
        return std::nullopt;
      }
      value = (value << hex_shift) | nibble.value();
    }
    parts.at(part) = value;
  }
  if (parts.at(timeline_part) != timeline) {
    return std::nullopt;
  }
  return (static_cast<std::uint64_t>(parts.at(log_part)) << bits_per_u32) | parts.at(segment_part);
}

}  // namespace

namespace detail {

/// \brief Minimal platform file handle with positional I/O.
class OsFile {
  public:
  static auto open(const std::filesystem::path& path, bool create) -> std::unique_ptr<OsFile> {
#if defined(_WIN32)
    const auto handle =
        ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                      create ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      if (!create && ::GetLastError() == ERROR_FILE_NOT_FOUND) {
        return nullptr;
      }
      throw last_error(path);
    }
    return std::unique_ptr<OsFile>{new OsFile{handle}};
#else
    std::uint32_t flags = static_cast<std::uint32_t>(O_RDWR | O_CLOEXEC);
    if (create) {
      flags |= static_cast<std::uint32_t>(O_CREAT);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = ::open(path.c_str(), static_cast<int>(flags), 0600);
    if (descriptor < 0) {
      if (!create && errno == ENOENT) {
        return nullptr;
      }
      throw last_error(path);
    }
    return std::unique_ptr<OsFile>{new OsFile{descriptor}};
#endif
  }

  ~OsFile() {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) {
      static_cast<void>(::CloseHandle(handle_));
    }
#else
    if (descriptor_ >= 0) {
      static_cast<void>(::close(descriptor_));
    }
#endif
  }

  OsFile(const OsFile&) = delete;
  auto operator=(const OsFile&) -> OsFile& = delete;
  OsFile(OsFile&&) = delete;
  auto operator=(OsFile&&) -> OsFile& = delete;

  auto read_at(std::span<std::byte> out, std::uint64_t offset) -> void {
#if defined(_WIN32)
    std::size_t done = 0;
    while (done < out.size()) {
      OVERLAPPED overlapped{};
      const auto position = offset + static_cast<std::uint64_t>(done);
      overlapped.Offset = static_cast<DWORD>(position);
      overlapped.OffsetHigh = static_cast<DWORD>(position >> bits_per_u32);
      const auto remaining = out.size() - done;
      const auto chunk =
          static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
      DWORD read = 0;
      if (::ReadFile(handle_, out.subspan(done).data(), chunk, &read, &overlapped) == 0) {
        if (::GetLastError() == ERROR_HANDLE_EOF) {
          break;
        }
        throw last_error({});
      }
      if (read == 0) {
        break;
      }
      done += static_cast<std::size_t>(read);
    }
    std::ranges::fill(out.subspan(done), std::byte{0});
#else
    std::size_t done = 0;
    while (done < out.size()) {
      const auto count = ::pread(descriptor_, out.subspan(done).data(), out.size() - done,
                                 static_cast<off_t>(offset + static_cast<std::uint64_t>(done)));
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw last_error({});
      }
      if (count == 0) {
        break;
      }
      done += static_cast<std::size_t>(count);
    }
    std::ranges::fill(out.subspan(done), std::byte{0});
#endif
  }

  auto write_at(std::span<const std::byte> input, std::uint64_t offset) -> void {
#if defined(_WIN32)
    std::size_t done = 0;
    while (done < input.size()) {
      OVERLAPPED overlapped{};
      const auto position = offset + static_cast<std::uint64_t>(done);
      overlapped.Offset = static_cast<DWORD>(position);
      overlapped.OffsetHigh = static_cast<DWORD>(position >> bits_per_u32);
      const auto remaining = input.size() - done;
      const auto chunk =
          static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
      DWORD written = 0;
      if (::WriteFile(handle_, input.subspan(done).data(), chunk, &written, &overlapped) == 0) {
        throw last_error({});
      }
      if (written == 0) {
        throw std::runtime_error{"WriteFile wrote zero bytes"};
      }
      done += static_cast<std::size_t>(written);
    }
#else
    std::size_t done = 0;
    while (done < input.size()) {
      const auto count = ::pwrite(descriptor_, input.subspan(done).data(), input.size() - done,
                                  static_cast<off_t>(offset + static_cast<std::uint64_t>(done)));
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw last_error({});
      }
      if (count == 0) {
        throw std::runtime_error{"pwrite wrote zero bytes"};
      }
      done += static_cast<std::size_t>(count);
    }
#endif
  }

  auto truncate(std::uint64_t size) -> void {
#if defined(_WIN32)
    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(size);
    if (::SetFilePointerEx(handle_, distance, nullptr, FILE_BEGIN) == 0) {
      throw last_error({});
    }
    if (::SetEndOfFile(handle_) == 0) {
      throw last_error({});
    }
#else
    if (::ftruncate(descriptor_, static_cast<off_t>(size)) != 0) {
      throw last_error({});
    }
#endif
  }

  auto fsync() -> void {
#if defined(_WIN32)
    if (::FlushFileBuffers(handle_) == 0) {
      throw last_error({});
    }
#else
    if (::fsync(descriptor_) != 0) {
      throw last_error({});
    }
#endif
  }

  private:
#if defined(_WIN32)
  explicit OsFile(HANDLE handle) : handle_{handle} {}
  HANDLE handle_{INVALID_HANDLE_VALUE};
#else
  explicit OsFile(int descriptor) : descriptor_{descriptor} {}
  int descriptor_{-1};
#endif
};

/// \brief Flushes a directory entry after creating or deleting a segment.
auto fsync_directory(const std::filesystem::path& directory) -> void {
#if defined(_WIN32)
  static_cast<void>(directory);
#else
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
#endif
}

}  // namespace detail

class FileWal::Impl {
  public:
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  Impl(std::filesystem::path directory, std::uint32_t timeline, std::size_t segment_size)
      : directory_{std::move(directory)}, timeline_{timeline}, segment_size_{segment_size} {
    if (directory_.empty()) {
      throw std::invalid_argument{"FileWal: empty directory"};
    }
    if (timeline_ == 0) {
      throw std::invalid_argument{"FileWal: timeline 0 is reserved"};
    }
    if (segment_size_ < 2 * wal_page_size) {
      throw std::invalid_argument{"FileWal: segment size must hold at least two pages"};
    }
    if (segment_size_ % wal_page_size != 0) {
      throw std::invalid_argument{"FileWal: segment size must be a multiple of the page size"};
    }
    if (segment_size_ > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument{"FileWal: unsupported segment size"};
    }
    static_cast<void>(std::filesystem::create_directories(directory_));
    scan_locked();
  }

  auto insert(const WalRecord& record) -> LSN {
    std::lock_guard lock{mutex_};
    return insert_locked(record);
  }

  auto flush(LSN upto) -> void {
    std::lock_guard lock{mutex_};
    flush_locked(upto);
  }

  [[nodiscard]] auto needs_flush(LSN upto) const -> bool {
    std::lock_guard lock{mutex_};
    return std::ranges::any_of(
        pending_, [upto](const PendingRecord& pending) { return pending.start <= upto; });
  }

  [[nodiscard]] auto read(LSN from) -> WalReadResult {
    std::lock_guard lock{mutex_};
    return read_locked(from);
  }

  auto switch_segment() -> void {
    std::lock_guard lock{mutex_};
    const auto offset = inserted_ % static_cast<LSN>(segment_size_);
    if (offset == 0) {
      return;
    }
    pad_to(inserted_ + (static_cast<LSN>(segment_size_) - offset));
  }

  auto recycle(LSN before) -> void {
    std::lock_guard lock{mutex_};
    if (before > inserted_) {
      throw std::invalid_argument{"FileWal: cannot recycle past the inserted LSN"};
    }
    flush_locked(inserted_);
    const auto keep_from = before / static_cast<LSN>(segment_size_);

    std::vector<std::filesystem::path> remove_paths;
    for (const auto& entry : std::filesystem::directory_iterator{directory_}) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto index = parse_segment_name(entry.path().filename().string(), timeline_);
      if (!index.has_value() || index.value() >= keep_from) {
        continue;
      }
      remove_paths.push_back(entry.path());
    }

    for (auto iterator = files_.begin(); iterator != files_.end();) {
      if (iterator->first < keep_from) {
        iterator = files_.erase(iterator);
      } else {
        ++iterator;
      }
    }

    bool removed = false;
    for (const auto& path : remove_paths) {
      std::error_code error;
      const bool erased = std::filesystem::remove(path, error);
      if (error) {
        throw std::system_error{error, path.string()};
      }
      removed = removed || erased;
    }
    if (removed) {
      detail::fsync_directory(directory_);
    }
  }

  auto close() -> void {
    std::lock_guard lock{mutex_};
    flush_locked(inserted_);
    files_.clear();
  }

  private:
  struct PendingRecord {
    LSN start{invalid_lsn};
    LSN end{invalid_lsn};
  };

  class TruncatedRecord : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
  };

  struct PeekResult {
    enum class Kind { record, padding, truncated };

    Kind kind{Kind::padding};
    std::uint32_t total{0};
  };

  struct FramedRecord {
    LSN start{invalid_lsn};
    LSN end{invalid_lsn};
    std::vector<std::byte> bytes;
  };

  struct ReadPrefix {
    std::vector<std::byte> bytes;
    LSN next{invalid_lsn};
  };

  [[nodiscard]] static auto continuation_flag(const WalPageHeader& header) -> bool {
    return (header.info & wal_page_info_continuation) != 0;
  }

  [[nodiscard]] static auto long_header_flag(const WalPageHeader& header) -> bool {
    return (header.info & wal_page_info_long_header) != 0;
  }

  auto validate_record_total(std::uint32_t total) const -> void {
    if (total < record_header_size || total > segment_size_) {
      throw std::runtime_error{"FileWal: invalid record length"};
    }
  }

  auto validate_page_header(const WalPageHeader& header, LSN pageaddr, bool allow_long) const
      -> void {
    if (header.magic != wal_page_magic) {
      throw std::runtime_error{"FileWal: invalid page magic"};
    }
    if (header.reserved != 0) {
      throw std::runtime_error{"FileWal: invalid page reserved field"};
    }
    const auto unknown =
        static_cast<std::uint16_t>(header.info & static_cast<std::uint16_t>(~page_info_known));
    if (unknown != 0) {
      throw std::runtime_error{"FileWal: unknown page flags"};
    }
    if (header.timeline != timeline_) {
      throw std::runtime_error{"FileWal: page timeline mismatch"};
    }
    if (header.pageaddr != pageaddr) {
      throw std::runtime_error{"FileWal: page address mismatch"};
    }
    const bool long_header = long_header_flag(header);
    const bool continuation = continuation_flag(header);
    if (long_header && continuation) {
      throw std::runtime_error{"FileWal: page is both long and continuation"};
    }
    if (long_header) {
      if (!allow_long || pageaddr != 0) {
        throw std::runtime_error{"FileWal: long page header in a data page"};
      }
      if (header.rem_len != 0) {
        throw std::runtime_error{"FileWal: long page header has remaining data"};
      }
    } else if (continuation) {
      if (header.rem_len == 0) {
        throw std::runtime_error{"FileWal: empty continuation page header"};
      }
    } else if (header.rem_len != 0) {
      throw std::runtime_error{"FileWal: unexpected remaining page length"};
    }
  }

  auto validate_long_page_header(const WalLongPageHeader& header) const -> void {
    validate_page_header(header.page, 0, true);
    if (!long_header_flag(header.page)) {
      throw std::runtime_error{"FileWal: missing long page header flag"};
    }
    if (header.sysid != wal_system_identifier) {
      throw std::runtime_error{"FileWal: system identifier mismatch"};
    }
    if (header.segment_size != segment_size_) {
      throw std::runtime_error{"FileWal: segment size mismatch"};
    }
    if (header.block_size != wal_page_size) {
      throw std::runtime_error{"FileWal: block size mismatch"};
    }
  }

  auto read_stream(LSN offset, std::span<std::byte> out) -> void {
    auto position = offset;
    auto remaining = out;
    while (!remaining.empty()) {
      const auto buffer_end = flushed_ + static_cast<LSN>(buffer_.size());
      if (position >= flushed_ && position < buffer_end) {
        const auto available = static_cast<std::size_t>(buffer_end - position);
        const auto count = std::min(remaining.size(), available);
        const auto source = std::span<const std::byte>{buffer_}.subspan(
            static_cast<std::size_t>(position - flushed_), count);
        std::ranges::copy(source, remaining.begin());
        remaining = remaining.subspan(count);
        position += static_cast<LSN>(count);
        continue;
      }

      const auto segment = position / static_cast<LSN>(segment_size_);
      const auto segment_offset = static_cast<std::size_t>(position % segment_size_);
      auto* file = open_segment(segment, false);
      if (file == nullptr) {
        throw std::runtime_error{"FileWal: segment file is missing"};
      }
      const auto count = std::min(remaining.size(), segment_size_ - segment_offset);
      file->read_at(remaining.first(count), segment_offset);
      remaining = remaining.subspan(count);
      position += static_cast<LSN>(count);
    }
  }

  [[nodiscard]] auto read_page_header(LSN pageaddr) -> WalPageHeader {
    std::array<std::byte, wal_page_header_size> bytes{};
    read_stream(pageaddr, bytes);
    return decode_page_header(bytes);
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  [[nodiscard]] auto read_prefix(LSN start, std::size_t count,
                                 std::optional<std::uint32_t> expected_total, LSN limit)
      -> ReadPrefix {
    std::vector<std::byte> bytes;
    bytes.reserve(count);
    auto position = start;
    std::size_t collected = 0;
    bool first = true;
    const auto segment = start / static_cast<LSN>(segment_size_);
    while (collected < count) {
      const auto page = page_start(position);
      if (page / static_cast<LSN>(segment_size_) != segment) {
        throw std::runtime_error{"FileWal: record crosses a segment boundary"};
      }
      if (page + static_cast<LSN>(wal_page_header_size) > limit) {
        throw TruncatedRecord{"FileWal: truncated page header"};
      }
      const auto header = read_page_header(page);
      if (header.magic == 0) {
        throw TruncatedRecord{"FileWal: missing page header"};
      }
      validate_page_header(header, page, page == 0);
      const bool continuation = continuation_flag(header);
      if (first) {
        if (position < page + static_cast<LSN>(wal_page_header_size)) {
          throw std::runtime_error{"FileWal: record starts inside a page header"};
        }
        if (position == page + static_cast<LSN>(wal_page_header_size) && continuation) {
          throw std::runtime_error{"FileWal: record starts in a continuation page"};
        }
      } else {
        if (position != page) {
          throw std::runtime_error{"FileWal: record does not continue at a page boundary"};
        }
        if (!continuation) {
          throw std::runtime_error{"FileWal: missing continuation page"};
        }
        if (expected_total.has_value() &&
            header.rem_len != expected_total.value() - static_cast<std::uint32_t>(collected)) {
          throw std::runtime_error{"FileWal: invalid remaining length"};
        }
      }
      if (position == page) {
        position += static_cast<LSN>(wal_page_header_size);
      }
      const auto page_end = page + static_cast<LSN>(wal_page_size);
      if (position >= page_end) {
        throw std::runtime_error{"FileWal: empty page payload"};
      }
      const auto available = static_cast<std::size_t>(page_end - position);
      const auto chunk = std::min(count - collected, available);
      if (position + static_cast<LSN>(chunk) > limit) {
        throw TruncatedRecord{"FileWal: truncated record payload"};
      }
      const auto old_size = bytes.size();
      bytes.resize(old_size + chunk);
      read_stream(position, std::span<std::byte>{bytes}.subspan(old_size, chunk));
      position += static_cast<LSN>(chunk);
      collected += chunk;
      first = false;
      if (collected < count && position != page_end) {
        if (position >= limit) {
          throw TruncatedRecord{"FileWal: truncated record payload"};
        }
        throw std::runtime_error{"FileWal: record does not fill the page"};
      }
    }
    return ReadPrefix{.bytes = std::move(bytes), .next = position};
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  [[nodiscard]] auto walk_record(LSN start, std::uint32_t total, LSN limit) -> LSN {
    auto position = start;
    std::size_t remaining = total;
    bool first = true;
    const auto segment = start / static_cast<LSN>(segment_size_);
    while (remaining > 0) {
      const auto page = page_start(position);
      if (page / static_cast<LSN>(segment_size_) != segment) {
        throw std::runtime_error{"FileWal: record crosses a segment boundary"};
      }
      if (page + static_cast<LSN>(wal_page_header_size) > limit) {
        throw TruncatedRecord{"FileWal: truncated page header"};
      }
      const auto header = read_page_header(page);
      if (header.magic == 0) {
        throw TruncatedRecord{"FileWal: missing page header"};
      }
      validate_page_header(header, page, page == 0);
      const bool continuation = continuation_flag(header);
      if (first) {
        if (position < page + static_cast<LSN>(wal_page_header_size)) {
          throw std::runtime_error{"FileWal: record starts inside a page header"};
        }
        if (position == page + static_cast<LSN>(wal_page_header_size) && continuation) {
          throw std::runtime_error{"FileWal: record starts in a continuation page"};
        }
      } else {
        if (position != page) {
          throw std::runtime_error{"FileWal: record does not continue at a page boundary"};
        }
        if (!continuation) {
          throw std::runtime_error{"FileWal: missing continuation page"};
        }
        if (header.rem_len != static_cast<std::uint32_t>(remaining)) {
          throw std::runtime_error{"FileWal: invalid remaining length"};
        }
      }
      if (position == page) {
        position += static_cast<LSN>(wal_page_header_size);
      }
      const auto page_end = page + static_cast<LSN>(wal_page_size);
      if (position >= page_end) {
        throw std::runtime_error{"FileWal: empty page payload"};
      }
      const auto available = static_cast<std::size_t>(page_end - position);
      const auto chunk = std::min(remaining, available);
      if (position + static_cast<LSN>(chunk) > limit) {
        throw TruncatedRecord{"FileWal: truncated record payload"};
      }
      position += static_cast<LSN>(chunk);
      remaining -= chunk;
      first = false;
      if (remaining > 0 && position != page_end) {
        if (position >= limit) {
          throw TruncatedRecord{"FileWal: truncated record payload"};
        }
        throw std::runtime_error{"FileWal: record does not fill the page"};
      }
    }
    return position;
  }

  [[nodiscard]] auto peek_total(LSN from, LSN limit) -> PeekResult {
    const auto page = page_start(from);
    const auto header = read_page_header(page);
    if (header.magic == 0) {
      return PeekResult{.kind = PeekResult::Kind::truncated};
    }
    validate_page_header(header, page, page == 0);
    if (from < page + static_cast<LSN>(wal_page_header_size)) {
      throw std::runtime_error{"FileWal: record starts inside a page header"};
    }
    if (from == page + static_cast<LSN>(wal_page_header_size) && continuation_flag(header)) {
      throw std::runtime_error{"FileWal: record starts in a continuation page"};
    }
    const auto page_end = page + static_cast<LSN>(wal_page_size);
    const auto available = static_cast<std::size_t>(page_end - from);

    if (from + static_cast<LSN>(bytes_per_u32) > limit) {
      const auto tail_size = static_cast<std::size_t>(std::min<LSN>(available, limit - from));
      std::array<std::byte, bytes_per_u32 - 1> tail{};
      read_stream(from, std::span<std::byte>{tail}.first(tail_size));
      const bool nonzero =
          std::ranges::any_of(tail, [](std::byte item) { return item != std::byte{0}; });
      return PeekResult{.kind = nonzero ? PeekResult::Kind::truncated : PeekResult::Kind::padding};
    }

    if (available >= bytes_per_u32) {
      std::array<std::byte, bytes_per_u32> bytes{};
      read_stream(from, bytes);
      const auto total = read_u32(bytes, 0);
      if (total == 0) {
        return PeekResult{.kind = PeekResult::Kind::padding};
      }
      validate_record_total(total);
      return PeekResult{.kind = PeekResult::Kind::record, .total = total};
    }

    std::array<std::byte, bytes_per_u32 - 1> tail{};
    read_stream(from, std::span<std::byte>{tail}.first(available));
    const bool nonzero =
        std::ranges::any_of(tail, [](std::byte item) { return item != std::byte{0}; });
    const auto next = page_end;
    bool next_continuation = false;
    if (next < limit) {
      const auto next_header = read_page_header(next);
      if (next_header.magic != 0) {
        validate_page_header(next_header, next, next == 0);
        next_continuation = continuation_flag(next_header);
      }
    }
    if (!nonzero && !next_continuation) {
      return PeekResult{.kind = PeekResult::Kind::padding};
    }
    try {
      const auto prefix = read_prefix(from, bytes_per_u32, std::nullopt, limit);
      const auto total = read_u32(prefix.bytes, 0);
      if (total == 0) {
        return PeekResult{.kind = PeekResult::Kind::padding};
      }
      validate_record_total(total);
      return PeekResult{.kind = PeekResult::Kind::record, .total = total};
    } catch (const TruncatedRecord&) {
      return PeekResult{.kind = PeekResult::Kind::truncated};
    }
  }

  [[nodiscard]] auto locate_record_start(LSN from) -> LSN {
    auto position = from;
    while (true) {
      if (position >= inserted_) {
        throw std::out_of_range{"FileWal: LSN does not name a record"};
      }
      const auto page = page_start(position);
      const auto header = read_page_header(page);
      if (header.magic == 0) {
        if (position != page) {
          throw std::runtime_error{"FileWal: missing page header"};
        }
        position = page + static_cast<LSN>(wal_page_size);
        continue;
      }
      validate_page_header(header, page, page == 0);
      const bool continuation = continuation_flag(header);
      if (position == page) {
        if (continuation) {
          throw std::runtime_error{"FileWal: record starts in a continuation page"};
        }
        position += static_cast<LSN>(wal_page_header_size);
      } else {
        if (position < page + static_cast<LSN>(wal_page_header_size)) {
          throw std::runtime_error{"FileWal: LSN points inside a page header"};
        }
        if (position == page + static_cast<LSN>(wal_page_header_size) && continuation) {
          throw std::runtime_error{"FileWal: record starts in a continuation page"};
        }
      }
      const auto peek = peek_total(position, inserted_);
      if (peek.kind == PeekResult::Kind::padding) {
        position = page + static_cast<LSN>(wal_page_size);
        continue;
      }
      if (peek.kind == PeekResult::Kind::truncated) {
        throw std::runtime_error{"FileWal: truncated record"};
      }
      return position;
    }
  }

  [[nodiscard]] auto frame_record(LSN start, std::span<const std::byte> record) const
      -> FramedRecord {
    const bool starts_page = start == page_start(start);
    std::vector<std::byte> bytes;
    auto position = start;
    std::size_t consumed = 0;
    bool first = true;
    while (consumed < record.size()) {
      const auto page = page_start(position);
      if (position == page) {
        const WalPageHeader header{
            .magic = wal_page_magic,
            .info = static_cast<std::uint16_t>(first ? 0 : wal_page_info_continuation),
            .timeline = timeline_,
            .pageaddr = page,
            .rem_len = static_cast<std::uint32_t>(first ? 0 : record.size() - consumed),
            .reserved = 0};
        const auto header_bytes = encode_page_header(header);
        bytes.insert(bytes.end(), header_bytes.begin(), header_bytes.end());
        position += static_cast<LSN>(wal_page_header_size);
      }
      const auto page_end = page + static_cast<LSN>(wal_page_size);
      if (position >= page_end) {
        throw std::runtime_error{"FileWal: frame does not fit the page"};
      }
      const auto available = static_cast<std::size_t>(page_end - position);
      const auto chunk = std::min(record.size() - consumed, available);
      const auto begin = record.begin() + static_cast<std::ptrdiff_t>(consumed);
      bytes.insert(bytes.end(), begin, begin + static_cast<std::ptrdiff_t>(chunk));
      consumed += chunk;
      position += static_cast<LSN>(chunk);
      first = false;
    }
    const auto record_start = starts_page ? start + static_cast<LSN>(wal_page_header_size) : start;
    return FramedRecord{.start = record_start, .end = position, .bytes = std::move(bytes)};
  }

  [[nodiscard]] auto segment_end_for(LSN lsn) const -> LSN {
    const auto offset = lsn % static_cast<LSN>(segment_size_);
    return lsn + (static_cast<LSN>(segment_size_) - offset);
  }

  auto ensure_long_header(detail::OsFile& file) -> void {
    std::array<std::byte, wal_long_page_header_size> bytes{};
    file.read_at(bytes, 0);
    const auto page = decode_page_header(bytes);
    if (page.magic == 0) {
      const WalLongPageHeader header{.page = {.magic = wal_page_magic,
                                              .info = wal_page_info_long_header,
                                              .timeline = timeline_,
                                              .pageaddr = 0,
                                              .rem_len = 0,
                                              .reserved = 0},
                                     .sysid = wal_system_identifier,
                                     .segment_size = static_cast<std::uint32_t>(segment_size_),
                                     .block_size = static_cast<std::uint32_t>(wal_page_size)};
      const auto header_bytes = encode_long_page_header(header);
      file.write_at(header_bytes, 0);
      return;
    }
    const auto header = decode_long_page_header(bytes);
    validate_long_page_header(header);
  }

  [[nodiscard]] auto segment_filename(std::uint64_t index) const -> std::string {
    return std::format("{:08x}{:08x}{:08x}", timeline_,
                       static_cast<std::uint32_t>(index >> bits_per_u32),
                       static_cast<std::uint32_t>(index));
  }

  auto open_segment(std::uint64_t index, bool create) -> detail::OsFile* {
    const auto existing = files_.find(index);
    if (existing != files_.end()) {
      return existing->second.get();
    }
    const auto path = directory_ / segment_filename(index);
    const bool existed = std::filesystem::exists(path);
    if (!create && !existed) {
      return nullptr;
    }
    auto file = detail::OsFile::open(path, create);
    if (file == nullptr) {
      return nullptr;
    }
    if (create) {
      const auto size = existed ? std::filesystem::file_size(path) : std::uintmax_t{0};
      if (size < segment_size_) {
        file->truncate(segment_size_);
      }
      if (index == 0) {
        ensure_long_header(*file);
      }
      if (!existed) {
        detail::fsync_directory(directory_);
      }
    }
    auto [position, inserted] = files_.try_emplace(index, std::move(file));
    static_cast<void>(inserted);
    return position->second.get();
  }

  auto scan_locked() -> void {
    std::vector<std::uint64_t> segments;
    for (const auto& entry : std::filesystem::directory_iterator{directory_}) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto index = parse_segment_name(entry.path().filename().string(), timeline_);
      if (index.has_value()) {
        segments.push_back(index.value());
      }
    }
    std::ranges::sort(segments);
    const auto last = std::unique(segments.begin(), segments.end());
    segments.erase(last, segments.end());

    LSN end = wal_page_size;
    LSN last_start = invalid_lsn;
    if (segments.empty()) {
      inserted_ = end;
      flushed_ = end;
      last_prev_ = last_start;
      return;
    }

    const auto first = segments.front();
    end = first * static_cast<LSN>(segment_size_) + (first == 0 ? wal_page_size : LSN{0});

    if (first == 0) {
      auto* file = open_segment(0, false);
      if (file == nullptr) {
        throw std::runtime_error{"FileWal: missing first segment"};
      }
      const auto path = directory_ / segment_filename(0);
      const auto file_size = std::filesystem::file_size(path);
      std::array<std::byte, wal_long_page_header_size> bytes{};
      file->read_at(bytes, 0);
      const auto page = decode_page_header(bytes);
      if (page.magic == 0) {
        if (file_size >= static_cast<std::uintmax_t>(wal_page_size) + bytes_per_u32) {
          std::array<std::byte, bytes_per_u32> first_bytes{};
          file->read_at(first_bytes, wal_page_size);
          const bool nonzero =
              std::ranges::any_of(first_bytes, [](std::byte item) { return item != std::byte{0}; });
          if (nonzero) {
            throw std::runtime_error{"FileWal: unsupported flat WAL format"};
          }
        }
      } else {
        const auto long_header = decode_long_page_header(bytes);
        validate_long_page_header(long_header);
      }
    }

    bool stop = false;
    for (const auto index : segments) {
      if (stop) {
        break;
      }
      auto* file = open_segment(index, false);
      if (file == nullptr) {
        continue;
      }
      const auto segment_start = index * static_cast<LSN>(segment_size_);
      const auto path = directory_ / segment_filename(index);
      const auto file_size = std::filesystem::file_size(path);
      const auto stored = static_cast<LSN>(
          std::min<std::uintmax_t>(segment_size_, static_cast<std::uintmax_t>(file_size)));
      const auto segment_limit = segment_start + stored;
      auto position = segment_start + (index == 0 ? wal_page_size : LSN{0});
      while (position < segment_limit) {
        const auto page = page_start(position);
        if (page + static_cast<LSN>(wal_page_header_size) > segment_limit) {
          const auto available = static_cast<std::size_t>(segment_limit - position);
          std::array<std::byte, wal_page_header_size> bytes{};
          if (available > 0) {
            file->read_at(std::span<std::byte>{bytes}.first(available),
                          static_cast<std::size_t>(position - segment_start));
          }
          const bool nonzero =
              std::ranges::any_of(bytes, [](std::byte item) { return item != std::byte{0}; });
          if (nonzero) {
            end = position;
            stop = true;
          }
          break;
        }

        const auto header = read_page_header(page);
        if (header.magic == 0) {
          if (position != page) {
            throw std::runtime_error{"FileWal: missing page header"};
          }
          break;
        }
        validate_page_header(header, page, page == 0);
        const bool continuation = continuation_flag(header);
        if (position == page) {
          if (continuation) {
            throw std::runtime_error{"FileWal: record starts in a continuation page"};
          }
          position += static_cast<LSN>(wal_page_header_size);
        } else {
          if (position < page + static_cast<LSN>(wal_page_header_size)) {
            throw std::runtime_error{"FileWal: record starts inside a page header"};
          }
          if (position == page + static_cast<LSN>(wal_page_header_size) && continuation) {
            throw std::runtime_error{"FileWal: record starts in a continuation page"};
          }
        }

        PeekResult peek;
        try {
          peek = peek_total(position, segment_limit);
        } catch (const TruncatedRecord&) {
          end = position;
          stop = true;
          break;
        }
        if (peek.kind == PeekResult::Kind::padding) {
          position = page + static_cast<LSN>(wal_page_size);
          continue;
        }
        if (peek.kind == PeekResult::Kind::truncated) {
          end = position;
          stop = true;
          break;
        }

        try {
          const auto next = walk_record(position, peek.total, segment_limit);
          last_start = position;
          position = next;
          end = position;
        } catch (const TruncatedRecord&) {
          end = position;
          stop = true;
          break;
        }
      }
    }

    inserted_ = end;
    flushed_ = end;
    last_prev_ = last_start;
  }

  auto pad_to(LSN target) -> void {
    if (target <= inserted_) {
      return;
    }
    const auto count = static_cast<std::size_t>(target - inserted_);
    buffer_.insert(buffer_.end(), count, std::byte{0});
    inserted_ = target;
  }

  [[nodiscard]] auto encode_record_locked(const WalRecord& record) const -> std::vector<std::byte> {
    std::array<bool, max_block_refs> seen{};
    std::size_t total = record_header_size;
    for (const auto& block : record.blocks) {
      if (block.id >= max_block_refs) {
        throw std::invalid_argument{"FileWal: block id out of range"};
      }
      if (seen.at(block.id)) {
        throw std::invalid_argument{"FileWal: duplicate block id"};
      }
      seen.at(block.id) = true;
      if (static_cast<std::uint8_t>(block.fork) != static_cast<std::uint8_t>(Fork::main)) {
        throw std::invalid_argument{"FileWal: unsupported fork"};
      }
      const auto image_size = block.full_image.has_value() ? block.full_image->size() : 0;
      if (block.data.size() > max_payload_size || image_size > max_payload_size) {
        throw std::length_error{"FileWal: block payload is too large"};
      }
      total += block_header_size + block.data.size() + image_size;
    }
    if (!record.main_data.empty()) {
      total += payload_marker_size + record.main_data.size();
    }
    if (total > segment_size_) {
      throw std::length_error{"FileWal: record does not fit in a segment"};
    }

    std::vector<std::byte> bytes;
    bytes.reserve(total);
    append_u32(bytes, static_cast<std::uint32_t>(total));
    append_u32(bytes, record.xid);
    append_u64(bytes, last_prev_);
    bytes.push_back(std::byte{record.info});
    bytes.push_back(std::byte{record.rmid});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    append_u32(bytes, 0);

    for (const auto& block : record.blocks) {
      const auto data_length = static_cast<std::uint32_t>(block.data.size());
      const auto image_length = block.full_image.has_value()
                                    ? static_cast<std::uint32_t>(block.full_image.value().size())
                                    : 0;
      std::uint16_t flags = 0;
      if (data_length != 0) {
        flags = static_cast<std::uint16_t>(flags | block_flag_data);
      }
      if (image_length != 0) {
        flags = static_cast<std::uint16_t>(flags | block_flag_image);
      }
      bytes.push_back(std::byte{block.id});
      bytes.push_back(std::byte{static_cast<std::uint8_t>(block.fork)});
      append_u16(bytes, flags);
      append_u32(bytes, block.block);
      append_u32(bytes, data_length);
      append_u32(bytes, image_length);
      if (block.full_image.has_value()) {
        const auto& image = block.full_image.value();
        bytes.insert(bytes.end(), image.begin(), image.end());
      }
      bytes.insert(bytes.end(), block.data.begin(), block.data.end());
    }

    if (!record.main_data.empty()) {
      bytes.push_back(std::byte{main_data_marker});
      bytes.insert(bytes.end(), record.main_data.begin(), record.main_data.end());
    }

    store_u32(bytes, record_crc_offset, compute_crc(bytes));
    return bytes;
  }

  auto insert_locked(const WalRecord& record) -> LSN {
    const auto bytes = encode_record_locked(record);
    auto start = inserted_;
    auto segment_end = segment_end_for(start);
    auto framed = frame_record(start, bytes);
    if (framed.end > segment_end) {
      const auto next_start = segment_end;
      const auto next_end = next_start + static_cast<LSN>(segment_size_);
      auto candidate = frame_record(next_start, bytes);
      if (candidate.end > next_end) {
        throw std::length_error{"FileWal: record does not fit in a segment"};
      }
      pad_to(next_start);
      framed = std::move(candidate);
    }

    buffer_.insert(buffer_.end(), framed.bytes.begin(), framed.bytes.end());
    pending_.push_back(PendingRecord{.start = framed.start, .end = framed.end});
    inserted_ = framed.end;
    last_prev_ = framed.start;
    return framed.start;
  }

  auto flush_locked(LSN upto) -> void {
    LSN target = invalid_lsn;
    for (const auto& pending : pending_) {
      if (pending.start <= upto) {
        target = std::max(target, pending.end);
      }
    }
    if (target <= flushed_) {
      return;
    }

    auto position = flushed_;
    while (position < target) {
      const auto segment = position / static_cast<LSN>(segment_size_);
      const auto offset = position % static_cast<LSN>(segment_size_);
      const auto count = std::min<std::uint64_t>(
          target - position, static_cast<std::uint64_t>(segment_size_) - offset);
      auto* file = open_segment(segment, true);
      file->write_at(
          std::span<const std::byte>{buffer_}.subspan(static_cast<std::size_t>(position - flushed_),
                                                      static_cast<std::size_t>(count)),
          offset);
      file->fsync();
      position += count;
    }

    const auto consumed = static_cast<std::size_t>(target - flushed_);
    if (consumed > buffer_.size()) {
      throw std::runtime_error{"FileWal: buffer underflow"};
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
    flushed_ = target;
    std::erase_if(pending_,
                  [this](const PendingRecord& pending) { return pending.end <= flushed_; });
  }

  [[nodiscard]] auto read_locked(LSN from) -> WalReadResult {
    if (from < wal_page_size || from >= inserted_) {
      throw std::out_of_range{"FileWal: LSN does not name a record"};
    }

    const auto start = locate_record_start(from);
    const auto peek = peek_total(start, inserted_);
    if (peek.kind != PeekResult::Kind::record) {
      throw std::runtime_error{"FileWal: LSN does not name a record"};
    }
    auto prefix = read_prefix(start, peek.total, peek.total, inserted_);
    read_storage_ = std::move(prefix.bytes);
    const auto bytes = std::span<const std::byte>{read_storage_};
    if (read_u32(bytes, record_crc_offset) != compute_crc(bytes)) {
      throw std::runtime_error{"FileWal: record checksum mismatch"};
    }
    auto record = decode_record(bytes);
    return WalReadResult{.record = std::move(record), .next_lsn = prefix.next};
  }

  [[nodiscard]] auto decode_record(std::span<const std::byte> bytes) const -> WalRecord {
    if (bytes.size() < record_header_size) {
      throw std::runtime_error{"FileWal: truncated record header"};
    }
    if (read_u32(bytes, record_total_len_offset) != bytes.size()) {
      throw std::runtime_error{"FileWal: record length mismatch"};
    }
    const auto padding = bytes.subspan(record_padding_offset, record_padding_size);
    if (!std::ranges::all_of(padding, [](std::byte value) { return value == std::byte{0}; })) {
      throw std::runtime_error{"FileWal: nonzero record padding"};
    }

    WalRecord record{
        .rmid = std::to_integer<std::uint8_t>(byte_at(bytes, record_rmid_offset)),
        .info = std::to_integer<std::uint8_t>(byte_at(bytes, record_info_offset)),
        .xid = read_u32(bytes, record_xid_offset),
    };
    std::array<bool, max_block_refs> seen{};
    std::size_t offset = record_header_size;
    while (offset < bytes.size()) {
      if (byte_at(bytes, offset) == std::byte{main_data_marker}) {
        ++offset;
        record.main_data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
        break;
      }
      if (bytes.size() - offset < block_header_size) {
        throw std::runtime_error{"FileWal: truncated block header"};
      }
      const auto block_id = std::to_integer<std::uint8_t>(byte_at(bytes, offset));
      if (block_id >= max_block_refs || seen.at(block_id)) {
        throw std::runtime_error{"FileWal: invalid block id"};
      }
      seen.at(block_id) = true;
      const auto fork = std::to_integer<std::uint8_t>(byte_at(bytes, offset + block_fork_offset));
      if (fork != static_cast<std::uint8_t>(Fork::main)) {
        throw std::runtime_error{"FileWal: unsupported fork"};
      }
      const auto flags = read_u16(bytes, offset + block_flags_offset);
      if ((flags & static_cast<std::uint16_t>(~block_flags_known)) != 0U) {
        throw std::runtime_error{"FileWal: unknown block flags"};
      }
      const auto block_number = read_u32(bytes, offset + block_number_offset);
      const auto data_length = read_u32(bytes, offset + block_data_length_offset);
      const auto image_length = read_u32(bytes, offset + block_image_length_offset);
      if (((flags & block_flag_data) != 0) != (data_length != 0) ||
          ((flags & block_flag_image) != 0) != (image_length != 0)) {
        throw std::runtime_error{"FileWal: inconsistent block flags"};
      }
      offset += block_header_size;

      if (bytes.size() - offset < image_length) {
        throw std::runtime_error{"FileWal: truncated block image"};
      }
      std::optional<std::vector<std::byte>> image;
      if (image_length != 0) {
        const auto image_bytes = bytes.subspan(offset, image_length);
        image = std::vector<std::byte>{image_bytes.begin(), image_bytes.end()};
        offset += image_length;
      }

      if (bytes.size() - offset < data_length) {
        throw std::runtime_error{"FileWal: truncated block data"};
      }
      const auto data = bytes.subspan(offset, data_length);
      offset += data_length;
      record.blocks.push_back(WalBlockRef{.id = block_id,
                                          .fork = Fork::main,
                                          .block = block_number,
                                          .data = data,
                                          .full_image = std::move(image)});
    }
    return record;
  }

  std::filesystem::path directory_;
  std::uint32_t timeline_{0};
  std::size_t segment_size_{0};
  mutable std::mutex mutex_;
  std::map<std::uint64_t, std::unique_ptr<detail::OsFile>> files_;
  std::vector<std::byte> buffer_;
  std::vector<std::byte> read_storage_;
  std::vector<PendingRecord> pending_;
  LSN flushed_{wal_page_size};
  LSN inserted_{wal_page_size};
  LSN last_prev_{invalid_lsn};
};

FileWal::FileWal(std::filesystem::path directory, std::uint32_t timeline, std::size_t segment_size)
    : impl_{std::make_unique<Impl>(std::move(directory), timeline, segment_size)} {}

FileWal::~FileWal() {
  try {
    impl_->close();
  } catch (const std::exception& error) {
    static_cast<void>(error);
  }
}

auto FileWal::insert(const WalRecord& record) -> LSN {
  return impl_->insert(record);
}

auto FileWal::flush(LSN upto) -> void {
  impl_->flush(upto);
}

auto FileWal::needs_flush(LSN upto) const -> bool {
  return impl_->needs_flush(upto);
}

auto FileWal::read(LSN from) -> WalReadResult {
  return impl_->read(from);
}

auto FileWal::switch_segment() -> void {
  impl_->switch_segment();
}

auto FileWal::recycle(LSN before) -> void {
  impl_->recycle(before);
}

auto FileWal::close() -> void {
  impl_->close();
}

}  // namespace kravidb::storage
