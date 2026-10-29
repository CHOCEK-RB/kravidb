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

module storage.file_relation_file;

import std;
import storage.relation_file;

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

  auto read_at(std::span<std::byte, physical_page_size> out, std::uint64_t offset) -> void {
#if defined(_WIN32)
    std::size_t done = 0;
    while (done < out.size()) {
      OVERLAPPED overlapped{};
      const auto position = static_cast<std::uint64_t>(offset) + done;
      overlapped.Offset = static_cast<DWORD>(position);
      overlapped.OffsetHigh = static_cast<DWORD>(position >> 32U);
      DWORD read = 0;
      if (::ReadFile(handle_, out.data() + done, static_cast<DWORD>(out.size() - done), &read,
                     &overlapped) == 0) {
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

  auto write_at(std::span<const std::byte, physical_page_size> input, std::uint64_t offset)
      -> void {
#if defined(_WIN32)
    std::size_t done = 0;
    while (done < input.size()) {
      OVERLAPPED overlapped{};
      const auto position = static_cast<std::uint64_t>(offset) + done;
      overlapped.Offset = static_cast<DWORD>(position);
      overlapped.OffsetHigh = static_cast<DWORD>(position >> 32U);
      DWORD written = 0;
      if (::WriteFile(handle_, input.data() + done, static_cast<DWORD>(input.size() - done),
                      &written, &overlapped) == 0) {
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
///
/// Windows has no portable directory fsync; file contents are still flushed by
/// `FlushFileBuffers`.
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

class FileRelationFile::Impl {
  public:
  Impl(std::filesystem::path base_directory, RelFileNode node, std::size_t segment_size)
      : base_directory_{std::move(base_directory)},
        base_name_{std::to_string(node.relation)},
        blocks_per_segment_{static_cast<std::uint64_t>(segment_size) / physical_page_size} {
    if (base_directory_.empty()) {
      throw std::invalid_argument{"FileRelationFile: empty base directory"};
    }
    if (segment_size == 0 || segment_size % physical_page_size != 0) {
      throw std::invalid_argument{
          "FileRelationFile: segment size must be a multiple of the page size"};
    }
    if (blocks_per_segment_ == 0 ||
        blocks_per_segment_ >
            static_cast<std::uint64_t>(std::numeric_limits<BlockNumber>::max()) + 1U) {
      throw std::invalid_argument{"FileRelationFile: unsupported segment size"};
    }
    sizes_.try_emplace(Fork::main, scan_size(Fork::main));
  }

  auto read(Fork fork, BlockNumber block, std::span<std::byte, physical_page_size> out) -> void {
    std::lock_guard lock{mutex_};
    if (block >= size_of(fork)) {
      std::ranges::fill(out, std::byte{0});
      return;
    }
    const auto position = locate(block);
    auto* file = open_segment(fork, position.segment, false);
    if (file == nullptr) {
      std::ranges::fill(out, std::byte{0});
      return;
    }
    file->read_at(out, position.offset);
  }

  auto write(Fork fork, BlockNumber block, std::span<const std::byte, physical_page_size> input)
      -> void {
    if (block == std::numeric_limits<BlockNumber>::max()) {
      throw std::length_error{"FileRelationFile: block number overflow"};
    }
    std::lock_guard lock{mutex_};
    const auto position = locate(block);
    auto* file = open_segment(fork, position.segment, true);
    file->write_at(input, position.offset);
    auto& size = sizes_.try_emplace(fork, 0).first->second;
    size = std::max(size, static_cast<BlockNumber>(block + 1U));
  }

  auto extend(Fork fork, BlockNumber blocks) -> void {
    std::lock_guard lock{mutex_};
    const auto current = static_cast<std::uint64_t>(size_of(fork));
    const auto target = current + static_cast<std::uint64_t>(blocks);
    if (target > std::numeric_limits<BlockNumber>::max()) {
      throw std::length_error{"FileRelationFile: relation exceeds the maximum block number"};
    }
    resize_locked(fork, static_cast<BlockNumber>(target));
  }

  auto fsync(Fork fork) -> void {
    std::lock_guard lock{mutex_};
    static_cast<void>(fork);
    for (auto& [segment, file] : files_) {
      static_cast<void>(segment);
      file->fsync();
    }
  }

  auto truncate(Fork fork, BlockNumber blocks) -> void {
    std::lock_guard lock{mutex_};
    resize_locked(fork, blocks);
  }

  [[nodiscard]] auto block_count(Fork fork) const -> BlockNumber {
    std::lock_guard lock{mutex_};
    return size_of(fork);
  }

  private:
  struct Position {
    std::uint64_t segment{0};
    std::uint64_t offset{0};
  };

  [[nodiscard]] auto size_of(Fork fork) const -> BlockNumber {
    const auto size = sizes_.find(fork);
    return size == sizes_.end() ? 0 : size->second;
  }

  [[nodiscard]] auto locate(BlockNumber block) const -> Position {
    const auto block_number = static_cast<std::uint64_t>(block);
    return Position{.segment = block_number / blocks_per_segment_,
                    .offset = (block_number % blocks_per_segment_) * physical_page_size};
  }

  [[nodiscard]] auto fork_suffix(Fork fork) const -> std::string_view {
    switch (fork) {
      case Fork::main:
        return {};
    }
    return {};
  }

  [[nodiscard]] auto path_for(Fork fork, std::uint64_t segment) const -> std::filesystem::path {
    std::string name = base_name_;
    name += fork_suffix(fork);
    if (segment != 0) {
      name += ".";
      name += std::to_string(segment);
    }
    return base_directory_ / name;
  }

  [[nodiscard]] auto list_segments(Fork fork) const
      -> std::map<std::uint64_t, std::filesystem::path> {
    std::map<std::uint64_t, std::filesystem::path> result;
    if (!std::filesystem::exists(base_directory_)) {
      return result;
    }
    const std::string prefix = base_name_ + std::string{fork_suffix(fork)};
    for (const auto& entry : std::filesystem::directory_iterator{base_directory_}) {
      const auto filename = entry.path().filename().string();
      std::uint64_t segment = 0;
      if (filename == prefix) {
        segment = 0;
      } else {
        const std::string segment_prefix = prefix + ".";
        if (!filename.starts_with(segment_prefix)) {
          continue;
        }
        const auto suffix = std::string_view{filename}.substr(segment_prefix.size());
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        const auto [end, error] = std::from_chars(suffix.data(), suffix.end(), segment);
        // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
        if (error != std::errc{} || end != suffix.end()) {
          continue;
        }
      }
      result.try_emplace(segment, entry.path());
    }
    return result;
  }

  [[nodiscard]] auto scan_size(Fork fork) const -> BlockNumber {
    const auto segments = list_segments(fork);
    std::uint64_t maximum = 0;
    for (const auto& [segment, path] : segments) {
      const auto bytes = std::filesystem::file_size(path);
      const auto blocks = (bytes + physical_page_size - 1U) / physical_page_size;
      const auto segment_start = segment * blocks_per_segment_;
      const auto end = segment_start + blocks;
      maximum = std::max(maximum, end);
    }
    if (maximum > std::numeric_limits<BlockNumber>::max()) {
      throw std::length_error{
          "FileRelationFile: existing segments exceed the maximum block number"};
    }
    return static_cast<BlockNumber>(maximum);
  }

  [[nodiscard]] auto open_segment(Fork fork, std::uint64_t segment, bool create)
      -> detail::OsFile* {
    const auto existing = files_.find(segment);
    if (existing != files_.end()) {
      return existing->second.get();
    }
    const auto path = path_for(fork, segment);
    const bool existed = std::filesystem::exists(path);
    auto file = detail::OsFile::open(path, create);
    if (file == nullptr) {
      return nullptr;
    }
    if (!existed) {
      detail::fsync_directory(base_directory_);
    }
    auto [inserted, success] = files_.try_emplace(segment, std::move(file));
    static_cast<void>(success);
    return inserted->second.get();
  }

  auto resize_locked(Fork fork, BlockNumber blocks) -> void {
    if (blocks == 0) {
      const auto segments = list_segments(fork);
      for (const auto& [segment, path] : segments) {
        static_cast<void>(segment);
        files_.erase(segment);
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
          throw std::filesystem::filesystem_error{"remove", path, error};
        }
      }
      sizes_.insert_or_assign(fork, 0);
      if (!segments.empty()) {
        detail::fsync_directory(base_directory_);
      }
      return;
    }

    const auto last_segment = (static_cast<std::uint64_t>(blocks) - 1U) / blocks_per_segment_;
    for (std::uint64_t segment = 0; segment <= last_segment; ++segment) {
      const auto start = segment * blocks_per_segment_;
      const auto remaining = static_cast<std::uint64_t>(blocks) - start;
      const auto count = std::min(blocks_per_segment_, remaining);
      auto* file = open_segment(fork, segment, true);
      file->truncate(count * physical_page_size);
    }

    bool removed = false;
    for (auto iterator = files_.begin(); iterator != files_.end();) {
      if (iterator->first > last_segment) {
        iterator = files_.erase(iterator);
        removed = true;
      } else {
        ++iterator;
      }
    }
    for (const auto& [segment, path] : list_segments(fork)) {
      if (segment > last_segment) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
          throw std::filesystem::filesystem_error{"remove", path, error};
        }
        removed = true;
      }
    }
    sizes_.insert_or_assign(fork, blocks);
    if (removed) {
      detail::fsync_directory(base_directory_);
    }
  }

  std::filesystem::path base_directory_;
  std::string base_name_;
  std::uint64_t blocks_per_segment_{0};
  mutable std::mutex mutex_;
  std::map<std::uint64_t, std::unique_ptr<detail::OsFile>> files_;
  std::map<Fork, BlockNumber> sizes_;
};

FileRelationFile::FileRelationFile(std::filesystem::path base_directory, RelFileNode node,
                                   std::size_t segment_size)
    : impl_{std::make_unique<Impl>(std::move(base_directory), node, segment_size)} {}

FileRelationFile::~FileRelationFile() = default;

auto FileRelationFile::read(Fork fork, BlockNumber block,
                            std::span<std::byte, physical_page_size> out) -> void {
  impl_->read(fork, block, out);
}

auto FileRelationFile::write(Fork fork, BlockNumber block,
                             std::span<const std::byte, physical_page_size> input) -> void {
  impl_->write(fork, block, input);
}

auto FileRelationFile::extend(Fork fork, BlockNumber blocks) -> void {
  impl_->extend(fork, blocks);
}

auto FileRelationFile::fsync(Fork fork) -> void {
  impl_->fsync(fork);
}

auto FileRelationFile::truncate(Fork fork, BlockNumber blocks) -> void {
  impl_->truncate(fork, blocks);
}

auto FileRelationFile::block_count(Fork fork) const -> BlockNumber {
  return impl_->block_count(fork);
}

}  // namespace kravidb::storage
