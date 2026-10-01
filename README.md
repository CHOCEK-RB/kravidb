# kravidb

## Requirements

- C++23 compiler (`clang++` 17+)
- [xmake](https://xmake.io) 2.8+
- `clang-format` 17+
- `clang-tidy` 17+

## Quick Start

```bash
# Build the project
xmake

# Run the database binary
xmake run kravidb
```

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

