module sql.explain;

import std;
import sql.ast;
import sql.query;
import storage.tuple;

namespace kravidb::sql {
namespace {

[[nodiscard]] auto operator_name(BinaryOp binary) -> std::string_view {
  switch (binary) {
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

[[nodiscard]] auto position_suffix(const SourcePosition& position) -> std::string {
  if (position.line == 0 || position.column == 0) {
    return {};
  }
  return std::format(" @ {}:{}", position.line, position.column);
}

[[nodiscard]] auto decimal_text(const storage::Decimal& value) -> std::string {
  const bool negative = value.unscaled < 0;
  const auto magnitude =
      static_cast<std::uint64_t>(negative ? -(value.unscaled + 1) + 1 : value.unscaled);
  std::string digits = std::to_string(magnitude);
  if (value.scale > 0) {
    if (digits.size() <= value.scale) {
      digits.insert(0, static_cast<std::size_t>(value.scale) + 1 - digits.size(), '0');
    }
    digits.insert(digits.size() - value.scale, 1, '.');
  }
  if (negative) {
    digits.insert(0, 1, '-');
  }
  return digits;
}

[[nodiscard]] auto quoted_string(std::string_view text) -> std::string {
  std::string quoted;
  quoted.reserve(text.size() + 2);
  quoted.push_back('\'');
  for (const char character : text) {
    if (character == '\'') {
      quoted += "''";
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

[[nodiscard]] auto constant_text(const ConstValue& value) -> std::string {
  return std::visit(
      [](const auto& constant) -> std::string {
        using Type = std::decay_t<decltype(constant)>;
        if constexpr (std::is_same_v<Type, std::monostate>) {
          return "NULL";
        } else if constexpr (std::is_same_v<Type, std::int64_t>) {
          return std::to_string(constant);
        } else if constexpr (std::is_same_v<Type, bool>) {
          return constant ? "TRUE" : "FALSE";
        } else if constexpr (std::is_same_v<Type, storage::Decimal>) {
          return decimal_text(constant);
        } else {
          return quoted_string(constant);
        }
      },
      value);
}

[[nodiscard]] auto expression_node(const TypedExpr& expression) -> ExplainNode {
  if (const auto* column = std::get_if<ColumnVar>(&expression.node); column != nullptr) {
    return ExplainNode{
        .label = std::format("{} : {} (#{}){}", column->name, type_name(expression.type),
                             column->attnum, position_suffix(expression.position)),
        .kind = ExplainKind::column,
    };
  }

  if (const auto* constant = std::get_if<Const>(&expression.node); constant != nullptr) {
    return ExplainNode{
        .label = std::format("{} : {}{}", constant_text(constant->value),
                             type_name(expression.type), position_suffix(expression.position)),
        .kind = ExplainKind::constant,
    };
  }

  const auto& operation = std::get<OpExpr>(expression.node);
  ExplainNode node{
      .label = std::format("{} : {}{}", operator_name(operation.op), type_name(expression.type),
                           position_suffix(expression.position)),
      .kind = ExplainKind::operator_,
  };
  if (operation.left != nullptr) {
    node.children.push_back(expression_node(*operation.left));
  }
  if (operation.right != nullptr) {
    node.children.push_back(expression_node(*operation.right));
  }
  return node;
}

auto append_node(std::string& output, const ExplainNode& node, std::string_view prefix, bool last,
                 bool root) -> void {
  if (!root) {
    output += prefix;
    output += last ? "└─ " : "├─ ";
  }
  output += node.label;
  output += '\n';

  const std::string child_prefix =
      root ? std::string{} : std::string{prefix} + (last ? "   " : "│  ");
  for (std::size_t index = 0; index < node.children.size(); ++index) {
    append_node(output, node.children.at(index), child_prefix, index + 1 == node.children.size(),
                false);
  }
}

}  // namespace

auto explain_query(const Query& query) -> ExplainNode {
  ExplainNode root{
      .label = std::format("SelectStmt {} (oid={})", query.relation_name, query.relation),
      .kind = ExplainKind::statement,
  };

  ExplainNode targets{.label = "Targets", .kind = ExplainKind::targets};
  for (const auto& target : query.targets) {
    targets.children.push_back(ExplainNode{
        .label = std::format("{} : {} (#{})", target.name, type_name(target.type), target.attnum),
        .kind = ExplainKind::target,
    });
  }
  root.children.push_back(std::move(targets));

  ExplainNode where{.label = "Where", .kind = ExplainKind::where};
  if (query.where != nullptr) {
    where.label += std::format(" : {}", type_name(query.where->type));
    where.children.push_back(expression_node(*query.where));
  } else {
    where.label += " : none";
  }
  root.children.push_back(std::move(where));

  return root;
}

auto format_query(const Query& query) -> std::string {
  std::string output;
  append_node(output, explain_query(query), "", true, true);
  if (!output.empty()) {
    output.pop_back();
  }
  return output;
}

}  // namespace kravidb::sql
