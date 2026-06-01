# Limitations

What this repo **does not** model and what the measurement **does not** claim. Written up front because the project is being published as a portfolio piece and the worst thing it could do is overclaim.

---

## What the latency measurement does not claim

The numbers in [`README.md`](README.md) (`p50 = 6.40 µs`, `p99 = 9.73 µs`) are end-to-end tick-to-trade on **AWS Tokyo `c7gn.2xlarge`**. They are honest but narrow:

### Single-venue
The measurement is on the **Delta L2 feed only**. The Phase 2 Bybit V5 client (OB200 + `publicTrade`) is not yet wired, so there is no cross-venue lead-lag component in the measured path.

### Strategy is a placeholder
The quoter is a **fixed-spread inventory-neutral placeholder**. It produces a `QuoteIntent` every snapshot whose mid moves more than `requote_threshold_ticks`. There is no microprice, no OFI, no inventory skew. The Phase 2 quoter (microprice + Cont-Kukanov OFI) is not yet built.

### Shadow execution, not live
The execution path is `ExecMode::Shadow`. The HMAC is signed and the REST body is constructed (which exercises most of the hot path), but the bytes are not handed to `send()`. So `wire_out` measures **decision-to-stack-buffer**, not `send()`-to-socket round trip. A real wire round trip to Delta from Tokyo adds a few hundred microseconds in the median.

### `tick_to_trade` is post-`SSL_read` userspace to intent-emitted
"tick" is a userspace `cntvct_el0` timestamp taken immediately after `SSL_read` returns in the WS reactor. This means the kernel network stack and the OpenSSL record decryption have already run by the time the start clock is taken — it is **not** a hardware or `SO_TIMESTAMPING` receive timestamp. The number measures what userspace can do once the bytes are in hand; it does not measure NIC-to-userspace.

"trade" is the quote intent emitted by the strategy, with the shadow REST body fully signed and constructed. It does **not** include the on-the-wire propagation to the exchange or any acknowledgement.

### Sample size
`n = 1,694` `tick_to_trade` samples gathered over a ~15-minute run. Enough to land p50 and p90 stably. **p99 has wider confidence than the single number suggests, and p99.9 / max should be treated as exploratory.** A 24-hour run would tighten this and is on the Phase 2 list.

### Hardware
`c7gn.2xlarge`, Graviton 3E (Neoverse-V1), 8 physical cores no SMT, 16 GB RAM. Cores 2-5 isolated, cores 0-1 for housekeeping + IRQs, cores 6-7 not used during the measurement. Not directly comparable to x86 numbers — Graviton has different cache and pipeline characteristics, and `rdtscp` resolves to `cntvct_el0` rather than the x86 TSC.

### No kernel-bypass
This is a `SO_BUSY_POLL` + `SCHED_FIFO` + isolated-cores design, not DPDK / Onload / kernel-bypass. The plan calls out busy-poll as the "credible kernel-bypass-lite stopping point" for a portfolio piece. A real HFT shop would go further.

---

## What the strategy does not model

The placeholder fixed-spread quoter exists only to exercise the latency machinery. It is not a serious strategy claim.

The Phase 2 microprice + OFI quoter, once built, will be a credible quoter but it will still **not** be a serious live-P&L claim because:

- **No queue-position model yet.** Phase 2 plans `total_volume` / `our_volume` / `est_volume_ahead` per level with a cancel-flow heuristic (back-bias), but the placeholder treats fills as if joining the back of the queue always.
- **No adverse-selection model in the placeholder.** Phase 2 adds a rolling fill-conditional 5-second return per side.
- **No real impact model.** The Phase 2 fill simulator's three modes (optimistic / pessimistic / probabilistic) approximate this but are calibrated to recorded tape, not first-principles.
- **No latency-aware re-quoting.** A real quoter cancels-and-replaces based on book moves vs. its own latency, which we'd need a real wire round trip to model honestly.

---

## What this repo does not implement

Listed up front so reviewers don't have to find these by reading code:

- **Bybit V5 WS client.** Phase 2.
- **Queue-position-aware orderbook.** Phase 2.
- **Three-mode fill simulator.** Phase 2.
- **Tick recorder + deterministic replayer + `make replay-determinism`.** Phase 2.
- **Microprice + OFI quoter.** Phase 2.
- **Production risk overlays.** Scaffolding exists in `strategy/risk_overlay.hpp`; the daily-loss kill, stale-feed kill, and spread-blowout kill are not yet wired through the live decision path.
- **Live order placement.** The execution path can reach `ExecMode::Live`, but the portfolio runs are always `Shadow`. No live order has been placed from this binary.
- **TLS certificate verification.** `SSL_VERIFY_NONE` is set on the transport client. Fine for testnet; not acceptable in a production deployment.
- **L2 checksum validation.** Delta's L2 feed has a checksum field; we don't verify it.
- **`TickerSession`.** Stubbed; no message handler.
- **`StrategyProcess` legacy stub.** A newer strategy process exists; the original stub file is still in the tree.

---

## What's known to be brittle

- **macOS support is limited to compile.** The production stack uses `epoll`, `timerfd`, `eventfd`, `shm_open`, and `SCHED_FIFO`, which don't have first-class equivalents on macOS. Build inside the Docker dev environment if you're on a Mac.
- **InfluxDB push thread.** The metrics push thread must be created *before* the hot thread's CPU pin and `SCHED_FIFO` promotion, then explicitly re-pinned to housekeeping cores 0-1 with `SCHED_OTHER`. Otherwise it inherits the realtime policy and starves on the isolated cores, and all you see in Grafana is the feed-side stages. This is fragile — `feed.hpp`, `oms.hpp`, and `strategy.hpp` all have to call `start_periodic_push()` first, and `registry.hpp` has to do the re-pin.
- **`/dev/shm/trading_bot_state`** survives `pkill -9`. Every restart needs `rm -f`.
- **GRUB cmdline overrides on AWS Ubuntu AMIs.** See [`DEPLOY_AWS.md`](DEPLOY_AWS.md) step 3 — the cloud-image cfg resets `GRUB_CMDLINE_LINUX_DEFAULT` and you have to write to a higher-priority file to win.

---

## Caveats on the methodology

- **`rdtscp` calibration.** On ARM, "rdtscp" is `cntvct_el0`, calibrated against `CLOCK_MONOTONIC` over a 100 ms window at boot. The calibration is good to within ~50 ppm on Graviton, which is well below the noise floor of a low-microsecond measurement, but it's not free.
- **Span overhead.** Each `Span` constructor and destructor does one `cntvct_el0` read and one log-bucket increment (atomic relaxed). On Graviton this is a few ns; in the histograms this is included implicitly in every reported number.
- **JSON parse stage.** `simdjson` ondemand is zero-copy through the message, but the `ws_frame` stage's output is a contiguous buffer, not the kernel buffer — there's one memcpy out of the SSL record that we'd ideally remove.
- **No load test.** The numbers are from live Delta testnet at its natural message rate (a few hundred messages/sec). They are not under bursty / hostile load.
- **No comparison baseline.** I have not measured a comparable open-source stack (e.g., a generic Boost.Asio + nlohmann::json client) on the same host. A side-by-side comparison would make the numbers more credible; it's not done yet.
