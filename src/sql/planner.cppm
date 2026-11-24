/// \file
/// \brief Cost-based read planner over analyzed SELECT queries.

export module sql.planner;

import std;
export import sql.query;
export import sql.error;

export namespace kravidb::sql {

/// \brief Statistics supplied by the relation owner, not inferred from SQL.
struct RelationStats {
  double pages{0};
  double tuples{0};
};

/// \brief A usable, published index supplied by the storage owner.
///
/// Only the first index attribute can constrain a scan. The planner never
/// treats a primary-key declaration alone as proof of a physical index.
struct PlannerIndex {
  storage::Oid oid{storage::invalid_oid};
  std::int16_t first_attnum{0};
  bool unique{false};
  double pages{1};
};

enum class ScanKind : std::uint8_t { sequential, index };

/// \brief One safe index condition; the complete WHERE must still be rechecked.
struct IndexCondition {
  std::int16_t attnum{0};
  BinaryOp comparison{BinaryOp::eq};
  ConstValue value;
};

/// \brief Chosen access path and its cost estimate.
struct ScanPlan {
  ScanKind kind{ScanKind::sequential};
  storage::Oid index_oid{storage::invalid_oid};
  std::optional<IndexCondition> condition;
  double startup_cost{0};
  double total_cost{0};
  double estimated_rows{0};
};

/// \brief Diagnostic SELECT plan: Projection -> optional Filter -> scan.
///
/// The plan owns its output metadata/condition. It does not own an executable
/// expression tree and cannot execute SQL or promise row order.
struct ReadPlan {
  storage::Oid relation{storage::invalid_oid};
  std::vector<TargetEntry> targets;
  bool has_filter{false};
  ScanPlan scan;
};

/// \brief Chooses the cheapest safe sequential/index path.
///
/// Costs: sequential page=1, random heap page=4, tuple CPU=0.01. Equality
/// selectivity is 1/N for unique indexes, otherwise 0.01; range is 1/3.
/// Only BIGINT/VARCHAR column-to-constant comparisons of identical types can
/// become index conditions. OR and NOT EQUAL never supply a partial index scan.
[[nodiscard]] auto plan_select(const Query& query, RelationStats stats,
                               std::span<const PlannerIndex> indexes = {}) -> SqlResult<ReadPlan>;

/// \brief Human-readable plan tree with access paths, estimates and costs.
[[nodiscard]] auto explain_plan(const ReadPlan& plan) -> std::string;

}  // namespace kravidb::sql
