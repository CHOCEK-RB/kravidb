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

