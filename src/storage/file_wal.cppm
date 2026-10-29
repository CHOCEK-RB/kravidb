/// \file
/// \brief File-backed write-ahead log with segment files.

export module storage.file_wal;

import std;
export import storage.wal;

export namespace kravidb::storage {

/// \brief WAL backed by one file per segment under a directory.
///
/// Segment names are 24 hexadecimal digits holding the timeline, log and
/// segment numbers. Each segment is split into 8 KiB pages with a 24-byte
/// header. A record may span several pages through continuation headers and
/// never crosses a segment boundary. One writer may own a directory at a
/// time; readers may reopen it to inspect durable data.
class FileWal final : public Wal {
  public:
  explicit FileWal(std::filesystem::path directory, std::uint32_t timeline = 1,
                   std::size_t segment_size = wal_segment_size);
  ~FileWal() override;

  FileWal(const FileWal&) = delete;
  auto operator=(const FileWal&) -> FileWal& = delete;
  FileWal(FileWal&&) = delete;
  auto operator=(FileWal&&) -> FileWal& = delete;

  auto insert(const WalRecord& record) -> LSN override;
  auto flush(LSN upto) -> void override;
  [[nodiscard]] auto needs_flush(LSN upto) const -> bool override;

  /// \brief Reads the record at `from`.
  ///
  /// `from` is a record start, a page boundary, or the `next_lsn` returned by
  /// an earlier read. `next_lsn` is the physical LSN immediately after the
  /// record, including any page headers crossed. Reading past the last record
  /// throws `std::out_of_range`.
  ///
  /// Spans in the result point into storage owned by this object and stay
  /// valid until the next `read`, `flush`, `insert`, or destruction.
  [[nodiscard]] auto read(LSN from) -> WalReadResult override;

  auto switch_segment() -> void override;
  auto recycle(LSN before) -> void override;
  auto close() -> void override;

  private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace kravidb::storage
