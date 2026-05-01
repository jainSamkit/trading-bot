# Scalping Strategy — SOLUSD Perpetual (Delta Exchange)

**Instrument:** SOLUSD perpetual  
**Exchange:** Delta Exchange (India)  
**Target:** 15 bps gross per trade (~10–11 bps net after fees)  
**Style:** High-frequency scalp, continuous trading, no overnight holds

---

## Hypothesis

Short-term orderbook imbalance on SOLUSD perpetuals predicts near-term price direction over a 1–30 second horizon. When more size is stacked on the bid than the ask (or vice versa), informed and momentum participants are leaning in that direction, and the mid-price tends to follow. Combined with tick-level momentum confirmation and a liquid spread filter, this imbalance signal can generate edges sufficient to cover taker exit fees and produce positive EV per trade.

---

## Fee Structure

| Leg | Order type | Base fee | GST (18%) | Total |
|-----|-----------|----------|-----------|-------|
| Entry | Post-only limit (maker) | +2 bps | +0.36 bps | **+2.36 bps** |
| Exit | Aggressive limit / market (taker) | +5 bps | +0.90 bps | **+5.90 bps** |
| **Round trip** | | **+7 bps** | **+1.26 bps** | **+8.26 bps** |

GST is 18% of the base commission on each leg, charged separately.

To net 15 bps after fees, gross target per trade = **23.26 bps** (~24 bps rounded).  
Post-only entry is non-negotiable — taking both legs as taker (2 × 5.90 = 11.80 bps) leaves even less room and makes the 15 bps net target very difficult to achieve consistently.

---

## Signal Definitions

### Primary: Orderbook Imbalance

```
bid_qty  = sum of sizes at best N bid levels  (N = 5)
ask_qty  = sum of sizes at best N ask levels  (N = 5)
imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty)
range: [-1.0, +1.0]
```

- `imbalance > +0.30` → bullish lean → seek long entry
- `imbalance < -0.30` → bearish lean → seek short entry
- Recomputed on every L2 update (tick-driven, not time-driven)

**Open question:** optimal N (currently 5 levels). Deeper levels add noise; shallower levels react too fast to spoofing. Test 3 vs 5 vs 10.

### Confirmation: Tick Momentum (EMA crossover)

```
ema_fast = EMA(mid_price, 5 ticks)
ema_slow = EMA(mid_price, 20 ticks)
momentum = ema_fast - ema_slow
```

- Long signal: `momentum > 0`
- Short signal: `momentum < 0`
- Uses tick count (L2 updates), not wall-clock time

**Open question:** whether tick EMA adds real confirmation or just lags the imbalance signal. May drop this in favour of a simpler "last N mid moves all same direction" check.

### Filter: Spread Regime

```
spread_bps = (best_ask - best_bid) / mid * 10000
```

- `spread_bps ≤ 5` → liquid, trading allowed
- `spread_bps > 5` and `≤ 8` → caution, reduce size
- `spread_bps > 8` → go flat, no new entries

### Filter: Mark-Spot Basis

```
basis_bps = (perp_mark - spot_index) / spot_index * 10000
```

- `|basis_bps| > 15` → funding pressure extreme, avoid trading in direction of basis
- `|basis_bps| > 30` → go flat entirely, funding risk too high

---

## Entry Rules

All conditions must hold simultaneously:

**Long entry:**
1. `imbalance > 0.30`
2. `momentum > 0` (ema_fast > ema_slow)
3. `spread_bps ≤ 5`
4. `basis_bps > -15` (not in extreme negative basis)
5. `net_position < max_position` (see Position Sizing)
6. No active circuit breaker

**Short entry:** mirror of above with reversed imbalance, momentum, basis conditions.

**Order type:** post-only limit at best bid (long) or best ask (short). If not filled within 2 seconds, cancel and re-evaluate — do not chase.

---

## Exit Rules

Priority order (first condition hit wins):

1. **Profit target** — exit when unrealized PnL ≥ 15 bps from entry mid. Use aggressive limit (taker) at target price.
2. **Time stop** — if position held > 30 seconds and unrealized PnL < 0, exit at market immediately. Thesis has expired.
3. **Hard stop** — if price moves 20 bps adverse from entry mid, exit at market. No exceptions.
4. **Signal reversal** — if imbalance flips strongly in opposite direction (`|imbalance| > 0.40` opposite) before profit target, exit early. Take whatever P&L is available.
5. **Spread blowout** — if `spread_bps > 8` while in position, exit at market. Liquidity has evaporated.

---

## Position Sizing & Risk

### Size limits
- Max position: **5 contracts** (long or short, per instrument)
- Entry size: **1–2 contracts** per signal. Scale to max only if imbalance persists across 3+ consecutive L2 updates.
- Never add to a losing position.

### Inventory skew
As net position builds, skew quotes to offload:
- `net_long > 2`: stop adding longs; lower ask to encourage sells (passive unwind)
- `net_short > 2`: mirror

### Daily loss limit
- Stop all trading if daily realized PnL < **−50 bps × daily_avg_contracts_traded**
- Resume next session only (not same day)

---

## Go-Flat Conditions

Stop entering new positions (and consider exiting existing ones) when:

| Condition | Action |
|-----------|--------|
| `spread_bps > 8` | No new entries; exit if in position |
| `\|basis_bps\| > 30` | Go flat entirely |
| 1-min OHLC candle range > 30 bps | Pause — volatility too high for stops |
| 3 consecutive stop-outs | **Circuit breaker**: pause 5 minutes |
| Daily loss limit hit | Stop for the day |
| Known macro event within 5 min | Go flat pre-event, resume after |

---

## Open Questions

1. **Imbalance depth N** — 3 vs 5 vs 10 levels. Needs backtesting or live paper trading to calibrate.
2. **Tick EMA confirmation** — does it add edge or just lag? Consider replacing with "last 3 mid ticks same direction".
3. **Entry cancellation window** — 2 seconds before cancel feels arbitrary. Should be spread-adaptive: wider spread → shorter window (less likely to fill at a good price).
4. **Multiple instruments** — run the same strategy on BTCUSD in parallel? Requires per-instrument state isolation (already supported by ProductTable). Correlation risk: both legs could go against simultaneously.
5. **Sizing model** — flat 1–2 contracts per signal is simple but ignores signal strength. Consider scaling entry size proportional to `|imbalance|` (e.g. 1 contract if 0.30–0.50, 2 if > 0.50).
6. **Fill quality measurement** — need to track adverse selection rate (did price move against us within 500ms of fill?) to detect if we're being picked off by informed flow.
7. **OHLC signal** — not yet used. Possible use: if 5-min candle is strongly directional, bias entries in that direction and avoid counter-trend scalps.

---

## Quantitative Research Findings (19 Days — Mar 1–19 2026)

Data: BTC (7.3M trades), ETH (6.0M trades), SOL (1.1M trades), AVAX (illiquid — not viable)

### Strategy 1 (Primary): L2 Imbalance Scalp — described above

Not yet measured from live data. Hypothesis supported by structure; needs paper-mode validation.

### Strategy 2: BTC→SOL Lead-Lag (Tick-Level Only)

**Signal:** BTC perpetual moves ≥10 bps within a 5-second window.  
**Action:** Enter SOL in the same direction within ≤2 seconds of BTC move completing.  
**Result:**
- Win rate: ~96% directional agreement within 30 seconds
- Gross per trade: +12.67 bps median
- Net after 8.26 bps round-trip: **+4.41 bps**
- Signal frequency: ~10–15 per day (qualifying BTC moves)

**Critical constraint:** Edge window is ≤2 seconds. Without AWS Mumbai (ap-south-1) colocation, typical round-trip to Delta Exchange India is 300–600 ms over internet, leaving 1.4–1.7 s of execution budget. Latency budget is extremely tight. Without colocation this strategy is impractical.

**ETH as filter:** ETH confirms BTC moves ~1 second faster than SOL. Using ETH >10 bps as a filter after BTC >15 bps signal kept 95% of signals and provided negligible edge improvement (+0.06 bps). Not worth the complexity.

**Why candle-level analysis fails:** At 1-min candle granularity, BTC→SOL shows win=46%, gross=-0.65 bps — the edge has already been absorbed into prices by the time the candle closes.

### Strategy 3: Mean Reversion After Extreme 1-Min Candle

**Signal:** 1-min candle with |return| > 40 bps. Enter counter-trend at candle close.  
**Hold:** 5 minutes (exit at market).

| Asset | n/day | Gross | Win | Net (maker-maker) | Net (maker-taker) |
|-------|-------|-------|-----|-------------------|-------------------|
| ETH   | 9.9   | +6.19 bps | 58% | **+1.47 bps** | −2.07 bps |
| SOL   | 11.6  | +3.75 bps | 55% | −0.97 bps | −4.51 bps |
| BTC   | 3.7   | +1.61 bps | 59% | −3.11 bps | −6.65 bps |

Edge only survives if **both legs are maker**. At 10-min hold, edge reverts to noise. ETH is the only asset with positive net; SOL and BTC do not clear maker-maker fees at this hold time. ETH is excluded as a trading instrument (not the focus), but this confirms mean reversion exists in the data.

### Strategies Tested and Rejected (from data)

| Strategy | Result | Why rejected |
|----------|--------|-------------|
| BTC→SOL at 5-min candle | gross −0.27 bps, win 46% | Edge dissipated before candle close |
| Taker buy volume imbalance (3-min rolling >65%) | gross −0.78 bps, win 47% | Completely random at any threshold |
| Volatility continuation (high-vol candle direction) | gross ≈0 bps, win 47% | Noise at all hold times 1–5 min |
| Sustained 15-min taker pressure >65% | gross −1.43 bps, win 48% | Negative gross, momentum already absorbed |
| Compression breakout (5 quiet candles + break) | win 46–47% | Negative net, does not work |

---

## Colocation Requirement

Delta Exchange India operates from AWS Mumbai (ap-south-1). The lead-lag edge disappears within 2 seconds of the BTC move completing. From a residential connection, round-trip latency is 300–600 ms; this leaves ~1 s of headroom in a 2 s window, which is insufficient after accounting for order placement and fill confirmation. To trade Strategy 2 competitively, colocation on a VM in ap-south-1 is required. Strategy 1 (L2 imbalance scalp on SOL) also benefits from colocation but the edge window is longer (the fill or cancel cycle is 2 s per the entry rules), making it more forgiving.

---

## Rejected Ideas

- **Taking both legs at market** — 11.80 bps round-trip cost (2 × 5.90 bps with GST) leaves only 3.20 bps net on a 15 bps gross move, insufficient given slippage and adverse selection. Maker entry is required.
- **Time-based candle signals as primary** — too slow for 15 bps targets. Tick-driven imbalance reacts in milliseconds; 1-min candles react in 10–60 seconds by which time the edge is gone.
- **Holding positions through high-volatility** — early idea was to widen stops during high-vol. Rejected: wide stops mean large losses when they hit, which destroys the P&L of many small wins. Better to go flat entirely.
- **ETH as trading instrument** — mean reversion edge on ETH at +1.47 bps net (maker-maker) at 5-min hold is too thin to rely on. Requires perfect maker fills on both legs and offers no buffer for adverse selection.
- **Cross-asset momentum at candle granularity** — BTC→SOL at 5-min candles: win=46%, gross=−0.27 bps. The lead-lag operates at tick resolution (≤2 s), not candle resolution.

---

## Implementation Status

| Component | Status |
|-----------|--------|
| L2 feed + OrderBook (10 levels) | Done |
| Mark price feed | Done |
| Spot price feed | Done |
| OHLC feed | Done |
| OMS (orders, positions, wallet) | Done |
| REST execution (create/edit/cancel) | Done |
| Imbalance computation | **Not yet built** |
| EMA tick momentum | **Not yet built** |
| Strategy process / signal loop | **Not yet built** |
| Paper trading / signal logging | **Not yet built** |
| Backtesting framework | **Not yet built** |

---

## Next Steps

1. Add `imbalance()` method to `OrderBook` — it already holds the bid/ask depth needed
2. Add tick EMA state to a `SignalState` struct (ema_fast, ema_slow, last_imbalance, last_signal)
3. Build `StrategyProcess` as a read-only consumer of `MarketState` shared memory + `OMSState`
4. **Paper mode first**: log what the bot would do (entry price, direction, exit price, P&L) without sending orders. Run for 1–2 days, measure theoretical edge.
5. Go live with 1-contract max, compare fill quality to paper results
6. Calibrate imbalance threshold and depth N from live data
