/// \file
/// \brief Statement-level transaction contract and its no-op adapter.

export module storage.transaction;

import std;

export namespace kravidb::storage {

/// \brief Transaction identifier, one per statement.
using Xid = std::uint32_t;

/// \brief XID that names no transaction.
inline constexpr Xid invalid_xid = 0;

/// \brief Visible transaction window of a statement.
struct Snapshot {
  Xid xmin{invalid_xid};
  Xid xmax{invalid_xid};
  std::vector<Xid> xip;
};

/// \brief Statement-scoped transaction handle.
struct Transaction {
  /// \brief XID assigned to the statement.
  Xid xid{invalid_xid};
};

/// \brief Contract of the transaction manager.
///
/// Every statement runs in its own transaction: the engine begins one XID,
/// passes a `Transaction` through the execution path and commits or aborts it.
class TransactionManager {
  public:
  TransactionManager() = default;
  TransactionManager(const TransactionManager&) = delete;
  auto operator=(const TransactionManager&) -> TransactionManager& = delete;
  TransactionManager(TransactionManager&&) = delete;
  auto operator=(TransactionManager&&) -> TransactionManager& = delete;
  virtual ~TransactionManager() = default;

  /// \brief Starts a statement and returns its XID.
  [[nodiscard]] virtual auto begin_statement() -> Xid = 0;

  /// \brief Commits the statement identified by `xid`.
  virtual auto commit_statement(Xid xid) -> void = 0;

  /// \brief Aborts the statement identified by `xid`.
  virtual auto abort_statement(Xid xid) -> void = 0;

  /// \brief Visibility snapshot of the statement identified by `xid`.
  [[nodiscard]] virtual auto snapshot_for(Xid xid) const -> Snapshot = 0;
};

/// \brief Transaction adapter that only hands out increasing XIDs.
class NoopTransactionManager final : public TransactionManager {
  public:
  [[nodiscard]] auto begin_statement() -> Xid override { return ++last_xid_; }
  auto commit_statement(Xid xid) -> void override {}
  auto abort_statement(Xid xid) -> void override {}
  [[nodiscard]] auto snapshot_for(Xid xid) const -> Snapshot override {
    return Snapshot{.xmin = xid, .xmax = xid + 1};
  }

  private:
  Xid last_xid_{invalid_xid};
};

}  // namespace kravidb::storage
