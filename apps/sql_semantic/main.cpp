/// \file
/// \brief Interactive semantic analyzer visualizer built on FTXUI.
///
/// Shows the demo catalog, the decorated SELECT AST and the resolved columns.
/// `--print` renders the same AST as plain text for non-interactive use.

import std;
import kravidb;

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

namespace {

namespace sql = kravidb::sql;
namespace storage = kravidb::storage;

// Bright truecolor palette tuned for terminals with a dark background.
// Color channels are data, not magic constants.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
[[nodiscard]] auto text_color() -> ftxui::Color {
  return ftxui::Color::RGB(229, 231, 235);
}
[[nodiscard]] auto muted() -> ftxui::Color {
  return ftxui::Color::RGB(156, 163, 175);
}
[[nodiscard]] auto chrome() -> ftxui::Color {
  return ftxui::Color::RGB(148, 163, 184);
}
[[nodiscard]] auto accent() -> ftxui::Color {
  return ftxui::Color::RGB(96, 165, 250);
}
[[nodiscard]] auto success() -> ftxui::Color {
  return ftxui::Color::RGB(134, 239, 172);
}
[[nodiscard]] auto failure() -> ftxui::Color {
  return ftxui::Color::RGB(252, 165, 165);
}
[[nodiscard]] auto cursor_text() -> ftxui::Color {
  return ftxui::Color::RGB(15, 23, 42);
}

[[nodiscard]] auto explain_color(sql::ExplainKind kind) -> ftxui::Color {
  switch (kind) {
    case sql::ExplainKind::statement:
      return ftxui::Color::RGB(103, 232, 249);
    case sql::ExplainKind::targets:
      return ftxui::Color::RGB(253, 230, 138);
    case sql::ExplainKind::target:
      return ftxui::Color::RGB(147, 197, 253);
    case sql::ExplainKind::where:
      return ftxui::Color::RGB(240, 171, 252);
    case sql::ExplainKind::operator_:
      return ftxui::Color::RGB(252, 165, 165);
    case sql::ExplainKind::column:
      return ftxui::Color::RGB(134, 239, 172);
    case sql::ExplainKind::constant:
      return ftxui::Color::RGB(253, 230, 138);
  }
  return text_color();
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

constexpr int wide_catalog_width = 30;
constexpr int compact_catalog_width = 24;
constexpr int wide_resolution_width = 30;
constexpr int compact_resolution_width = 24;
constexpr int wide_terminal_width = 110;
constexpr int input_height = 3;
constexpr int minimum_main_height = 10;

[[nodiscard]] auto compact_layout() -> bool {
  return ftxui::Terminal::Size().dimx < wide_terminal_width;
}

[[nodiscard]] auto catalog_width() -> int {
  return compact_layout() ? compact_catalog_width : wide_catalog_width;
}

[[nodiscard]] auto resolution_width() -> int {
  return compact_layout() ? compact_resolution_width : wide_resolution_width;
}

struct Demo {
  std::string name;
  std::string sql;
};

[[nodiscard]] auto default_demos() -> std::vector<Demo> {
  return {
      {.name = "explicit projection", .sql = "SELECT id, name FROM students;"},
      {.name = "star expansion", .sql = "SELECT * FROM students;"},
      {.name = "compound predicate",
       .sql = "SELECT id, name FROM students WHERE gpa >= 3.5 AND active = TRUE;"},
      {.name = "duplicate target", .sql = "SELECT id, id FROM students;"},
      {.name = "unknown column", .sql = "SELECT nope FROM students;"},
      {.name = "unknown table", .sql = "SELECT name FROM missing;"},
      {.name = "type mismatch", .sql = "SELECT name FROM students WHERE gpa = 'high';"},
  };
}

[[nodiscard]] auto attribute(std::string name, storage::Oid type, bool primary_key = false)
    -> storage::AttributeDesc {
  return storage::AttributeDesc{
      .name = std::move(name),
      .type_oid = type,
      .attnum = 0,
      .not_null = primary_key,
      .typmod = -1,
      .primary_key = primary_key,
  };
}

[[nodiscard]] auto attribute_type(const storage::AttributeDesc& attribute) -> std::string {
  switch (attribute.type_oid) {
    case storage::int8_oid:
      return "bigint";
    case storage::varchar_oid:
      return "varchar";
    case storage::float8_oid:
      return "float";
    case storage::bool_oid:
      return "boolean";
    case storage::numeric_oid: {
      const auto typmod = storage::decode_typmod(attribute.typmod);
      if (typmod.has_value()) {
        return std::format("decimal({},{})", typmod->precision, typmod->scale);
      }
      return "decimal";
    }
    default:
      return std::format("oid={}", attribute.type_oid);
  }
}

struct State {
  storage::SchemaCatalog catalog;
  storage::Transaction transaction{};
  std::vector<Demo> demos;
  std::string source;
  std::string validated;
  std::string title;
  int cursor{0};
  std::size_t demo_index{0};
  bool editing{false};
  std::optional<sql::Query> query;
  std::optional<sql::SqlError> error;

  State() : demos(default_demos()) {
    create_schema();
    load_demo(0);
  }

  auto create_schema() -> void {
    catalog.bootstrap();
    catalog.create_table(transaction, storage::TableDef{
                                          .name = "students",
                                          .attributes =
                                              {
                                                  attribute("id", storage::int8_oid, true),
                                                  attribute("name", storage::varchar_oid),
                                                  attribute("gpa", storage::float8_oid),
                                                  attribute("active", storage::bool_oid),
                                              },
                                          .primary_key = "id",
                                      });
    catalog.create_table(transaction, storage::TableDef{
                                          .name = "courses",
                                          .attributes =
                                              {
                                                  attribute("id", storage::int8_oid, true),
                                                  attribute("title", storage::varchar_oid),
                                                  attribute("credits", storage::int8_oid),
                                              },
                                          .primary_key = "id",
                                      });
    catalog.create_table(transaction, storage::TableDef{
                                          .name = "enrollments",
                                          .attributes =
                                              {
                                                  attribute("student_id", storage::int8_oid),
                                                  attribute("course_id", storage::int8_oid),
                                                  attribute("grade", storage::float8_oid),
                                                  attribute("passed", storage::bool_oid),
                                              },
                                          .primary_key = "",
                                      });
  }

  auto analyze() -> void {
    auto result = sql::prepare_query(source, catalog, transaction);
    if (result.has_value()) {
      query = std::move(result.value());
      error.reset();
    } else {
      query.reset();
      error = std::move(result.error());
    }
  }

  auto load_demo(std::size_t index) -> void {
    demo_index = index;
    source = demos.at(index).sql;
    validated = source;
    title = demos.at(index).name;
    cursor = static_cast<int>(source.size());
    editing = false;
    analyze();
  }

  auto set_query(std::string text) -> void {
    source = std::move(text);
    validated = source;
    title = "custom";
    cursor = static_cast<int>(source.size());
    editing = false;
    analyze();
  }
};

[[nodiscard]] auto render_catalog(State& state) -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(ftxui::text(" Catalog") | ftxui::bold | ftxui::color(accent()));
  rows.push_back(ftxui::separator());
  for (const auto& relation : state.catalog.list_relations(state.transaction)) {
    rows.push_back(ftxui::text(std::format(" {} (#{})", relation.name, relation.oid)) |
                   ftxui::bold | ftxui::color(text_color()));
    for (const auto& column : relation.attributes) {
      const std::string primary = column.primary_key ? " PK" : "";
      rows.push_back(ftxui::text(std::format("   #{} {} : {}{}", column.attnum, column.name,
                                             attribute_type(column), primary)) |
                     ftxui::color(muted()));
    }
  }
  return ftxui::vbox(std::move(rows)) | ftxui::borderStyled(ftxui::LIGHT, chrome()) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, catalog_width());
}

[[nodiscard]] auto display_label(std::string label, bool show_positions, bool compact)
    -> std::string {
  if (!show_positions) {
    const auto position = label.find(" @ ");
    if (position != std::string::npos) {
      label.resize(position);
    }
  }
  if (compact) {
    const auto oid = label.find(" (oid=");
    if (oid != std::string::npos) {
      label.resize(oid);
    }
  }
  return label;
}

auto append_ast_rows(std::vector<ftxui::Element>& rows, const sql::ExplainNode& node,
                     std::string_view prefix, bool last, bool root, bool show_positions,
                     bool compact) -> void {
  std::string line;
  if (!root) {
    line += prefix;
    line += last ? "└─ " : "├─ ";
  }
  line += display_label(node.label, show_positions, compact);
  rows.push_back(ftxui::text(std::move(line)) | ftxui::color(explain_color(node.kind)));

  const std::string child_prefix =
      root ? std::string{} : std::string{prefix} + (last ? "   " : "│  ");
  for (std::size_t index = 0; index < node.children.size(); ++index) {
    append_ast_rows(rows, node.children.at(index), child_prefix, index + 1 == node.children.size(),
                    false, show_positions, compact);
  }
}

[[nodiscard]] auto render_ast(const State& state) -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(ftxui::text(" Decorated AST") | ftxui::bold | ftxui::color(accent()));
  rows.push_back(ftxui::separator());

  if (state.error.has_value()) {
    rows.push_back(ftxui::text(" no typed AST") | ftxui::color(failure()));
  } else if (state.query.has_value()) {
    const bool compact = compact_layout();
    append_ast_rows(rows, sql::explain_query(*state.query), "", true, true, !compact, compact);
  } else {
    rows.push_back(ftxui::text(" waiting for analysis") | ftxui::color(muted()));
  }

  return ftxui::vbox(std::move(rows)) | ftxui::borderStyled(ftxui::LIGHT, chrome()) | ftxui::flex;
}

[[nodiscard]] auto render_resolution(const State& state) -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(ftxui::text(" Resolved targets") | ftxui::bold | ftxui::color(accent()));
  rows.push_back(ftxui::separator());

  if (state.query.has_value()) {
    rows.push_back(
        ftxui::text(std::format(" {} (#{})", state.query->relation_name, state.query->relation)) |
        ftxui::color(text_color()));
    for (const auto& target : state.query->targets) {
      rows.push_back(ftxui::text(std::format("  #{} {} : {}", target.attnum, target.name,
                                             sql::type_name(target.type))) |
                     ftxui::color(muted()));
    }
  } else {
    rows.push_back(ftxui::text(" no resolved columns") | ftxui::color(muted()));
  }

  return ftxui::vbox(std::move(rows)) | ftxui::borderStyled(ftxui::LIGHT, chrome()) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, resolution_width());
}

[[nodiscard]] auto source_line(std::string_view source, std::size_t offset)
    -> std::pair<std::string, std::size_t> {
  const std::size_t position = std::min(offset, source.size());
  std::size_t start = position;
  while (start > 0 && source.at(start - 1) != '\n') {
    --start;
  }
  std::size_t end = position;
  while (end < source.size() && source.at(end) != '\n') {
    ++end;
  }
  return {std::string{source.substr(start, end - start)}, position - start};
}

[[nodiscard]] auto render_error(const sql::SqlError& error, std::string_view source)
    -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(ftxui::hbox({
      ftxui::text(" ERROR ") | ftxui::bold | ftxui::color(failure()),
      ftxui::text(error.sqlstate) | ftxui::bold | ftxui::color(failure()),
      ftxui::text(std::format(" · {}:{}", error.position.line, error.position.column)) |
          ftxui::color(muted()),
  }));
  rows.push_back(ftxui::paragraph(error.message) | ftxui::color(failure()));

  const std::size_t zero_based = error.position.offset == 0 ? 0 : error.position.offset - 1;
  const auto [line, column] = source_line(source, zero_based);
  if (!line.empty()) {
    rows.push_back(ftxui::text(line) | ftxui::color(text_color()));
    const std::size_t caret_column = error.position.column > 0 ? error.position.column - 1 : column;
    rows.push_back(ftxui::text(std::string(caret_column, ' ') + "^") | ftxui::color(failure()));
  }
  return ftxui::vbox(std::move(rows));
}

[[nodiscard]] auto render_status(const State& state) -> ftxui::Element {
  if (state.error.has_value()) {
    return render_error(*state.error, state.source);
  }
  if (!state.query.has_value()) {
    return ftxui::text(" waiting for analysis") | ftxui::color(muted());
  }
  const auto predicate = state.query->where != nullptr ? "1 predicate" : "no predicate";
  return ftxui::hbox({
      ftxui::text(" OK ") | ftxui::bold | ftxui::color(success()),
      ftxui::text(std::format("· {} targets · {}", state.query->targets.size(), predicate)) |
          ftxui::color(text_color()),
  });
}

[[nodiscard]] auto render_header(const State& state) -> ftxui::Element {
  const auto status = state.error.has_value() ? "ERROR" : "OK";
  const auto status_color = state.error.has_value() ? failure() : success();
  return ftxui::hbox({
      ftxui::text(" kravidb") | ftxui::bold | ftxui::color(accent()),
      ftxui::text(" | semantic analyzer") | ftxui::color(text_color()),
      ftxui::text(std::format(" · {}", state.title)) | ftxui::color(muted()),
      ftxui::filler(),
      ftxui::text(status) | ftxui::bold | ftxui::color(status_color),
  });
}

[[nodiscard]] auto render_footer(const State& state) -> ftxui::Element {
  const std::string hint =
      state.editing ? "Enter analizar · Esc cancelar" : "e editar · d demo · q salir";
  return ftxui::hbox({
      ftxui::text(std::format(" demo {}/{}", state.demo_index + 1, state.demos.size())) |
          ftxui::color(muted()),
      ftxui::filler(),
      ftxui::text(hint) | ftxui::color(muted()),
  });
}

[[nodiscard]] auto make_input(State& state) -> ftxui::Component {
  ftxui::InputOption option;
  option.content = &state.source;
  option.cursor_position = &state.cursor;
  option.multiline = false;
  option.transform = [&state](const ftxui::InputState& input_state) -> ftxui::Element {
    auto cursor_cell = ftxui::text(" ") | ftxui::bgcolor(accent()) | ftxui::color(cursor_text());
    if (input_state.is_placeholder) {
      return ftxui::hbox({
                 std::move(cursor_cell),
                 ftxui::text("SELECT ...") | ftxui::dim | ftxui::color(muted()),
             }) |
             ftxui::xflex | ftxui::xframe;
    }

    const std::size_t size = state.source.size();
    const auto cursor = std::min(static_cast<std::size_t>(std::max(state.cursor, 0)), size);
    const std::size_t next = std::min(cursor + 1, size);
    auto before = ftxui::text(std::string_view{state.source}.substr(0, cursor));
    auto cursor_char = cursor < size
                           ? ftxui::text(std::string_view{state.source}.substr(cursor, 1)) |
                                 ftxui::bgcolor(accent()) | ftxui::color(cursor_text())
                           : std::move(cursor_cell);
    auto after = ftxui::text(std::string_view{state.source}.substr(next));
    return ftxui::hbox({std::move(before), std::move(cursor_char), std::move(after)}) |
           ftxui::xflex | ftxui::xframe;
  };
  option.on_change = [&state] -> void { state.analyze(); };
  return ftxui::Input(option);
}

[[nodiscard]] auto render_document(State& state, const ftxui::Component& input) -> ftxui::Element {
  ftxui::Elements rows;
  rows.push_back(render_header(state));
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::hbox({
                     ftxui::text(" SQL ") | ftxui::color(accent()),
                     input->Render() | ftxui::xflex,
                 }) |
                 ftxui::borderStyled(ftxui::LIGHT, chrome()) |
                 ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, input_height));
  rows.push_back(ftxui::separator());
  rows.push_back(ftxui::hbox({
                     render_catalog(state),
                     render_ast(state) | ftxui::flex,
                     render_resolution(state),
                 }) |
                 ftxui::flex |
                 ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, minimum_main_height));
  rows.push_back(ftxui::separator());
  rows.push_back(render_status(state));
  rows.push_back(render_footer(state));
  return ftxui::vbox(std::move(rows)) | ftxui::flex | ftxui::borderStyled(ftxui::LIGHT, chrome());
}

struct Options {
  bool help{false};
  bool error{false};
  bool print{false};
  std::optional<std::string> query;
};

[[nodiscard]] auto parse_args(std::span<char*> args) -> Options {
  Options options;
  auto iterator = args.begin();
  if (iterator == args.end()) {
    return options;
  }
  ++iterator;
  for (; iterator != args.end(); ++iterator) {
    const std::string_view argument{*iterator};
    if (argument == "--help" || argument == "-h") {
      options.help = true;
      continue;
    }
    if (argument == "--print") {
      options.print = true;
      continue;
    }
    if (argument == "--query") {
      ++iterator;
      if (iterator == args.end()) {
        options.error = true;
        return options;
      }
      options.query = std::string{*iterator};
      continue;
    }
    options.error = true;
    return options;
  }
  return options;
}

auto print_usage(std::ostream& output) -> void {
  output << "Usage: kravidb_semantic [--print] [--query <sql>]\n\n"
            "  (no arguments)       interactive semantic analyzer visualizer\n"
            "  --print              print the decorated AST and exit\n"
            "  --query <sql>        analyze one query\n"
            "  -h, --help           show this help\n";
}

auto print_analysis(State& state) -> int {
  state.analyze();
  if (state.error.has_value()) {
    std::cerr << std::format("{}: {} at {}:{}\n", state.error->sqlstate, state.error->message,
                             state.error->position.line, state.error->position.column);
    return 1;
  }
  if (!state.query.has_value()) {
    std::cerr << "kravidb_semantic: no query to print\n";
    return 1;
  }
  std::cout << sql::format_query(*state.query) << '\n';
  return 0;
}

auto run(const std::optional<std::string>& initial_query) -> void {
  State state;
  if (initial_query.has_value()) {
    state.set_query(*initial_query);
  }

  auto input = make_input(state);
  auto screen = ftxui::App::Fullscreen();
  screen.TrackMouse(false);

  auto root =
      ftxui::Renderer([&state, &input] -> ftxui::Element { return render_document(state, input); });
  root = ftxui::CatchEvent(root, [&state, &input, &screen](const ftxui::Event& event) -> bool {
    if (state.editing) {
      if (event == ftxui::Event::Return) {
        state.validated = state.source;
        state.title = "custom";
        state.analyze();
        state.editing = false;
        return true;
      }
      if (event == ftxui::Event::Escape) {
        state.source = state.validated;
        state.analyze();
        state.editing = false;
        return true;
      }
      return input->OnEvent(event);
    }

    if (event == ftxui::Event::Character('e') || event == ftxui::Event::Character('E') ||
        event == ftxui::Event::Character('/')) {
      state.editing = true;
      state.cursor = static_cast<int>(state.source.size());
      input->TakeFocus();
      return true;
    }
    if (event == ftxui::Event::Escape || event == ftxui::Event::Character('q') ||
        event == ftxui::Event::Character('Q')) {
      screen.Exit();
      return true;
    }
    if (event == ftxui::Event::Character('d') || event == ftxui::Event::Character('D')) {
      state.load_demo((state.demo_index + 1) % state.demos.size());
      return true;
    }
    return false;
  });

  screen.Loop(root);
}

}  // namespace

auto main(int argc, char** argv) -> int {
  const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
  const Options options = parse_args(args);
  if (options.help) {
    print_usage(std::cout);
    return 0;
  }
  if (options.error) {
    print_usage(std::cerr);
    return 2;
  }

  if (options.print) {
    State state;
    if (options.query.has_value()) {
      state.set_query(*options.query);
    }
    return print_analysis(state);
  }

  run(options.query);
  return 0;
}
