# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Always build inside the build/ directory — never run cmake from the project root.
mkdir -p build && cd build

# Release build
cmake -DCMAKE_BUILD_TYPE=Release -DPORTABLE_RELEASE=ON ..
cmake --build . -j$(nproc)

# Debug build (AddressSanitizer + UBSan)
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(nproc)

# ThreadSanitizer build
cmake -DCMAKE_BUILD_TYPE=ThreadSanitizer ..
cmake --build . -j$(nproc)

# OMS end-to-end test (requires Delta testnet credentials in tests/main_oms_test.cpp)
cmake --build . --target oms_test
./build/oms_test

# Unit tests (GTest)
cmake -DBUILD_TESTS=ON ..
cmake --build . -j$(nproc)
ctest --output-on-failure
```

**Run trading bot:**
```bash
./build/trading_bot
# Testnet WS: socket-ind.testnet.deltaex.org  REST: cdn-ind.testnet.deltaex.org
# Production WS: socket.india.delta.exchange  REST: api.india.delta.exchange
```

**Docker dev environment:**
```bash
docker compose up -d --build
docker compose exec -it trading-bot bash
```

## Architecture

A low-latency C++ trading bot for Delta Exchange (crypto derivatives perpetuals). Two independent child processes (spawned via `fork()` from `src/main.cpp`): a **feed process** for market data and an **OMS process** for order management. CRTP is used throughout for compile-time polymorphism with zero virtual dispatch on hot paths.

---

### Process Model

`src/main.cpp` forks two child processes and waits on them:

1. **FeedProcess** (`src/processes/feed.hpp`) — public market data
   - Thread 1: epoll reactor running `DeltaWebsocketClient::start()` — receives L2, mark, OHLC, spot messages and pushes `FeedMessage` events into a `SpscRing<FeedMessage, 4096>`
   - Thread 2: `MarketState::run()` — drains the feed ring, maintains 10-level orderbooks, OHLC rings, mark/spot price snapshots, writes into shared memory

2. **OmsProcess** (`src/processes/oms.hpp`) — private order and position management
   - Thread 1: epoll reactor running `DeltaOMSWebsocketClient::start()` — receives order/position/fill/wallet events, pushes `OMSEvent`s into `oms_ws_ring`
   - Thread 2: `OrderStateManager::run()` — drains three rings (ws, rest, reconcile), maintains full order/position/wallet state

The two processes do not communicate with each other. FeedProcess writes to shared memory; OmsProcess ignores it. Strategy process (`src/processes/strategy.hpp`) is currently a stub.

---

### Transport Layer (`src/transport/`)

- **wsclient.hpp** — CRTP base `WebSocketClient<Derived>`. TCP connect (getaddrinfo), OpenSSL TLS with SNI, WebSocket HTTP upgrade, RFC 6455 frame encode/decode (masking, fragmentation, extended lengths). epoll reactor with `timerfd` (heartbeat) and `eventfd` (shutdown). xoshiro256** PRNG seeded from `/dev/urandom` for WS masking keys.
- **session.hpp** — CRTP session base `Session<DerivedSession, ClientDerived>`. Manages connection lifecycle (init → auth → subscribe → message loop), reconnection with exponential backoff (1 s base, max 10 retries). Derived class hooks: `onMessage()`, `onSubscribe()`.
- **http_client.hpp / .cpp** — `TcpClient` wrapping cpp-httplib. HTTPS GET/POST/PUT/DELETE with HMAC-SHA256 request signing. Base class for `DeltaRestClient`.
- **types.hpp** — `SessionStatus`, `SessionID`, `SessionCtx`, `EpollSlot`.

---

### Market Data Layer

**WebSocket client:** `src/delta_exchange/ws_client.hpp` — `DeltaWebsocketClient` owns four sessions, epoll fd, and eventfd. Routes epoll callbacks to the correct session. Heartbeat timeout: 35 s.

**Feed sessions** (`src/delta_exchange/sessions/`):

| File | Channel | Status |
|------|---------|--------|
| `l2.hpp` | `l2_updates` / `l2_orderbook` | Full — sequence validation, tick-space conversion, snapshot/update |
| `mark.hpp` | `mark_price` | Full — parses mark price, pushes `MarkPriceData` |
| `ohlc.hpp` | `candlestick_*` | Full — 12 resolutions, mark vs trade OHLC |
| `spot.hpp` | `spot_price` | Full — spot index via product's `index_symbol` lookup |
| `ticker.hpp` | `ticker` | Stub |

**Types** (`src/delta_exchange/sessions/types.hpp`): `L2Update` (32 bid/ask levels, sequence, snapshot flag), `MarkPriceData`, `SpotPriceData`, `OHLCData`, `FeedMessage` union with kernel/frame/parse timestamps.

---

### Market State (`src/market_state/`)

- **market_state.hpp / .cpp** — `MarketState` drains `SpscRing<FeedMessage, 4096>`, maintains `OrderBook<10>` per instrument, per-resolution `OHLCRing`, mark/spot price arrays. Writes snapshots to shared memory via `SeqLock`.
- **ohlc_ring.hpp** — Ring buffer of OHLC candles per resolution.
- **latency_stats.hpp / .cpp** — Tracks kernel→frame→parse latency percentiles.

---

### OrderBook (`src/core/orderbook/`)

- **orderbook.hpp / orderbook_impl.hpp** — `OrderBook<Depth>` template. Dense integer tick space — no floating-point on the hot path. Prices stored as tick indices (`price * inv_tick_size`). Methods: `onSnapshot()`, `onUpdate()`, `mid()`, `spread()`, `bestBidPrice()`, `bestAskPrice()`. Depth configurable at compile time (default 10 levels).

---

### OMS Layer

**WebSocket client:** `src/delta_exchange/oms_ws_client.hpp` — `DeltaOMSWebsocketClient` owns three private sessions (orders, positions+fills, wallet), one shared `SpscRing<OMSEvent, 256>*`. All sessions push to the same ring.

**OMS sessions** (`src/delta_exchange/sessions/`):

| File | Channel | Notes |
|------|---------|-------|
| `order.hpp` | `orders` | Snapshot + live CRUD. Sequence gap → `OrdersInvalid` + reconnect |
| `position.hpp` | `positions` + `v2/user_trades` | Positions + fills. Fill seq gap → `PositionsInvalid` + reconnect |
| `wallet.hpp` | `margins` | Wallet balance + margin updates |

**State manager:** `src/oms/oms_manager.hpp` / `oms_manager.cpp`

`OrderStateManager::run()` is a tight busy-spin loop draining three rings:
- `oms_ws_ring` — live WS events (orders, positions, fills, wallet, signals)
- `oms_rest_ring` — REST response echoes (create/edit/cancel confirmations)
- `oms_reconcile_ring` — periodic REST reconcile snapshots

Events dispatch through a compile-time handler table `kHandlers[]` indexed by `OMSEventType`.

**Channel state machine** (per channel — orders, positions, wallet):
```
Invalid → (first snapshot entry arrives) → Rebuilding → (SnapshotComplete signal) → Valid
       ← (gap detected / reconnect)                                                ←
```

**`OMSState`** holds:
- `MemoryPool<Order, 64>` bid/ask/stop arrays per instrument (heap-allocated — ~2 MB total)
- `positions_[MAX_INSTRUMENTS]` array
- `exchange_id_order_map` / `client_id_order_map` (unordered_map)
- `exchange_id_stop_order_map` / `client_id_stop_order_map`
- Atomic `ChannelState` for orders, positions, wallet

**Audit log:** `src/oms/oms_audit.hpp` — printf-style formatted writes to `oms_audit.log`, no heap allocation.

> **Important:** `OrderStateManager` is ~2 MB. Always heap-allocate it (and the rings) with `std::make_unique` — never put them on the stack.

---

### REST Client (`src/delta_exchange/rest_client.hpp / .cpp`)

`DeltaRestClient` extends `TcpClient`. All REST methods sign requests with HMAC-SHA256.

**Execution methods** (push response into provided `SpscRing<OMSEvent>*`):
- `create_order`, `edit_order`, `cancel_order` — single order CRUD
- `cancel_all_orders` — DELETE /v2/orders/all (no order data in response)
- `close_all_positions` — closes all positions at market

**Reconciliation methods** (push into reconcile ring):
- `reconcile_open_orders` — paginated GET /v2/orders, all open+pending states
- `reconcile_open_positions` — GET /v2/positions
- `reconcile_wallet` — GET /v2/wallet/balances

**API serialization** (`src/delta_exchange/api/`): each endpoint has a `Request` struct with `serialize()` (builds JSON body) and `deserialize_page()` / `parse_success()` (parses response into `OMSEvent` structs). Uses simdjson ondemand — no intermediate allocations.

---

### Execution Layer (`src/execution/`)

- **execution_manager.hpp** — `ExecutionIntent` union (`CreateOrderIntent`, `EditOrderIntent`, `CancelOrderIntent`, `CancelAllOrderIntent`, `CloseAllPositionsIntent`). Currently REST methods are called directly; a `ConnectionPool<RestClient>` for multi-threaded execution is planned but not yet implemented.

---

### Domain Models (`src/delta_exchange/models/`)

All models are trivially copyable — no heap strings, no virtual methods. Char arrays for symbols/fill IDs. Safe to copy into SPSC ring slots.

- `product.hpp` — `Product` (tick_size, inv_tick_size, contract_value, exchange_id, index_symbol), `ProductTable` (max 64 instruments, lookup by symbol or exchange_id)
- `order.hpp` — `Order` (all fields for open/pending orders including stop fields)
- `position.hpp` — `Position` (size, entry_price, margin, PnL, margin_mode)
- `fill.hpp` — `Fill` (price, size, side, role, reason, seq_no)
- `wallet.hpp` — `Wallet` (balance, available_balance, margins, commissions)

---

### Core Utilities (`src/core/`)

- **spsc_ring.hpp** — `SpscRing<T, N>` (N must be power of 2). Head/tail on separate 64-byte cache lines. `push_begin()` / `push_commit()` (producer), `pop_begin()` / `pop_commit()` (consumer). Acquire/release on `head_`. Busy-spins if full.
- **memory_pool.hpp** — `MemoryPool<T, Cap>` fixed-size slab allocator. `alloc()` returns `T*` from pool; `free()` returns slot. Used for orders (bid/ask/stop pools per instrument).
- **seq_lock.hpp** — `SeqLock<T>` for lock-free snapshot reads (writer increments version, reader retries on odd version).
- **logger.hpp** — `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_DEBUG` macros with timestamps and component tags.

---

### IPC / Shared Memory (`src/ipc/`)

- **shm.hpp** — RAII `ShmOwner` / `ShmAccessor` wrapping `shm_open` / `mmap`.
- **shared_state.hpp** — `SharedState` layout: `MarketSnapshot`, `MarkSnapshot`, `SpotSnapshot`, `VolSnapshot` — written by FeedProcess, intended for (future) StrategyProcess reads.
- **snapshots.hpp** — Snapshot struct definitions.

---

## Key Design Patterns

- **CRTP throughout** — `WebSocketClient<Derived>`, `Session<DerivedSession, ClientDerived>`, `OrderBook<Depth>`. Zero virtual dispatch on hot paths.
- **Linux-specific** — `epoll`, `timerfd`, `eventfd`, `shm_open`. Not portable to macOS/Windows.
- **Single-threaded reactor** — All I/O multiplexed in one epoll loop; no locks in feed path.
- **Lock-free event passing** — `SpscRing` with acquire/release memory ordering; trivially-copyable event types.
- **simdjson ondemand** — Zero-copy JSON traversal; no intermediate string allocation in parse paths.
- **Tick-space arithmetic** — Prices stored as integers (tick index) in OrderBook; conversion happens once at parse time via `Product::inv_tick_size`.
- **Heap-allocate large objects** — `OrderStateManager` (~2 MB) and rings must use `std::make_unique` to avoid stack overflow.

---

## Dependencies

| Library | Version | How |
|---------|---------|-----|
| OpenSSL | system | TLS + HMAC-SHA256 signing |
| Boost | system (headers only) | Utility headers |
| simdjson | 3.12.2 | JSON parsing (FetchContent) |
| cpp-httplib | 0.18.1 | HTTPS REST client (FetchContent) |
| GoogleTest | 1.14.0 | Unit tests, `-DBUILD_TESTS=ON` (FetchContent) |

---

## Tests

**`tests/main_oms_test.cpp`** — End-to-end OMS test against Delta testnet (requires live credentials):
1. Channel snapshot — all channels reach `Valid` within 30 s
2. CRUD + WS echo — REST create/edit/cancel, verify WS echo arrives in OMS state
3. Forced reconnect — TCP disconnect on order session, verify recovery to `Valid`
4. Reconcile — REST open-orders/positions/wallet drift check
5. Stop orders — SL + TP create, verify in `stop_order_map`, cancel both
6. Market order + position — market buy 1 contract, verify position update in OMS
7. Cancel all orders — create 2 limits, `cancel_all` (limit only), verify position unchanged
8. Close all positions — create limit order + position, `close_all_positions`, verify cleared

Build and run: `cmake --build build --target oms_test && ./build/oms_test`

**Unit tests** (`-DBUILD_TESTS=ON`): `test_ws_parser`, `test_session`, `test_l2_parsing`

---

## Known TODOs / Incomplete Areas

- `SSL_VERIFY_NONE` is set — certificate verification is disabled
- `TickerSession` has no message handler
- `StrategyProcess` is an empty stub — strategy layer not yet built
- `ExecutionManager` / `ConnectionPool` not yet wired up (REST called directly from tests)
- Checksum validation in L2 updates not implemented
- Shared memory write path in FeedProcess not yet connected to `MarketState` output
