## Project

xtils — C++17 static utility library providing app framework, config, FSM, logging, networking, system abstractions, async tasks, and general utilities.

## Tech Stack

- C++17, CMake ≥ 3.10
- SSL/TLS: OpenSSL (default) or mbedTLS
- Test framework: doctest (vendored in `tests/doctest/`)
- Formatter: clang-format (Google-based style)
- CI: GitHub Actions (ubuntu-latest, Release build)

## Directory Structure

```
include/xtils/   — public headers (mirrors src/ layout)
src/             — implementation: app/ config/ debug/ fsm/ logging/ net/ system/ tasks/ utils/
tests/           — unit tests (*_test.cc), uses doctest
examples/        — usage examples
cmake/           — CMake helpers (autogen, config template)
docs/            — AI-friendly documentation (architecture, API reference, CHANGELOG)
```

## Documentation

See `docs/` for detailed AI-friendly references:
- `docs/README.md` — overview & navigation
- `docs/architecture.md` — project structure, build system, module dependencies
- `docs/api-reference.md` — complete public API by module
- `docs/CHANGELOG.md` — version history and breaking changes

## Key Commands

```bash
# Configure (Debug with tests & examples)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Format code
clang-format -i src/**/*.cc include/**/*.h
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | OFF | Build unit tests |
| `BUILD_EXAMPLES` | OFF | Build examples |
| `BUILD_WITH_SANITIZERS` | OFF | Enable ASan + UBSan |
| `USE_OPENSSL` | ON | Use OpenSSL for TLS |
| `USE_MBEDTLS` | OFF | Use mbedTLS for TLS |
| `INSPECT_DISABLE` | OFF | Disable inspect module |

## Code Conventions

- Follow Google C++ style (enforced by `.clang-format`)
- 2-space indentation, 80-column limit
- Pointer/reference aligned left (`int* p`)
- Use `cxx_std_17` features; no raw `new`/`delete` unless necessary
- Headers use `#pragma once` or include guards matching path
- Source files: `.cc`; headers: `.h`
- Test files named `<module>_test.cc`, one test file per module

## Workflow

- Default branch: `master`
- CI builds Release with tests and examples on push/PR to master
- Keep `build/`, `.cache/`, `log/` out of version control
