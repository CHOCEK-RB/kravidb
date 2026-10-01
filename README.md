# kravidb

## Requirements

- C++23 compiler (`clang++` 17+)
- [xmake](https://xmake.io) 2.8+
- `clang-format` 17+
- `clang-tidy` 17+
- [bun](https://bun.sh) 1.0+ (web visualizer only)

## Quick Start

```bash
# Build the project
xmake

# Run the database binary
xmake run kravidb
```

## HTTP API

`kravidb_api` exposes the storage engine over HTTP with
[cpp-httplib](https://github.com/yhirose/cpp-httplib), so the web visualizer can
drive a real engine instead of its bundled mock.

```bash
# Build and run the API (use release: sanitizers distort the latency metrics)
xmake f -m release
xmake build kravidb_api
xmake run kravidb_api          # listens on http://0.0.0.0:8080
xmake run kravidb_api 9000     # optional custom port
```

| Method | Path | Description |
| --- | --- | --- |
| `GET` | `/api/v1/health` | liveness probe |
| `GET` | `/api/v1/btree` | full tree, node by node |
| `POST` | `/api/v1/btree/insert` | body `{"key": 42, "payload": "..."}` |
| `GET` | `/api/v1/btree/search?key=42` | index-scan trace and metrics |
| `GET` | `/api/v1/page/:id` | slotted page dump (`id` is 1-based) |
| `POST` | `/api/v1/reset` | rebuild the engine and re-seed it |

The JSON payloads match the contract in `web/src/lib/api/types.ts`.
`full_scan.latency_ns` follows the frontend convention and is expressed in
microseconds. When `web/dist` exists the server also mounts it as static
content, so the UI and the API share a single origin (no CORS setup, no mixed
content).

## Web visualizer

The Svelte frontend lives in `web/`. It talks to the HTTP API when built with
`VITE_USE_MOCK=false`, and to an in-browser mock otherwise. Requires
[bun](https://bun.sh).

```bash
cd web
bun install
bun run dev                        # dev server against the mock
VITE_USE_MOCK=false bun run build  # production build wired to the API
```

The resulting `web/dist` is served by `kravidb_api` at `/`.

## Code Quality & Pre-Commit Checks

### Automatic Check (Git Hook)

Enable the local hook once to reject offending commits automatically:

```bash
git config core.hooksPath .githooks
```

### Manual Checks

Single command that runs the same gates as the hook and CI (format + build + clang-tidy):

```bash
xmake lint
```

To analyze only specific files:

```bash
KRAVIDB_LINT_FILES="src/index/btree.cppm" xmake lint
```

Equivalent raw commands:

```bash
# 1. Format all source files automatically
find src tests \( -name "*.cpp" -o -name "*.cppm" \) | xargs clang-format -i

# 2. Verify formatting (CI check)
find src tests \( -name "*.cpp" -o -name "*.cppm" \) | xargs clang-format --dry-run --Werror

# 3. Clean build and run with AddressSanitizer/UBSan
xmake f -c -m debug
xmake
xmake run kravidb

# 4. Run clang-tidy static analysis
find src tests \( -name "*.cpp" -o -name "*.cppm" \) | xargs clang-tidy -p .
```

## Documentation

The public API of the modules is documented with Doxygen comments (Spanish)
and published as Markdown in `docs/md`. Regenerate it with:

```bash
xmake docs
```

This runs `doxygen Doxyfile` (XML only, into `docs/xml`) and then
`scripts/doxygen_to_md.py`, which converts the XML into one Markdown file per
module, namespace, class and struct, plus the index `docs/md/index.md`. Only
the `.cppm` module interfaces are documented; the implementation units and the
tests are out of scope.

Raw commands (equivalent to the task):

```bash
doxygen Doxyfile
python3 scripts/doxygen_to_md.py
```

