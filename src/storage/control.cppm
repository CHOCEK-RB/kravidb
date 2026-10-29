/// \file
/// \brief Data directory, control file and instance lock.

export module storage.control;

import std;

export namespace kravidb::storage {

/// \brief Size of the packed control file on disk.
inline constexpr std::size_t kd_control_size = 88;

/// \brief Magic that identifies a kravidb control file.
inline constexpr std::uint64_t kd_control_magic = 0x4B444243544C3031ULL;

/// \brief On-disk format version of the control file.
inline constexpr std::uint32_t kd_format_version = 1;

/// \brief Contents of `KD_VERSION` for this format.
inline constexpr std::string_view kd_version_contents = "1\n";

/// \brief State stored in the control file.
enum class ControlState : std::uint32_t {
  clean = 0,
  in_progress = 1,
  recovery = 2,
};

/// \brief Write mode of one data directory.
enum class WriteMode : std::uint32_t {
  autocommit = 0,
  readonly = 1,
};

/// \brief Control file shared by every process that opens a data directory.
///
/// The struct is not written with `sizeof`: the on-disk layout is packed into
/// 88 bytes and encoded field by field.
struct ControlFile {
  std::uint64_t magic{kd_control_magic};
  std::uint32_t format_version{kd_format_version};
  std::uint64_t system_identifier{};
  std::uint64_t checkpoint_lsn{};
  std::uint64_t redo_lsn{};
  std::uint32_t timeline{1};
  ControlState state{ControlState::clean};
  std::uint32_t full_page_writes{1};
  std::uint32_t next_xid{1};
  std::uint32_t oldest_xid{1};
  WriteMode write_mode{WriteMode::autocommit};
  std::uint64_t base_id{};
  std::uint64_t previous_base{};
  std::uint64_t checkpoint_time{};
  std::uint32_t crc{};
};

/// \brief Encodes `control` into the 88-byte on-disk layout.
///
/// The CRC in the input is ignored and recomputed.
[[nodiscard]] auto encode_control(const ControlFile& control)
    -> std::array<std::byte, kd_control_size>;

/// \brief Decodes and validates an on-disk control file.
///
/// Throws `std::runtime_error` for a wrong size, magic, format, CRC or field
/// value.
[[nodiscard]] auto decode_control(std::span<const std::byte> bytes) -> ControlFile;

/// \brief Returns `KRAVIDB_DATA`, or `kd_data` when the variable is empty.
[[nodiscard]] auto default_data_directory() -> std::filesystem::path;

/// \brief Open data directory with its control file and exclusive lock.
///
/// The lock is held until this object is destroyed. The type is move-only.
class DataDirectory final {
  public:
  /// \brief Creates the layout when missing and completes a partial bootstrap.
  ///
  /// A directory that already has `KD_VERSION` but no `kd_control` is
  /// rejected: regenerating the control would change the system identifier.
  [[nodiscard]] static auto bootstrap(std::filesystem::path directory,
                                      WriteMode mode = WriteMode::autocommit) -> DataDirectory;

  /// \brief Opens an initialized data directory and takes the instance lock.
  [[nodiscard]] static auto open(std::filesystem::path directory) -> DataDirectory;

  ~DataDirectory();

  DataDirectory(DataDirectory&&) noexcept;
  auto operator=(DataDirectory&&) noexcept -> DataDirectory&;

  DataDirectory(const DataDirectory&) = delete;
  auto operator=(const DataDirectory&) -> DataDirectory& = delete;

  /// \brief Directory that owns this data set.
  [[nodiscard]] auto path() const -> const std::filesystem::path&;

  /// \brief Current in-memory control file.
  [[nodiscard]] auto control() const -> const ControlFile&;

  /// \brief Writes `control` atomically and updates the in-memory copy.
  auto write_control(const ControlFile& control) -> void;

  private:
  struct Impl;

  explicit DataDirectory(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace kravidb::storage
