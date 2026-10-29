module sql.analyzer;

import std;
import sql.ast;
import sql.error;
import sql.parser;
import sql.query;
import storage.catalog;
import storage.transaction;
import storage.tuple;

namespace kravidb::sql {
namespace {

using storage::AttributeDesc;
using storage::RelationDesc;

constexpr std::uint64_t decimal_radix = 10U;

/// \brief `atttypmod` of an attribute without type modifier.
constexpr std::int32_t unconstrained_typmod = -1;

[[nodiscard]] auto make_error(std::string_view state, std::string message, SourcePosition position)
    -> SqlError {
  return SqlError{
      .sqlstate = std::string{state},
      .message = std::move(message),
      .position = position,
  };
}

[[nodiscard]] auto is_numeric(const SqlType& type) noexcept -> bool {
  return type.kind == TypeKind::bigint || type.kind == TypeKind::decimal;
}

[[nodiscard]] auto is_comparable(const SqlType& left, const SqlType& right) noexcept -> bool {
  if (left.kind == TypeKind::null || right.kind == TypeKind::null) {
    return true;
  }
  if (is_numeric(left) && is_numeric(right)) {
    return true;
  }
  return left.kind == right.kind;
}

[[nodiscard]] auto operator_text(BinaryOp operation) -> std::string_view {
  switch (operation) {
    case BinaryOp::eq:
      return "=";
    case BinaryOp::ne:
      return "<>";
    case BinaryOp::lt:
      return "<";
    case BinaryOp::le:
      return "<=";
    case BinaryOp::gt:
      return ">";
    case BinaryOp::ge:
      return ">=";
    case BinaryOp::and_:
      return "AND";
    case BinaryOp::or_:
      return "OR";
    case BinaryOp::is_null:
      return "IS NULL";
    case BinaryOp::is_not_null:
      return "IS NOT NULL";
  }
  return "?";
}

/// \brief Number of decimal digits of `value`; zero has no significant digits.
[[nodiscard]] auto digit_count(std::uint64_t value) noexcept -> std::size_t {
  std::size_t digits = 0;
  while (value != 0) {
    value /= decimal_radix;
    ++digits;
  }
  return digits;
}

[[nodiscard]] auto magnitude(std::int64_t value) noexcept -> std::uint64_t {
  const auto bits = static_cast<std::uint64_t>(value);
  return value < 0 ? (~bits + 1U) : bits;
}

/// \brief Integer digits of a numeric constant, or nullopt for non-numeric values.
[[nodiscard]] auto integer_digits(const ConstValue& value) -> std::optional<std::size_t> {
  if (const auto* integer = std::get_if<std::int64_t>(&value)) {
    return digit_count(magnitude(*integer));
  }
  if (const auto* decimal = std::get_if<storage::Decimal>(&value)) {
    std::uint64_t whole = magnitude(decimal->unscaled);
    for (std::uint8_t step = 0; step < decimal->scale; ++step) {
      whole /= decimal_radix;
    }
    return digit_count(whole);
  }
  return std::nullopt;
}

/// \brief Converts exact decimal literal text (e.g. `-12.50`) into a storage decimal.
[[nodiscard]] auto parse_decimal(std::string_view text, SourcePosition position)
    -> SqlResult<storage::Decimal> {
  const bool negative = text.starts_with('-');
  std::string_view digits = negative ? text.substr(1) : text;

  const std::size_t dot = digits.find('.');
  const std::string_view whole = digits.substr(0, dot);
  const std::string_view fraction =
      dot == std::string_view::npos ? std::string_view{} : digits.substr(dot + 1);

  if (fraction.size() > storage::decimal_max_scale) {
    return std::unexpected{
        make_error(sqlstate::numeric_value_out_of_range,
                   std::format("numeric literal \"{}\" exceeds the maximum scale of {}", text,
                               storage::decimal_max_scale),
                   position)};
  }

  const std::uint64_t limit =
      negative ? magnitude(std::numeric_limits<std::int64_t>::min())
               : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

  std::uint64_t accumulated = 0;
  for (const std::string_view part : {whole, fraction}) {
    for (const char digit : part) {
      const auto value = static_cast<std::uint64_t>(digit - '0');
      if (accumulated > (limit - value) / decimal_radix) {
        return std::unexpected{
            make_error(sqlstate::numeric_value_out_of_range,
                       std::format("numeric literal \"{}\" is out of range", text), position)};
      }
      accumulated = (accumulated * decimal_radix) + value;
    }
  }

  std::int64_t unscaled = 0;
  if (negative) {
    unscaled = accumulated == limit ? std::numeric_limits<std::int64_t>::min()
                                    : -static_cast<std::int64_t>(accumulated);
  } else {
    unscaled = static_cast<std::int64_t>(accumulated);
  }
  return storage::Decimal{.scale = static_cast<std::uint8_t>(fraction.size()),
                          .unscaled = unscaled};
}

[[nodiscard]] auto simple_type(TypeKind kind) -> SqlType {
  return SqlType{.kind = kind, .decimal = std::nullopt};
}

[[nodiscard]] auto boolean_type() -> SqlType {
  return simple_type(TypeKind::boolean);
}

/// \brief SQL type of a catalog attribute.
[[nodiscard]] auto column_type(const AttributeDesc& attribute, SourcePosition position)
    -> SqlResult<SqlType> {
  switch (attribute.type_oid) {
    case storage::int8_oid:
      return simple_type(TypeKind::bigint);
    case storage::varchar_oid:
      return simple_type(TypeKind::varchar);
    case storage::numeric_oid:
      break;
    default:
      return std::unexpected{make_error(sqlstate::internal_error,
                                        std::format("column \"{}\" has unsupported type oid {}",
                                                    attribute.name, attribute.type_oid),
                                        position)};
  }

  if (attribute.typmod == unconstrained_typmod) {
    return simple_type(TypeKind::decimal);
  }

  const auto invalid = [&attribute, position]() -> SqlResult<SqlType> {
    return std::unexpected{make_error(
        sqlstate::invalid_parameter_value,
        std::format("column \"{}\" has invalid DECIMAL typmod {}: precision must be between 1 "
                    "and {} and scale must not exceed it",
                    attribute.name, attribute.typmod, max_decimal_precision),
        position)};
  };

  const auto typmod = storage::decode_typmod(attribute.typmod);
  if (!typmod.has_value() || typmod->precision > max_decimal_precision) {
    return invalid();
  }
  return SqlType{.kind = TypeKind::decimal,
                 .decimal = DecimalType{.precision = typmod->precision, .scale = typmod->scale}};
}

/// \brief Allocates a typed expression node.
[[nodiscard]] auto make_typed(TypedExpr::Node node, SqlType type, SourcePosition position)
    -> std::unique_ptr<TypedExpr> {
  return std::make_unique<TypedExpr>(
      TypedExpr{.node = std::move(node), .type = type, .position = position});
}

/// \brief Error if `expr` is not boolean (or the NULL literal).
[[nodiscard]] auto require_boolean(const TypedExpr& expr, std::string_view context)
    -> std::optional<SqlError> {
  if (expr.type.kind == TypeKind::boolean || expr.type.kind == TypeKind::null) {
    return std::nullopt;
  }
  return make_error(sqlstate::datatype_mismatch,
                    std::format("argument of {} must be type boolean, not type {}", context,
                                type_name(expr.type)),
                    expr.position);
}

/// \brief Error if a numeric constant cannot fit the `DECIMAL(p,s)` column it is compared with.
[[nodiscard]] auto check_decimal_fit(const TypedExpr& column, const TypedExpr& constant)
    -> std::optional<SqlError> {
  if (!std::holds_alternative<ColumnVar>(column.node) || !column.type.decimal.has_value()) {
    return std::nullopt;
  }
  const auto* value = std::get_if<Const>(&constant.node);
  if (value == nullptr) {
    return std::nullopt;
  }
  const auto digits = integer_digits(value->value);
  if (!digits.has_value()) {
    return std::nullopt;
  }

  const DecimalType declared = column.type.decimal.value();
  const std::size_t allowed = static_cast<std::size_t>(declared.precision) - declared.scale;
  if (*digits <= allowed) {
    return std::nullopt;
  }
  return make_error(
      sqlstate::numeric_value_out_of_range,
      std::format("numeric value has {} integer digits but column \"{}\" of type "
                  "{} allows at most {}",
                  *digits, std::get<ColumnVar>(column.node).name, type_name(column.type), allowed),
      constant.position);
}

class Analyzer {
  public:
  explicit Analyzer(const RelationDesc& relation) : relation_{&relation} {}

  [[nodiscard]] auto find_attribute(const Identifier& name) const
      -> SqlResult<const AttributeDesc*> {
    const auto match = std::ranges::find_if(
        relation_->attributes,
        [&name](const AttributeDesc& attribute) -> bool { return attribute.name == name.name; });
    if (match == relation_->attributes.end()) {
      return std::unexpected{make_error(
          sqlstate::undefined_column,
          std::format(R"(column "{}" of relation "{}" does not exist)", name.name, relation_->name),
          name.position)};
    }
    return &*match;
  }

  [[nodiscard]] auto analyze_expr(const Expr& expr) -> SqlResult<std::unique_ptr<TypedExpr>> {
    if (const auto* column = std::get_if<ColumnRef>(&expr.node)) {
      return analyze_node(*column);
    }
    if (const auto* literal = std::get_if<Literal>(&expr.node)) {
      return analyze_node(*literal);
    }
    return analyze_node(std::get<BinaryExpr>(expr.node));
  }

  private:
  [[nodiscard]] auto analyze_node(const ColumnRef& column) const
      -> SqlResult<std::unique_ptr<TypedExpr>> {
    auto attribute = find_attribute(column.name);
    if (!attribute.has_value()) {
      return std::unexpected{std::move(attribute.error())};
    }
    auto type = column_type(*attribute.value(), column.name.position);
    if (!type.has_value()) {
      return std::unexpected{std::move(type.error())};
    }
    return make_typed(ColumnVar{.attnum = attribute.value()->attnum, .name = column.name.name},
                      type.value(), column.name.position);
  }

  [[nodiscard]] static auto analyze_node(const Literal& literal)
      -> SqlResult<std::unique_ptr<TypedExpr>> {
    return std::visit(
        [&literal](const auto& value) -> SqlResult<std::unique_ptr<TypedExpr>> {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, std::int64_t>) {
            return make_typed(Const{.value = value}, simple_type(TypeKind::bigint),
                              literal.position);
          } else if constexpr (std::is_same_v<Value, DecimalLiteral>) {
            auto decimal = parse_decimal(value.text, literal.position);
            if (!decimal.has_value()) {
              return std::unexpected{std::move(decimal.error())};
            }
            return make_typed(Const{.value = decimal.value()}, simple_type(TypeKind::decimal),
                              literal.position);
          } else if constexpr (std::is_same_v<Value, std::string>) {
            return make_typed(Const{.value = value}, simple_type(TypeKind::varchar),
                              literal.position);
          } else {
            return make_typed(Const{.value = std::monostate{}}, simple_type(TypeKind::null),
                              literal.position);
          }
        },
        literal.value);
  }

  [[nodiscard]] auto analyze_node(const BinaryExpr& binary)
      -> SqlResult<std::unique_ptr<TypedExpr>> {
    auto left = analyze_expr(*binary.left);
    if (!left.has_value()) {
      return left;
    }

    if (binary.op == BinaryOp::is_null || binary.op == BinaryOp::is_not_null) {
      return make_typed(OpExpr{.op = binary.op, .left = std::move(left.value()), .right = nullptr},
                        boolean_type(), binary.position);
    }

    auto right = analyze_expr(*binary.right);
    if (!right.has_value()) {
      return right;
    }

    if (binary.op == BinaryOp::and_ || binary.op == BinaryOp::or_) {
      const std::string_view context = operator_text(binary.op);
      if (auto error = require_boolean(*left.value(), context)) {
        return std::unexpected{std::move(error.value())};
      }
      if (auto error = require_boolean(*right.value(), context)) {
        return std::unexpected{std::move(error.value())};
      }
    } else {
      if (!is_comparable(left.value()->type, right.value()->type)) {
        return std::unexpected{
            make_error(sqlstate::datatype_mismatch,
                       std::format("cannot compare {} {} {}", type_name(left.value()->type),
                                   operator_text(binary.op), type_name(right.value()->type)),
                       binary.position)};
      }
      if (auto error = check_decimal_fit(*left.value(), *right.value())) {
        return std::unexpected{std::move(error.value())};
      }
      if (auto error = check_decimal_fit(*right.value(), *left.value())) {
        return std::unexpected{std::move(error.value())};
      }
    }

    return make_typed(
        OpExpr{.op = binary.op, .left = std::move(left.value()), .right = std::move(right.value())},
        boolean_type(), binary.position);
  }

  const RelationDesc* relation_;
};

[[nodiscard]] auto statement_position(const Stmt& statement) -> SourcePosition {
  return std::visit(
      [](const auto& node) -> SourcePosition {
        if constexpr (requires { node.table.position; }) {
          return node.table.position;
        } else {
          return node.index.position;
        }
      },
      statement);
}

[[nodiscard]] auto unsupported_statement(const Stmt& statement) -> SqlError {
  return make_error(sqlstate::feature_not_supported, "only SELECT statements can be analyzed",
                    statement_position(statement));
}

[[nodiscard]] auto type_oid(const TypeName& type) -> storage::Oid {
  switch (type.kind) {
    case TypeNameKind::bigint:
      return storage::int8_oid;
    case TypeNameKind::varchar:
      return storage::varchar_oid;
    case TypeNameKind::decimal:
      return storage::numeric_oid;
  }
  return storage::invalid_oid;
}

[[nodiscard]] auto attribute_from(const ColumnDef& column) -> SqlResult<AttributeDesc> {
  std::int32_t typmod = unconstrained_typmod;
  if (column.type.kind == TypeNameKind::decimal) {
    if (!column.type.decimal.has_value() || column.type.decimal->precision == 0 ||
        column.type.decimal->precision > max_decimal_precision ||
        column.type.decimal->scale > column.type.decimal->precision) {
      return std::unexpected{make_error(
          sqlstate::invalid_parameter_value,
          std::format("invalid DECIMAL precision or scale for column \"{}\"", column.name.name),
          column.type.position)};
    }
    typmod = storage::encode_typmod(column.type.decimal->precision, column.type.decimal->scale);
  }
  return AttributeDesc{.name = column.name.name,
                       .type_oid = type_oid(column.type),
                       .attnum = 0,
                       .not_null = column.is_primary_key,
                       .typmod = typmod,
                       .primary_key = column.is_primary_key};
}

[[nodiscard]] auto require_table(const storage::Catalog& catalog, storage::Transaction& transaction,
                                 const Identifier& name) -> SqlResult<RelationDesc> {
  auto relation = catalog.lookup_relation(transaction, name.name);
  if (!relation.has_value()) {
    return std::unexpected{make_error(sqlstate::undefined_table,
                                      std::format("relation \"{}\" does not exist", name.name),
                                      name.position)};
  }
  if (relation->kind != storage::RelKind::table) {
    return std::unexpected{make_error(sqlstate::wrong_object_type,
                                      std::format("\"{}\" is not a table", name.name),
                                      name.position)};
  }
  return relation.value();
}

[[nodiscard]] auto require_attribute(const RelationDesc& relation, const Identifier& name)
    -> SqlResult<AttributeDesc> {
  const auto match = std::ranges::find_if(
      relation.attributes,
      [&name](const AttributeDesc& attribute) -> bool { return attribute.name == name.name; });
  if (match == relation.attributes.end()) {
    return std::unexpected{make_error(
        sqlstate::undefined_column,
        std::format("column \"{}\" of relation \"{}\" does not exist", name.name, relation.name),
        name.position)};
  }
  return *match;
}

[[nodiscard]] auto analyze_create_table(const CreateTableStmt& create,
                                        const storage::Catalog& catalog,
                                        storage::Transaction& transaction)
    -> SqlResult<AnalyzedStatement> {
  if (catalog.lookup_relation(transaction, create.table.name).has_value()) {
    return std::unexpected{make_error(
        sqlstate::duplicate_table, std::format("relation \"{}\" already exists", create.table.name),
        create.table.position)};
  }
  storage::TableDef definition{.name = create.table.name};
  std::unordered_set<std::string> names;
  std::size_t primary_keys = 0;
  for (std::size_t index = 0; index < create.columns.size(); ++index) {
    const auto& column = create.columns.at(index);
    if (!names.insert(column.name.name).second) {
      return std::unexpected{
          make_error(sqlstate::duplicate_column,
                     std::format("column \"{}\" specified more than once", column.name.name),
                     column.name.position)};
    }
    if (column.is_primary_key) {
      ++primary_keys;
      if (index != 0 || column.type.kind != TypeNameKind::bigint) {
        return std::unexpected{make_error(sqlstate::feature_not_supported,
                                          "primary key must be the first BIGINT column",
                                          column.name.position)};
      }
      definition.primary_key = column.name.name;
    }
    auto attribute = attribute_from(column);
    if (!attribute.has_value()) {
      return std::unexpected{std::move(attribute.error())};
    }
    definition.attributes.push_back(std::move(attribute.value()));
  }
  if (primary_keys != 1) {
    return std::unexpected{make_error(sqlstate::feature_not_supported,
                                      "CREATE TABLE requires exactly one primary key",
                                      create.table.position)};
  }
  return AnalyzedStatement{CreateTableCommand{.definition = std::move(definition)}};
}

[[nodiscard]] auto analyze_create_index(const CreateIndexStmt& create,
                                        const storage::Catalog& catalog,
                                        storage::Transaction& transaction)
    -> SqlResult<AnalyzedStatement> {
  if (catalog.lookup_relation(transaction, create.index.name).has_value()) {
    return std::unexpected{make_error(
        sqlstate::duplicate_table, std::format("relation \"{}\" already exists", create.index.name),
        create.index.position)};
  }
  auto relation = require_table(catalog, transaction, create.table);
  if (!relation.has_value()) {
    return std::unexpected{std::move(relation.error())};
  }
  auto attribute = require_attribute(relation.value(), create.column);
  if (!attribute.has_value()) {
    return std::unexpected{std::move(attribute.error())};
  }
  if (attribute->type_oid != storage::int8_oid) {
    return std::unexpected{make_error(sqlstate::feature_not_supported,
                                      "indexes are supported only on BIGINT columns",
                                      create.column.position)};
  }
  return AnalyzedStatement{
      CreateIndexCommand{.definition = storage::IndexDef{.relation = relation->oid,
                                                         .name = create.index.name,
                                                         .column = create.column.name,
                                                         .unique = create.unique}}};
}

[[nodiscard]] auto literal_matches(const Literal& literal, const AttributeDesc& attribute) -> bool {
  if (std::holds_alternative<NullLiteral>(literal.value)) {
    return !attribute.not_null;
  }
  if (attribute.type_oid == storage::int8_oid) {
    return std::holds_alternative<std::int64_t>(literal.value);
  }
  if (attribute.type_oid == storage::varchar_oid) {
    return std::holds_alternative<std::string>(literal.value);
  }
  return attribute.type_oid == storage::numeric_oid &&
         (std::holds_alternative<std::int64_t>(literal.value) ||
          std::holds_alternative<DecimalLiteral>(literal.value));
}

[[nodiscard]] auto analyze_literal_value(const Literal& literal, const AttributeDesc& attribute)
    -> SqlResult<Const> {
  if (std::holds_alternative<NullLiteral>(literal.value)) {
    if (attribute.not_null) {
      return std::unexpected{make_error(
          sqlstate::not_null_violation,
          std::format("null value in column \"{}\" violates not-null constraint", attribute.name),
          literal.position)};
    }
    return Const{.value = std::monostate{}};
  }
  if (!literal_matches(literal, attribute)) {
    return std::unexpected{
        make_error(sqlstate::datatype_mismatch,
                   std::format("value for column \"{}\" has incompatible type", attribute.name),
                   literal.position)};
  }
  if (const auto* value = std::get_if<std::int64_t>(&literal.value)) {
    if (attribute.type_oid == storage::numeric_oid) {
      return Const{.value = storage::Decimal{.scale = 0, .unscaled = *value}};
    }
    return Const{.value = *value};
  }
  if (const auto* value = std::get_if<std::string>(&literal.value)) {
    return Const{.value = *value};
  }
  auto decimal = parse_decimal(std::get<DecimalLiteral>(literal.value).text, literal.position);
  if (!decimal.has_value()) {
    return std::unexpected{std::move(decimal.error())};
  }
  return Const{.value = decimal.value()};
}

[[nodiscard]] auto analyze_insert(const InsertStmt& insert, const storage::Catalog& catalog,
                                  storage::Transaction& transaction)
    -> SqlResult<AnalyzedStatement> {
  auto relation = require_table(catalog, transaction, insert.table);
  if (!relation.has_value()) {
    return std::unexpected{std::move(relation.error())};
  }
  std::vector<AttributeDesc> attributes;
  if (insert.columns.empty()) {
    attributes = relation->attributes;
    std::ranges::sort(attributes, std::less{}, &AttributeDesc::attnum);
  } else {
    std::unordered_set<std::string> names;
    for (const auto& column : insert.columns) {
      if (!names.insert(column.name).second) {
        return std::unexpected{make_error(
            sqlstate::duplicate_column,
            std::format("column \"{}\" specified more than once", column.name), column.position)};
      }
      auto attribute = require_attribute(relation.value(), column);
      if (!attribute.has_value()) {
        return std::unexpected{std::move(attribute.error())};
      }
      attributes.push_back(std::move(attribute.value()));
    }
  }
  if (attributes.size() != insert.values.size()) {
    return std::unexpected{make_error(sqlstate::syntax_error,
                                      "INSERT has more target columns than expressions",
                                      insert.table.position)};
  }
  InsertCommand command{.relation = relation->oid};
  for (std::size_t index = 0; index < attributes.size(); ++index) {
    auto value = analyze_literal_value(insert.values.at(index), attributes.at(index));
    if (!value.has_value()) {
      return std::unexpected{std::move(value.error())};
    }
    command.values.push_back(
        AnalyzedValue{.attribute = attributes.at(index), .value = std::move(value.value())});
  }
  return AnalyzedStatement{std::move(command)};
}

[[nodiscard]] auto analyze_drop_table(const DropTableStmt& drop, const storage::Catalog& catalog,
                                      storage::Transaction& transaction)
    -> SqlResult<AnalyzedStatement> {
  auto relation = catalog.lookup_relation(transaction, drop.table.name);
  if (!relation.has_value()) {
    if (drop.if_exists) {
      return AnalyzedStatement{DropTableCommand{.if_exists = true}};
    }
    return std::unexpected{make_error(sqlstate::undefined_object,
                                      std::format("table \"{}\" does not exist", drop.table.name),
                                      drop.table.position)};
  }
  if (!relation->indexes.empty()) {
    return std::unexpected{make_error(
        sqlstate::dependent_objects_still_exist,
        std::format("cannot drop table \"{}\" because other objects depend on it", drop.table.name),
        drop.table.position)};
  }
  return AnalyzedStatement{
      DropTableCommand{.relation = relation->oid, .if_exists = drop.if_exists}};
}

[[nodiscard]] auto analyze_alter_drop(const AlterTableDropColumnStmt& alter,
                                      const storage::Catalog& catalog,
                                      storage::Transaction& transaction)
    -> SqlResult<AnalyzedStatement> {
  auto relation = require_table(catalog, transaction, alter.table);
  if (!relation.has_value()) {
    return std::unexpected{std::move(relation.error())};
  }
  auto attribute = require_attribute(relation.value(), alter.column);
  if (!attribute.has_value()) {
    return std::unexpected{std::move(attribute.error())};
  }
  if (attribute->primary_key || !relation->indexes.empty()) {
    return std::unexpected{
        make_error(sqlstate::dependent_objects_still_exist,
                   std::format("cannot drop indexed column \"{}\"", alter.column.name),
                   alter.column.position)};
  }
  return AnalyzedStatement{
      AlterTableDropColumnCommand{.relation = relation->oid, .attnum = attribute->attnum}};
}

/// \brief Output columns of `select`; `*` expands to every column in `attnum` order.
[[nodiscard]] auto resolve_targets(const SelectStmt& select, const RelationDesc& relation,
                                   const Analyzer& analyzer)
    -> SqlResult<std::vector<TargetEntry>> {
  std::vector<TargetEntry> targets;
  const auto add_target = [&targets](const AttributeDesc& attribute,
                                     SourcePosition position) -> std::optional<SqlError> {
    auto type = column_type(attribute, position);
    if (!type.has_value()) {
      return std::move(type.error());
    }
    targets.push_back(
        TargetEntry{.name = attribute.name, .attnum = attribute.attnum, .type = type.value()});
    return std::nullopt;
  };

  if (select.targets.empty()) {
    std::vector<const AttributeDesc*> attributes;
    attributes.reserve(relation.attributes.size());
    for (const auto& attribute : relation.attributes) {
      attributes.push_back(&attribute);
    }
    std::ranges::sort(attributes, std::less{}, [](const AttributeDesc* attribute) -> std::int16_t {
      return attribute->attnum;
    });
    for (const auto* attribute : attributes) {
      if (auto error = add_target(*attribute, select.table.position)) {
        return std::unexpected{std::move(error.value())};
      }
    }
    return targets;
  }

  targets.reserve(select.targets.size());
  for (const auto& target : select.targets) {
    auto attribute = analyzer.find_attribute(target);
    if (!attribute.has_value()) {
      return std::unexpected{std::move(attribute.error())};
    }
    if (auto error = add_target(*attribute.value(), target.position)) {
      return std::unexpected{std::move(error.value())};
    }
  }
  return targets;
}

/// \brief Analyzes `select` once its relation is looked up; `relation` is null when missing.
[[nodiscard]] auto analyze_resolved(const SelectStmt& select, const RelationDesc* relation)
    -> SqlResult<Query> {
  if (relation == nullptr) {
    return std::unexpected{make_error(
        sqlstate::undefined_table, std::format("relation \"{}\" does not exist", select.table.name),
        select.table.position)};
  }
  if (relation->kind != storage::RelKind::table) {
    return std::unexpected{make_error(sqlstate::wrong_object_type,
                                      std::format("\"{}\" is not a table", select.table.name),
                                      select.table.position)};
  }

  Analyzer analyzer{*relation};
  Query query{
      .relation = relation->oid,
      .relation_name = relation->name,
      .targets = {},
      .where = nullptr,
  };

  auto targets = resolve_targets(select, *relation, analyzer);
  if (!targets.has_value()) {
    return std::unexpected{std::move(targets.error())};
  }
  query.targets = std::move(targets.value());

  if (select.where != nullptr) {
    auto where = analyzer.analyze_expr(*select.where);
    if (!where.has_value()) {
      return std::unexpected{std::move(where.error())};
    }
    if (auto error = require_boolean(*where.value(), "WHERE")) {
      return std::unexpected{std::move(error.value())};
    }
    query.where = std::move(where.value());
  }

  return query;
}

}  // namespace
}  // namespace kravidb::sql

namespace kravidb::sql {

auto analyze_select(const SelectStmt& select, const storage::Catalog& catalog,
                    storage::Transaction& transaction) -> SqlResult<Query> {
  const auto relation = catalog.lookup_relation(transaction, select.table.name);
  return analyze_resolved(select, relation.has_value() ? &relation.value() : nullptr);
}

auto analyze_select(const SelectStmt& select, const storage::RelCache& cache) -> SqlResult<Query> {
  const auto relation = cache.relation(select.table.name);
  return analyze_resolved(select, relation.get());
}

auto analyze(const Stmt& statement, const storage::Catalog& catalog,
             storage::Transaction& transaction) -> SqlResult<Query> {
  if (const auto* select = std::get_if<SelectStmt>(&statement)) {
    return analyze_select(*select, catalog, transaction);
  }
  return std::unexpected{unsupported_statement(statement)};
}

auto analyze(const Stmt& statement, const storage::RelCache& cache) -> SqlResult<Query> {
  if (const auto* select = std::get_if<SelectStmt>(&statement)) {
    return analyze_select(*select, cache);
  }
  return std::unexpected{unsupported_statement(statement)};
}

auto analyze_statement(const Stmt& statement, const storage::Catalog& catalog,
                       storage::Transaction& transaction) -> SqlResult<AnalyzedStatement> {
  if (const auto* select = std::get_if<SelectStmt>(&statement)) {
    auto query = analyze_select(*select, catalog, transaction);
    if (!query.has_value()) {
      return std::unexpected{std::move(query.error())};
    }
    return AnalyzedStatement{std::move(query.value())};
  }
  if (const auto* create = std::get_if<CreateTableStmt>(&statement)) {
    return analyze_create_table(*create, catalog, transaction);
  }
  if (const auto* create = std::get_if<CreateIndexStmt>(&statement)) {
    return analyze_create_index(*create, catalog, transaction);
  }
  if (const auto* insert = std::get_if<InsertStmt>(&statement)) {
    return analyze_insert(*insert, catalog, transaction);
  }
  if (const auto* drop = std::get_if<DropTableStmt>(&statement)) {
    return analyze_drop_table(*drop, catalog, transaction);
  }
  if (const auto* alter = std::get_if<AlterTableDropColumnStmt>(&statement)) {
    return analyze_alter_drop(*alter, catalog, transaction);
  }
  return std::unexpected{unsupported_statement(statement)};
}

auto prepare_statement(std::string_view text, const storage::Catalog& catalog,
                       storage::Transaction& transaction) -> SqlResult<AnalyzedStatement> {
  auto statement = parse(text);
  if (!statement.has_value()) {
    return std::unexpected{std::move(statement.error())};
  }
  return analyze_statement(statement.value(), catalog, transaction);
}

auto prepare_query(std::string_view text, const storage::Catalog& catalog,
                   storage::Transaction& transaction) -> SqlResult<Query> {
  auto statement = parse(text);
  if (!statement.has_value()) {
    return std::unexpected{std::move(statement.error())};
  }
  return analyze(statement.value(), catalog, transaction);
}

auto prepare_query(std::string_view text, const storage::RelCache& cache) -> SqlResult<Query> {
  auto statement = parse(text);
  if (!statement.has_value()) {
    return std::unexpected{std::move(statement.error())};
  }
  return analyze(statement.value(), cache);
}

}  // namespace kravidb::sql
