# Architecture

## Directory Layout

```
xtils/
├── include/xtils/          # Public headers
│   ├── app/                # App framework (app.h, service.h, auto-gen.h)
│   ├── config/             # Configuration (config.h)
│   ├── debug/              # Debug tools (inspect.h, tracer.h)
│   ├── fsm/                # State machines (fsm.h, behavior_tree.h, bt_*logger.h)
│   ├── logging/            # Logging (logger.h, sink.h, watchdog.h)
│   ├── net/                # Networking
│   │   ├── transport/      # Transport layer (transport.h, tls_transport.h, plain_tcp_transport.h)
│   │   ├── http_client.h   # HTTP client (sync & async)
│   │   ├── http_server.h   # HTTP server (low-level, connection-oriented)
│   │   ├── http_router.h   # HTTP router (Express-style routing, middleware, static files)
│   │   ├── http_common.h   # HTTP types (method, url, headers, status codes)
│   │   ├── tcp_client.h / tcp_server.h
│   │   ├── udp_client.h / udp_server.h
│   │   ├── websocket_client.h
│   │   └── websocket_common.h
│   ├── system/             # OS abstractions (event_fd, paged_memory, platform, signal_handler, unix_socket)
│   ├── tasks/              # Async & scheduling
│   │   ├── task_runner.h          # Abstract TaskRunner interface
│   │   ├── unix_task_runner.h     # epoll/poll-based event loop
│   │   ├── thread_task_runner.h   # TaskRunner on a dedicated thread
│   │   ├── task_group.h           # Sequential/parallel task groups
│   │   ├── timer.h                # Steady & system clock timers
│   │   ├── cron_scheduler.h       # Cron-style job scheduler
│   │   └── event.h                # Typed event manager
│   └── utils/              # General utilities
│       ├── json.h           # Custom JSON implementation
│       ├── string_utils.h   # String operations
│       ├── file_utils.h     # File I/O & path operations
│       ├── base64.h / sha1.h
│       ├── byte_reader.h / byte_writer.h
│       ├── thread_safe.h    # Thread-safe queue
│       ├── weak_ptr.h       # Single-threaded WeakPtr
│       ├── scoped.h         # RAII wrappers (ScopedFile, ScopedDir, Scoped)
│       ├── time_utils.h     # Time utilities (steady/system clock)
│       ├── type_traits.h    # Compile-time type name
│       ├── endianness.h     # Byte order conversion
│       ├── exception.h      # Exception utilities
│       └── string_view.h    # string_view helpers
├── src/                    # Implementation (.cc files, mirrors include layout)
├── tests/                  # Unit tests (*_test.cc, uses doctest)
├── examples/               # Usage examples
├── cmake/                  # CMake helpers (autogen, config template)
├── CMakeLists.txt          # Root build file
└── docs/                   # This documentation
```

## Module Dependency Graph

```
utils (json, string, file, time, thread_safe, weak_ptr, scoped, ...)
  ↑
system (event_fd, paged_memory, signal_handler, unix_socket, platform)
  ↑
tasks (task_runner, unix_task_runner, thread_task_runner, task_group, timer, event, cron_scheduler)
  ↑
config (config.h — depends on json)
  ↑
logging (logger, sink, watchdog)
  ↑
net (tcp, udp, http, websocket — depends on tasks, system, utils)
  ↑
fsm (fsm, behavior_tree — depends on json, type_traits)
  ↑
debug (inspect — depends on net, tasks; tracer — standalone)
  ↑
app (app, service — orchestrates all modules)
```

## Build System

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | OFF | Build unit tests |
| `BUILD_EXAMPLES` | OFF | Build examples |
| `BUILD_WITH_SANITIZERS` | OFF | Enable ASan + UBSan |
| `USE_OPENSSL` | ON | Use OpenSSL for TLS |
| `USE_MBEDTLS` | OFF | Use mbedTLS for TLS |
| `INSPECT_DISABLE` | OFF | Disable inspect module (strips all INSPECT_* macros) |

### Build Commands

```bash
# Debug with tests & examples
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linking

```cmake
find_package(xtils REQUIRED)
target_link_libraries(myapp xtils::xtils)
```

The library exports `cxx_std_17` as a public compile feature — consumers automatically get C++17.

## TLS Backend

The library requires exactly one TLS backend:

- **OpenSSL** (default, `USE_OPENSSL=ON`): links `OpenSSL::SSL`
- **mbedTLS** (`USE_MBEDTLS=ON`): links `MbedTLS::mbedtls`, `MbedTLS::mbedx509`, `MbedTLS::mbedcrypto`

Compile definition `USE_OPENSSL` or `USE_MBEDTLS` is propagated to consumers.

## Code Conventions

- Google C++ style (`.clang-format`)
- 2-space indent, 80-column limit
- `#pragma once` for header guards
- PascalCase for public API methods
- `[[deprecated("Use Xxx() instead")]]` on legacy snake_case wrappers
