// Harness de benchmarks (Google Benchmark): compara el Index Scan O(log n)
// contra el Full Table Scan O(n) sobre volumenes crecientes de filas.
//
// Dataset sintetico: N tuplas `(i, payload)` insertadas a traves de `Table`,
// de modo que se ejercita la ruta real (RecordStore + BTree). Se mide latencia
// por operacion y Page I/O logico:
//   - Index Scan (`Table::find_by_index`): lee una pagina de datos por acierto y
//     recorre O(log n) nodos del indice (residente en memoria).
//   - Full Scan (`Table::scan_all`): recorre todas las paginas del store.

#include <benchmark/benchmark.h>

import std;
import kravidb;

namespace {

using kravidb::index::Key;
using kravidb::storage::Field;
using kravidb::storage::StorageEngine;
using kravidb::storage::Tuple;

constexpr std::size_t min_rows = 1'000;
constexpr std::size_t max_rows = 1'000'000;
constexpr int range_multiplier = 10;
constexpr std::size_t payload_size = 8;
constexpr std::size_t query_count = 256;
constexpr std::uint64_t rng_seed = 20'261'001ULL;
constexpr double min_time_seconds = 0.1;
constexpr std::uint64_t decimal_base = 10;
constexpr int width_rows = 10;
constexpr int width_metric = 16;
constexpr int width_speedup = 12;

// Payload determinista de longitud fija (SSO, sin reservas extra).
[[nodiscard]] auto make_payload(std::size_t row) -> std::string {
  std::string text(payload_size, '0');
  auto value = static_cast<std::uint64_t>(row);
  for (std::size_t index = 0; index < text.size(); ++index) {
    text.at(text.size() - 1 - index) =
        static_cast<char>('0' + static_cast<int>(value % decimal_base));
    value /= decimal_base;
  }
  return text;
}

struct Dataset {
  StorageEngine engine;
  std::vector<Key> queries;
};

[[nodiscard]] auto build_dataset(std::size_t rows) -> std::unique_ptr<Dataset> {
  auto data = std::make_unique<Dataset>();
  std::vector<std::size_t> order(rows);
  std::ranges::iota(order, std::size_t{0});

  // NOLINTNEXTLINE(bugprone-random-generator-seed, cert-msc32-c, cert-msc51-cpp)
  std::mt19937_64 rng{rng_seed};
  std::ranges::shuffle(order, rng);

  for (const std::size_t row : order) {
    const auto key = static_cast<Key>(row);
    data->engine.table().insert(Tuple{std::vector<Field>{Field{key}, Field{make_payload(row)}}});
  }

  const std::size_t take = std::min(query_count, rows);
  data->queries.reserve(take);
  for (std::size_t index = 0; index < take; ++index) {
    data->queries.push_back(static_cast<Key>(order.at(index)));
  }
  return data;
}

// El dataset se construye una sola vez por tamano y se reutiliza entre
// benchmarks y repeticiones.
[[nodiscard]] auto dataset_for(std::size_t rows) -> Dataset& {
  static std::map<std::size_t, std::unique_ptr<Dataset>> cache;
  const auto found = cache.find(rows);
  if (found != cache.end()) {
    return *found->second;
  }
  const auto inserted = cache.emplace(rows, build_dataset(rows));
  return *inserted.first->second;
}

[[nodiscard]] auto per_iteration(std::size_t total, std::size_t iterations) -> double {
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  if (iterations == 0) {
    return 0.0;
  }
  return static_cast<double>(total) / static_cast<double>(iterations);
}

class ScanBenchmark : public benchmark::Fixture {
  public:
  void SetUp(const ::benchmark::State& state) override {
    rows_ = static_cast<std::size_t>(state.range(0));
    dataset_ = &dataset_for(rows_);
  }

  [[nodiscard]] auto dataset() const noexcept -> Dataset& { return *dataset_; }

  private:
  std::size_t rows_{0};
  Dataset* dataset_{nullptr};
};

// NOLINTBEGIN(cert-err58-cpp, bugprone-throwing-static-initialization,
// cppcoreguidelines-owning-memory)
BENCHMARK_DEFINE_F(ScanBenchmark, IndexScan)(benchmark::State& state) {
  state.SetComplexityN(state.range(0));
  auto& engine = dataset().engine;
  const auto& queries = dataset().queries;
  std::size_t hits = 0;
  std::size_t cursor = 0;
  engine.records().reset_stats();
  engine.index().reset_search_stats();

  for (auto _ : state) {
    const auto key = queries.at(cursor % queries.size());
    const auto found = engine.table().find_by_index(key);
    if (found.has_value()) {
      ++hits;
      benchmark::DoNotOptimize(found.value().fields().size());
    }
    ++cursor;
  }

  const auto page_reads = engine.records().stats().page_reads;
  const auto search_stats = engine.index().last_search_stats();
  const auto iterations = static_cast<std::size_t>(state.iterations());

  state.counters["hits"] = static_cast<double>(hits);
  state.counters["pages/op"] = per_iteration(page_reads, iterations);
  state.counters["index_nodes/op"] = static_cast<double>(search_stats.node_accesses);
  state.counters["key_cmps/op"] = static_cast<double>(search_stats.key_comparisons);
}

BENCHMARK_DEFINE_F(ScanBenchmark, FullScan)(benchmark::State& state) {
  state.SetComplexityN(state.range(0));
  auto& engine = dataset().engine;
  std::size_t scanned = 0;

  for (auto _ : state) {
    const auto rows = engine.table().scan_all();
    scanned = rows.size();
    benchmark::DoNotOptimize(rows.data());
  }

  // `scan_all` usa `slot_at`, que no toca `PageIOStats`; el I/O logico del full
  // scan es, por construccion, el numero de paginas recorridas.
  const auto pages = engine.records().page_count();
  state.counters["hits"] = static_cast<double>(scanned);
  state.counters["pages/op"] = static_cast<double>(pages);
  state.counters["index_nodes/op"] = 0.0;
  state.counters["key_cmps/op"] = 0.0;
}

BENCHMARK_REGISTER_F(ScanBenchmark, IndexScan)
    ->RangeMultiplier(range_multiplier)
    ->Range(static_cast<std::int64_t>(min_rows), static_cast<std::int64_t>(max_rows))
    ->Unit(benchmark::kNanosecond)
    ->Complexity(benchmark::oLogN)
    ->MinTime(min_time_seconds);

BENCHMARK_REGISTER_F(ScanBenchmark, FullScan)
    ->RangeMultiplier(range_multiplier)
    ->Range(static_cast<std::int64_t>(min_rows), static_cast<std::int64_t>(max_rows))
    ->Unit(benchmark::kNanosecond)
    ->Complexity(benchmark::oN)
    ->MinTime(min_time_seconds);
// NOLINTEND(cert-err58-cpp, bugprone-throwing-static-initialization,
// cppcoreguidelines-owning-memory)

using Run = benchmark::BenchmarkReporter::Run;

// Recolecta los resultados (para el reporte comparativo) y delega la tabla
// estandar de Google Benchmark en el reporter por defecto.
class ComparisonReporter final : public benchmark::BenchmarkReporter {
  public:
  ComparisonReporter() : display_{benchmark::CreateDefaultDisplayReporter()} {}
  ComparisonReporter(const ComparisonReporter&) = delete;
  auto operator=(const ComparisonReporter&) -> ComparisonReporter& = delete;
  ComparisonReporter(ComparisonReporter&&) = delete;
  auto operator=(ComparisonReporter&&) -> ComparisonReporter& = delete;
  ~ComparisonReporter() override = default;

  auto ReportContext(const Context& context) -> bool override {
    if (display_ == nullptr) {
      return true;
    }
    return display_->ReportContext(context);
  }

  auto ReportRuns(const std::vector<Run>& reports) -> void override {
    if (display_ != nullptr) {
      display_->ReportRuns(reports);
    }
    for (const auto& run : reports) {
      if (run.run_type == Run::RT_Iteration) {
        runs_.push_back(run);
      }
    }
  }

  void Finalize() override {
    if (display_ != nullptr) {
      display_->Finalize();
    }
  }

  [[nodiscard]] auto runs() const noexcept -> const std::vector<Run>& { return runs_; }

  private:
  std::unique_ptr<benchmark::BenchmarkReporter> display_;
  std::vector<Run> runs_;
};

struct ComparisonRow {
  std::size_t rows{0};
  double index_ns{0.0};
  double full_ns{0.0};
  double index_pages{0.0};
  double full_pages{0.0};
  double index_nodes{0.0};
};

[[nodiscard]] auto counter_of(const Run& run, std::string_view name) -> double {
  const auto found = run.counters.find(std::string{name});
  if (found == run.counters.end()) {
    return 0.0;
  }
  return found->second.value;
}

[[nodiscard]] auto rows_of(const Run& run) -> std::size_t {
  constexpr char lowest_digit = '0';
  constexpr char highest_digit = '9';
  std::size_t value = 0;
  for (const char digit : run.run_name.args) {
    if (digit < lowest_digit || digit > highest_digit) {
      return 0;
    }
    value = (value * static_cast<std::size_t>(decimal_base)) +
            static_cast<std::size_t>(digit - lowest_digit);
  }
  return value;
}

void accumulate(std::map<std::size_t, ComparisonRow>& rows, const Run& run) {
  const auto count = rows_of(run);
  if (count == 0) {
    return;
  }
  ComparisonRow& row = rows.try_emplace(count).first->second;
  row.rows = count;
  if (run.run_name.function_name == "ScanBenchmark/IndexScan") {
    row.index_ns = run.GetAdjustedRealTime();
    row.index_pages = counter_of(run, "pages/op");
    row.index_nodes = counter_of(run, "index_nodes/op");
  } else if (run.run_name.function_name == "ScanBenchmark/FullScan") {
    row.full_ns = run.GetAdjustedRealTime();
    row.full_pages = counter_of(run, "pages/op");
  }
}

[[nodiscard]] auto safe_ratio(double numerator, double denominator) -> double {
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  if (denominator <= 0.0) {
    return 0.0;
  }
  return numerator / denominator;
}

void print_comparison(const std::vector<Run>& runs) {
  std::map<std::size_t, ComparisonRow> rows;
  for (const auto& run : runs) {
    accumulate(rows, run);
  }
  if (rows.empty()) {
    return;
  }

  std::cout << '\n' << "============ Index Scan vs Full Table Scan ============" << '\n';
  std::cout << std::setw(width_rows) << "N" << std::setw(width_metric) << "index ns/op"
            << std::setw(width_metric) << "full ns/op" << std::setw(width_speedup) << "speedup"
            << std::setw(width_metric) << "index pages/op" << std::setw(width_metric)
            << "full pages/op" << std::setw(width_metric) << "index nodes/op" << '\n';
  for (const auto& entry : rows) {
    const ComparisonRow& row = entry.second;
    std::cout << std::setw(width_rows) << row.rows << std::setw(width_metric) << row.index_ns
              << std::setw(width_metric) << row.full_ns << std::setw(width_speedup)
              << safe_ratio(row.full_ns, row.index_ns) << std::setw(width_metric) << row.index_pages
              << std::setw(width_metric) << row.full_pages << std::setw(width_metric)
              << row.index_nodes << '\n';
  }

  if (rows.size() >= 2) {
    const ComparisonRow& first = rows.begin()->second;
    const ComparisonRow& last = rows.rbegin()->second;
    std::cout << '\n' << "Conclusion:" << '\n';
    std::cout << "  N crece "
              << safe_ratio(static_cast<double>(last.rows), static_cast<double>(first.rows))
              << "x (de " << first.rows << " a " << last.rows << ")." << '\n';
    std::cout << "  Index Scan: pages/op " << first.index_pages << " -> " << last.index_pages
              << " (constante) y nodos/op " << first.index_nodes << " -> " << last.index_nodes
              << " (crecimiento ~log)." << '\n';
    std::cout << "  Full Scan:  pages/op " << first.full_pages << " -> " << last.full_pages
              << " (crecimiento ~lineal)." << '\n';
    std::cout << "  Speedup del Index Scan en N=" << last.rows << ": "
              << safe_ratio(last.full_ns, last.index_ns) << "x." << '\n';
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }

  ComparisonReporter reporter;
  benchmark::RunSpecifiedBenchmarks(&reporter);
  benchmark::Shutdown();

  print_comparison(reporter.runs());
  return 0;
}
