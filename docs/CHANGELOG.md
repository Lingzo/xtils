# CHANGELOG

All notable changes to xtils are documented here (reverse chronological order).
Format: `type(scope): description` — types: feat, fix, refactor, chore, tidy.

---

## Unreleased

### 2025-05 — Code Quality & Type Traits

- **fix(type_traits)**: add `type_name_cstr` for printf-safe usage
- **feat(bt)**: add structured `BtLogger` for offline and online analysis (CompositeLogger, FileLogger, InspectLogger)
- **fix**: resolve review findings from code quality overhaul
- **refactor**: comprehensive code quality overhaul — PascalCase API across all modules, `[[deprecated]]` wrappers for snake_case legacy APIs, test coverage expansion

### 2025-04 — Behavior Tree Events & Subtrees

- **feat(bt)**: load trees from JSON directories (`LoadTreesFromDirectory`)
- **feat(bt)**: add event-driven subtree controls (`SubTree`, `WaitForEvent`, `EventGuard` nodes)
- **feat(bt)**: add tree events system (`sendEvent`, `consumeEvent`, `peekEvent` on BtTree)
- **feat(bt)**: add pause/resume support for BtTree

### 2025-03 — API Naming & HTTP Improvements

- **refactor(app)**: rename `PostTask`/`PostAsyncTask` to `Spawn`/`SpawnAsync`
- **feat(http)**: update HttpClient API (unified sync/async interface)
- **fix(http)**: POST form/multipart with file handling
- **fix(http)**: chunked transfer encoding error
- **chore**: update inner TCP API for Transport abstraction

### 2025-02 — TLS & WebSocket

- **feat(net)**: support WSS (WebSocket Secure) client
- **feat(net)**: HttpClient supports TLS (via Transport layer)
- **tidy**: add backward-compatible API wrappers

### 2025-01 — Behavior Tree Foundation

- **feat(bt)**: add tree name in tree file format
- **feat(json)**: keep one-line for `dump(0)`
- **feat(bt)**: add `BtLogger` interface and implementations
- **feat(bt)**: update tree JSON format
- **feat(http)**: support `multipart/form-data` and large file uploads
- **fix(http)**: HTTP client redirect handling
- **feat(bt)**: update node interface (OnTick/OnStart/OnStop)
- **feat(bt)**: add common-use nodes (Sequence, Selector, Inverter, Delay, AlwaysSuccess, AlwaysFailure, SimpleAction)
- **feat(bt)**: add Blackboard (`AnyMap`)
- **feat(bt)**: use `AnyData` to replace `std::any`
- **feat(bt)**: add `dump()` / `dumpTree()` for tree visualization
- **feat(app)**: update service API
- **feat(bt)**: add input/output ports for nodes
- **feat(bt)**: initial behavior tree implementation with `BtFactory` JSON builder

### 2024 — Foundation

- **feat(tasks)**: add `EventManager` with typed/enum event dispatch
- **feat**: add `BUILD_WITH_SANITIZERS` CMake option (ASan + UBSan)
- **feat(json)**: custom JSON implementation (replaces nlohmann_json dependency)
- **tidy**: export `cxx_std_17` compile feature to consumers
- **feat(tasks)**: update `TaskGroup` API (`Sequential`/`Parallel` factory methods, `RunUntilCompleted`)
- **fix(json)**: `dump()` indent error
- **tidy**: update log formatter
- **feat(logging)**: flush log on exit
- **feat(logging)**: disable log file by default
- **feat(cmake)**: export autogen for downstream projects
- **feat**: disable build examples by default
- **feat(net)**: add WebSocket client
- **fix**: build error for CI
- **feat**: add README
- **tidy**: update Inspect web page
- **chore**: add GitHub Actions CI (ubuntu-latest, Release)
- **feat(tasks)**: add `CronScheduler` (interval + cron-expression tasks)
- **feat(app)**: update App API for better usability
- **feat(net)**: add HttpClient, TcpServer, TcpClient, UdpServer, UdpClient
- **feat(fsm)**: better debugging (history, Graphviz export)
- **feat(debug)**: Tracer uses `forward_list` to reduce memory usage

---

## Breaking Changes Summary

| When | What | Migration |
|------|------|-----------|
| 2025-05 | All public APIs renamed to PascalCase | Use new names; old snake_case still works but emits deprecation warnings |
| 2025-03 | `PostTask`/`PostAsyncTask` → `Spawn`/`SpawnAsync` on App | Use new names |
| 2025-01 | BT node interface changed to `OnTick`/`OnStart`/`OnStop` | Override new virtual methods |
| 2024 | Custom JSON replaces nlohmann_json | Use `xtils::Json` API (similar but not identical) |
