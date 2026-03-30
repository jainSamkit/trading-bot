# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Standard build
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DPORTABLE_RELEASE=ON ..
cmake --build . -j$(nproc)

# Debug build (AddressSanitizer + UBSan)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# ThreadSanitizer build
cmake -DCMAKE_BUILD_TYPE=ThreadSanitizer ..

# Build and run tests
cmake -DBUILD_TESTS=ON ..
cmake --build . -j$(nproc)
ctest --output-on-failure

# Run a single test binary
./build/test_ws_parser
./build/test_session
```

**Run:**
```bash
./build/trading_bot [host] [port] [path] [channel] [symbol]
# Production:
./build/trading_bot socket.india.delta.exchange 443 / l2_updates BTCUSD
# Testnet:
./build/trading_bot socket-ind.testnet.deltaex.org 443 / l2_updates BTCUSD
```

**Docker dev environment:**
```bash
docker compose up -d --build
docker compose exec -it trading-bot bash
```

## Architecture

A low-latency C++ WebSocket client for Delta Exchange (crypto derivatives). CRTP is used throughout for compile-time polymorphism with no virtual dispatch on hot paths.

### Threading Model

There are two threads that must share orderbook data:

1. **Feed thread** — runs the epoll reactor (`run_loop`), receives WebSocket messages, and parses `L2Update`s via `L2UpdateSession::onMessage()`.
2. **OrderBook thread** — owns `OrderBook<Depth>` instances and applies updates to the tick-space book.

These threads communicate via **POSIX shared memory (`shm_open`)** — the feed thread writes parsed `L2Update` structs into the shared region and the orderbook thread reads from it.

### Transport Layer (`src/transport/`)

- **wsclient.hpp** — Core. TCP (getaddrinfo/socket), OpenSSL TLS (SNI), WebSocket HTTP upgrade, RFC 6455 frame encode/decode (masking, fragmentation, extended lengths), and the `run_loop()` epoll reactor. Uses `timerfd` for heartbeat timeouts, `eventfd` for shutdown signaling, and xoshiro256** PRNG (seeded from `/dev/urandom`) for WS masking keys.
- **session.hpp** — CRTP session base. Manages connection lifecycle (init → subscribe → message loop), reconnection with exponential backoff (1s base, max 10 retries). Derived class hooks: `onMessage()`, `onSubscribe()`.
- **types.hpp** — `SessionStatus`, `SessionID`, `Kind`, `SessionCtx`, `EpollSlot`.

### Delta Exchange Layer (`src/delta_exchange/`)

- **client.hpp** — `DeltaWebsocketClient`: owns the three sessions, epoll fd, and eventfd. Routes epoll callbacks to the correct session. Heartbeat timeout: 35s.
- **sessions/l2.hpp** — The only fully implemented session. Parses `l2_updates`/`l2_orderbook` JSON via simdjson, validates sequence continuity (gaps invalidate the book and trigger reconnect), converts prices to tick space via `Product::inv_tick_size`, then writes the `L2Update` into shared memory for the orderbook thread.
- **sessions/ticker.hpp**, **sessions/mark.hpp** — Stubs; no message handlers yet.
- **models/product.hpp** — `Product` (tick_size, inv_tick_size, contract_value, etc.) and `ProductTable` (up to 64 instruments, lookup by symbol or exchange_id).

### OrderBook (`src/orderbook/`)

- **orderbook.hpp / orderbook_impl.hpp** — `OrderBook<Depth>` template. Dense integer tick space; no floating-point on the hot path. Maintains top-of-book `TOBEntry` ladder (configurable depth). Methods: `onSnapshot()`, `onUpdate()`, `mid()`, `spread()`, `bestBidPrice()`, `bestAskPrice()`. Reads `L2Update`s from shared memory written by the feed thread.

### Feed / Parser (`src/feed/`, `src/parser/`)

- **feed_types.hpp** — `Action` (Snapshot/Update), `BookLevel`, `FeedMessage`.
- **delta_parser.hpp / .cpp** — `FeedParser` stub; all methods return false/default.

## Key Design Patterns

- **CRTP throughout** — `WebSocketClient<Derived>`, `Session<DerivedSession, ClientDerived>`, `OrderBook<Depth>`.
- **Linux-specific** — `epoll`, `timerfd`, `eventfd`. Not portable to macOS/Windows.
- **Single-threaded reactor** — All I/O multiplexed in one epoll loop; no locks needed in the feed path.
- **simdjson ondemand** — Zero-copy JSON traversal in `L2UpdateSession`.
- **Tick-space arithmetic** — Prices are integers (tick index) inside the orderbook; conversion happens once at parse time using `Product::inv_tick_size`.

## Dependencies

- **OpenSSL** — TLS (system)
- **Boost** — headers only (system)
- **simdjson** 3.12.2 — JSON (FetchContent, auto-downloaded)
- **GoogleTest** 1.14.0 — tests only, `-DBUILD_TESTS=ON` (FetchContent)
- **liburing** — optional async I/O

## Known TODOs / Incomplete Areas

- `SSL_VERIFY_NONE` is set — certificate verification is disabled
- `FeedParser` is entirely stubbed out
- `TickerSession` and `MarkSession` have no message handlers
- Checksum validation in L2 updates is not implemented
- Shared memory region between feed thread and orderbook thread is not yet implemented
