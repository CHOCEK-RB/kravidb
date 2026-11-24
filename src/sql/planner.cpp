module sql.planner;

import std;

namespace kravidb::sql {
namespace {

constexpr double tuple_cpu_cost = 0.01;
constexpr double random_page_cost = 4.0;
constexpr double equality_selectivity = 0.01;
constexpr double range_selectivity = 1.0 / 3.0;

auto reverse_comparison(BinaryOp op) -> BinaryOp {
  switch (op) {
    case BinaryOp::lt:
      return BinaryOp::gt;
    case BinaryOp::le:
      return BinaryOp::ge;
    case BinaryOp::gt:
      return BinaryOp::lt;
    case BinaryOp::ge:
      return BinaryOp::le;
    default:
      return op;
  }
}

auto index_condition(const OpExpr& op) -> std::optional<IndexCondition> {
  if (op.op != BinaryOp::eq && op.op != BinaryOp::lt && op.op != BinaryOp::le &&
      op.op != BinaryOp::gt && op.op != BinaryOp::ge) {
    return std::nullopt;
  }
  if (!op.left || !op.right) {
    return std::nullopt;
  }
  const TypedExpr* column_expr = op.left.get();
  const TypedExpr* value_expr = op.right.get();
  auto comparison = op.op;
  if (std::holds_alternative<Const>(column_expr->node)) {
    std::swap(column_expr, value_expr);
    comparison = reverse_comparison(comparison);
  }
  const auto* column = std::get_if<ColumnVar>(&column_expr->node);
  const auto* value = std::get_if<Const>(&value_expr->node);
  if (column == nullptr || value == nullptr || column_expr->type != value_expr->type) {
    return std::nullopt;
  }
  const bool bigint = column_expr->type.kind == TypeKind::bigint &&
                      std::holds_alternative<std::int64_t>(value->value);
  const bool varchar = column_expr->type.kind == TypeKind::varchar &&
                       std::holds_alternative<std::string>(value->value);
  if (!bigint && !varchar) {
    return std::nullopt;
  }
  return IndexCondition{.attnum = column->attnum, .comparison = comparison, .value = value->value};
}

auto conditions(const TypedExpr* expression) -> std::vector<IndexCondition> {
  std::vector<IndexCondition> result;
  std::vector<const TypedExpr*> pending{expression};
  while (!pending.empty()) {
    const auto* current = pending.back();
    pending.pop_back();
    if (current == nullptr) {
      continue;
    }
    const auto* op = std::get_if<OpExpr>(&current->node);
    if (op == nullptr) {
      continue;
    }
    if (op->op == BinaryOp::and_) {
      pending.push_back(op->right.get());
      pending.push_back(op->left.get());
    } else if (auto condition = index_condition(*op)) {
      result.push_back(std::move(*condition));
    }
  }
  return result;
}

auto valid_count(double count) -> bool {
  return std::isfinite(count) && count >= 0;
}

}  // namespace

auto plan_select(const Query& query, RelationStats stats, std::span<const PlannerIndex> indexes)
    -> SqlResult<ReadPlan> {
  if (query.relation == storage::invalid_oid || !valid_count(stats.pages) ||
      !valid_count(stats.tuples)) {
    return std::unexpected{SqlError{.sqlstate = std::string{sqlstate::invalid_parameter_value},
                                    .message = "invalid planner relation or statistics",
                                    .position = {}}};
  }
  ReadPlan plan{.relation = query.relation,
                .targets = query.targets,
                .has_filter = query.where != nullptr,
                .scan = {.total_cost = stats.pages + stats.tuples * tuple_cpu_cost,
                         .estimated_rows = stats.tuples}};
  for (const auto& index : indexes) {
    if (index.oid == storage::invalid_oid || index.first_attnum <= 0 || !valid_count(index.pages)) {
      return std::unexpected{SqlError{.sqlstate = std::string{sqlstate::invalid_parameter_value},
                                      .message = "invalid planner index metadata",
                                      .position = {}}};
    }
    for (const auto& condition : conditions(query.where.get())) {
      if (condition.attnum != index.first_attnum) {
        continue;
      }
      const bool equality = condition.comparison == BinaryOp::eq;
      const double selectivity =
          equality ? (index.unique ? 1.0 / std::max(1.0, stats.tuples) : equality_selectivity)
                   : range_selectivity;
      const double rows = stats.tuples * selectivity;
      const double startup = std::log2(std::max(1.0, index.pages) + 1.0);
      const double cost =
          startup + std::min(stats.pages, rows) * random_page_cost + rows * tuple_cpu_cost;
      if (cost < plan.scan.total_cost) {
        plan.scan = {.kind = ScanKind::index,
                     .index_oid = index.oid,
                     .condition = condition,
                     .startup_cost = startup,
                     .total_cost = cost,
                     .estimated_rows = rows};
      }
    }
  }
  return plan;
}

auto explain_plan(const ReadPlan& plan) -> std::string {
  std::string text = std::format("Projection (columns={})\n", plan.targets.size());
  if (plan.has_filter) {
    text += "  Filter (recheck complete WHERE)\n";
  }
  const auto indent = plan.has_filter ? "    " : "  ";
  if (plan.scan.kind == ScanKind::index) {
    text += std::format("{}IndexScan (relation={} index={}", indent, plan.relation,
                        plan.scan.index_oid);
  } else {
    text += std::format("{}SeqScan (relation={}", indent, plan.relation);
  }
  text += std::format(" cost={:.3f}..{:.3f} rows={:.3f})\n", plan.scan.startup_cost,
                      plan.scan.total_cost, plan.scan.estimated_rows);
  return text;
}

}  // namespace kravidb::sql
