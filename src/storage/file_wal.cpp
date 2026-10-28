module;

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

module storage.file_wal;

import std;
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

constexpr std::uint32_t crc_polynomial = 0x82F63B78U;
constexpr std::uint32_t crc_initial = 0xFFFFFFFFU;
constexpr std::size_t crc_table_size = 256;

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

consteval auto make_crc_table() -> std::array<std::uint32_t, crc_table_size> {
  std::array<std::uint32_t, crc_table_size> table{};
  for (std::size_t index = 0; index < table.size(); ++index) {
    auto crc = static_cast<std::uint32_t>(index);
    for (std::uint32_t bit = 0; bit < bits_per_byte; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ crc_polynomial : crc >> 1U;
    }
    table.at(index) = crc;
  }
  return table;
}

inline constexpr auto crc_table = make_crc_table();

auto crc32c_update(std::uint32_t crc, std::span<const std::byte> bytes) -> std::uint32_t {
  for (const std::byte item : bytes) {
    const auto index = (crc ^ std::to_integer<std::uint32_t>(item)) & byte_mask;
    crc = crc_table.at(index) ^ (crc >> bits_per_byte);
  }
  return crc;
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

  [[nodiscard]] auto read(LSN from) -> WalRecord {
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
    if (!segments.empty()) {
      const auto first = segments.front();
      end = first * static_cast<LSN>(segment_size_) + (first == 0 ? wal_page_size : LSN{0});
    }

    for (const auto index : segments) {
      auto* file = open_segment(index, false);
      if (file == nullptr) {
        continue;
      }
      const auto segment_start = index * static_cast<LSN>(segment_size_);
      const auto segment_limit = segment_start + static_cast<LSN>(segment_size_);
      auto position = segment_start + (index == 0 ? wal_page_size : LSN{0});
      while (position + record_header_size <= segment_limit) {
        std::array<std::byte, record_header_size> header{};
        file->read_at(header, position - segment_start);
        const auto total = read_u32(header, record_total_len_offset);
        if (total == 0) {
          break;
        }
        if (total < record_header_size || position + total > segment_limit) {
          throw std::runtime_error{"FileWal: invalid record length"};
        }
        last_start = position;
        position += total;
        end = position;
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
    const auto offset = inserted_ % static_cast<LSN>(segment_size_);
    const auto segment_end = inserted_ + (static_cast<LSN>(segment_size_) - offset);
    if (inserted_ + static_cast<LSN>(bytes.size()) > segment_end) {
      pad_to(segment_end);
    }
    const auto start = inserted_;
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    pending_.push_back(
        PendingRecord{.start = start, .end = start + static_cast<LSN>(bytes.size())});
    inserted_ = start + static_cast<LSN>(bytes.size());
    last_prev_ = start;
    return start;
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

  [[nodiscard]] auto read_locked(LSN from) -> WalRecord {
    if (from < wal_page_size || from >= inserted_) {
      throw std::out_of_range{"FileWal: LSN does not name a record"};
    }

    if (from >= flushed_) {
      const auto offset = static_cast<std::size_t>(from - flushed_);
      const auto buffer = std::span<const std::byte>{buffer_};
      if (offset + record_header_size > buffer.size()) {
        throw std::runtime_error{"FileWal: truncated record"};
      }
      const auto total = read_u32(buffer, offset + record_total_len_offset);
      if (total < record_header_size || offset + total > buffer.size()) {
        throw std::runtime_error{"FileWal: invalid record length"};
      }
      read_storage_.assign(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                           buffer.begin() + static_cast<std::ptrdiff_t>(offset + total));
    } else {
      const auto segment = from / static_cast<LSN>(segment_size_);
      const auto offset = from % static_cast<LSN>(segment_size_);
      auto* file = open_segment(segment, false);
      if (file == nullptr) {
        throw std::runtime_error{"FileWal: segment file is missing"};
      }
      std::array<std::byte, record_header_size> header{};
      file->read_at(header, offset);
      const auto total = read_u32(header, record_total_len_offset);
      if (total < record_header_size || offset + total > static_cast<LSN>(segment_size_)) {
        throw std::runtime_error{"FileWal: invalid record length"};
      }
      read_storage_.resize(total);
      file->read_at(std::span<std::byte>{read_storage_}, offset);
    }

    const auto bytes = std::span<const std::byte>{read_storage_};
    if (read_u32(bytes, record_crc_offset) != compute_crc(bytes)) {
      throw std::runtime_error{"FileWal: record checksum mismatch"};
    }
    return decode_record(bytes);
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

auto FileWal::read(LSN from) -> WalRecord {
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
