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

All pull requests are evaluated by GitHub Actions (`.github/workflows/ci.yml`). Commits must pass formatting and compilation gates before merging.

### Automatic Check (Git Hook)

Enable the local hook once to reject offending commits automatically:

```bash
git config core.hooksPath .githooks
```

### Manual Checks

Execute the exact commands run by the CI pipeline:

```bash
# 1. Format all source files automatically
find src tests -name "*.cpp" -o -name "*.cppm" | xargs clang-format -i

# 2. Verify formatting (CI check)
find src tests -name "*.cpp" -o -name "*.cppm" | xargs clang-format --dry-run --Werror

# 3. Clean build and run with AddressSanitizer/UBSan
xmake f -c -m debug
xmake
xmake run kravidb

# 4. Run clang-tidy static analysis
find src -name "*.cpp" -o -name "*.cppm" | xargs clang-tidy -p .
```

