/// \file
/// \brief Write-ahead log contract and its no-op adapter.

export module storage.wal;

import std;
export import storage.relation_file;

export namespace kravidb::storage {

/// \brief Log sequence number: byte position in the WAL stream.
using LSN = std::uint64_t;

/// \brief LSN that names no record.
inline constexpr LSN invalid_lsn = 0;

/// \brief Size of a WAL segment file.
inline constexpr std::size_t wal_segment_size = std::size_t{16} * 1024 * 1024;

/// \brief Size of a WAL page.
inline constexpr std::size_t wal_page_size = std::size_t{8} * 1024;

/// \brief Relation block referenced by a WAL record.
struct WalBlockRef {
  std::uint8_t id{0};
  Fork fork{Fork::main};
  BlockNumber block{0};
  std::span<const std::byte> data;
  std::optional<std::vector<std::byte>> full_image;
};

/// \brief One record of the write-ahead log.
struct WalRecord {
  std::uint8_t rmid{0};
  std::uint8_t info{0};
  std::uint32_t xid{0};
  std::vector<WalBlockRef> blocks;
  std::vector<std::byte> main_data;
};

/// \brief Contract of the write-ahead log.
///
/// A record is written before the pages it protects reach the relation file.
/// `insert` only appends to the log buffers; `flush` makes the prefix durable.
class Wal {
  public:
  Wal() = default;
  Wal(const Wal&) = delete;
  auto operator=(const Wal&) -> Wal& = delete;
  Wal(Wal&&) = delete;
  auto operator=(Wal&&) -> Wal& = delete;
  virtual ~Wal() = default;

  /// \brief Appends `record` and returns the LSN it starts at.
  virtual auto insert(const WalRecord& record) -> LSN = 0;

  /// \brief Makes every record up to `upto` durable.
  virtual auto flush(LSN upto) -> void = 0;

  /// \brief True when records up to `upto` are not durable yet.
  [[nodiscard]] virtual auto needs_flush(LSN upto) const -> bool = 0;

  /// \brief Reads the record that starts at `from`.
  [[nodiscard]] virtual auto read(LSN from) -> WalRecord = 0;

  /// \brief Forces the next insert into a new segment.
  virtual auto switch_segment() -> void = 0;

  /// \brief Discards segments that only hold records before `before`.
  virtual auto recycle(LSN before) -> void = 0;

  /// \brief Flushes pending records and releases the segments.
  virtual auto close() -> void = 0;
};

/// \brief WAL adapter that accepts records and stores nothing.
class NoopWal final : public Wal {
  public:
  auto insert(const WalRecord& record) -> LSN override;
  auto flush(LSN upto) -> void override {}
  [[nodiscard]] auto needs_flush(LSN upto) const -> bool override { return false; }
  [[nodiscard]] auto read(LSN from) -> WalRecord override;
  auto switch_segment() -> void override {}
  auto recycle(LSN before) -> void override {}
  auto close() -> void override {}

  private:
  LSN inserted_{invalid_lsn};
};

inline auto NoopWal::insert(const WalRecord& record) -> LSN {
  return ++inserted_;
}

inline auto NoopWal::read(LSN from) -> WalRecord {
  throw std::logic_error{"NoopWal does not store records"};
}

}  // namespace kravidb::storage
