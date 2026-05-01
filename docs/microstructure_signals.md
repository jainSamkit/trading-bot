# Microstructure Signal Toolset

Reference for signal construction, interpretation, and combination. These are building blocks — not strategies on their own. Feed them into a decision logic (see Combined Signal section).

---

## 1. Trade Flow Imbalance (TFI)

**Construction:**
```
signed_size = size × (+1 if aggressor=buy, -1 if aggressor=sell)
TFI(t, w) = Σ signed_size over window [t-w, t]

Normalized: TFI_ratio = TFI / total_volume_in_window  → range [-1, +1]
Z-score:    TFI_z = (TFI - rolling_mean) / rolling_std
```

**Predictive horizon:** 30 seconds to 5 minutes. Decays fast beyond that.

**Why it works:** Aggressors pay the spread because they have a reason — information, urgency, or forced flow (liquidations). Dominant buy aggression means ask is being consumed while bid sits still.

**Limitations:**
- Ignores book depth — 100 BTC into a thin book ≠ 100 BTC into a deep book
- Can't distinguish real aggression from spoofer-triggered stops
- Misses passive flow (most MM activity)

**Upgrades:**
- **With L2:** Use OFI instead — `OFI = Δ(bid_size at top) - Δ(ask_size at top)`. Includes quote cancellations/additions. IC at 1-min horizon typically 2–3× TFI.
- **With OHLC:** Gate on vol regime — TFI z-score over vol-conditional baseline, not absolute. TFI is noisy in vol expansion.
- **With mark price:** Trade price diverging from mark + strong TFI = continuation setup (market moving, index hasn't caught up).
- **With spot:** Perp TFI + spot TFI same direction = real flow. Perp-only TFI = possible liquidation/retail noise.

**Best combined use:**
```
TFI_60s_z > 2.0 AND OFI_60s_z > 1.5 AND spot_TFI same sign
```

---

## 2. VPIN (Volume-Synchronized Probability of Informed Trading)

**Construction** (Easley, López de Prado, O'Hara 2012):
```
1. Define bucket size V = 1/50 of avg daily volume (or fixed notional)
2. Accumulate trades into equal-volume buckets
3. Per bucket: imbalance = |buy_vol - sell_vol| / V
4. VPIN = rolling mean of imbalance over last 50 buckets
```

Key: buckets are equal in **volume, not time**. During calm, one bucket = 30 min. During panic, one bucket = 30 sec. This normalizes for activity bursts.

**What it predicts:** Magnitude of upcoming move, not direction. High VPIN = MMs are being picked off = they're about to defend = volatility expansion imminent.

**Use cases:**
- Volatility breakout filter: only enter directional when VPIN is rising
- Risk gate: VPIN > threshold → reduce size or flatten (competing with informed flow)
- Pairs with TFI: high TFI alone = noise; high TFI + rising VPIN = informed directional flow

**Limitations:**
- Lagging by construction (rolling average over completed buckets)
- Bucket size needs tuning per asset and regime
- "Informed trader" model is a simplification

**Upgrades:**
- **With L2:** VPIN rising + depth decaying on one side = direction + magnitude together
- **With OHLC:** When VPIN rises but realized vol hasn't moved yet, you have lead time — this is the exploitable window
- **With mark price:** Mark-trade gap widening + rising VPIN = order book stress, precedes liquidation cascades
- **With spot:** Perp VPIN spikes but spot VPIN doesn't = leverage-driven stress, not market-wide informed flow

**Best combined use:**
```
VPIN rising AND TFI directional AND L2 depth decay aligned → breakout setup
Exit on VPIN mean reversion
```

---

## 3. Size-Bucketed Flow

**Construction:**
```
Percentile boundaries from 4-week lookback (stable buckets):
  small_TFI  = TFI from trades with size < p25
  medium_TFI = TFI from trades p25–p95
  large_TFI  = TFI from trades > p95
```

**The key insight — different size cohorts have opposite predictive signs in crypto:**
- **Small (retail):** often *contrarian*. Retail piles in at tops, capitulates at bottoms. `small_TFI strongly positive` = local top forming.
- **Large (institutional/MM unwinds):** usually *informed*. Big aggressive prints rarely happen accidentally. `large_TFI directional` = follow it.
- **Medium:** noisy, mixed. Often the safest to ignore.

Best signal: `large_TFI positive AND small_TFI negative` = institutions buying while retail sells.

**Limitations:**
- Iceberg orders and sweep splits corrupt bucketing — 500 BTC institutional order may show as 50 prints of 10 BTC
- Need to tag child orders of same parent (same price, microsecond-spaced)
- Bucket boundaries are venue-specific and shift with regimes

**Upgrades:**
- **With L2:** Large TFI lifts but no large resting orders placed afterward = pure aggression, no defense = continuation
- **With OHLC:** Large flow during low-volume hours is more informative than during peak hours
- **With spot:** Large directional flow on both perp and spot = real institutional accumulation. Perp-only = leverage-driven, likely reverses.

**Best combined use:**
```
large_TFI_z > 2 AND small_TFI_z < -1 AND spot large_TFI_z > 1
→ institutional accumulation against retail panic, hold 15–60 min
```

---

## 4. Trade Intensity (Exponentially Weighted)

**Construction:**
```
intensity(t) = Σ exp(-(t - t_i) / τ)  for all trades i with t_i ≤ t
```
τ = decay constant (e.g., 30 seconds). Equivalent: counter that decays continuously and increments on each trade.

Deseasonalize: compare to typical intensity for this hour-of-week.

**Why it matters:** Activity itself is signal. Intensity spikes mark regime shifts — news, liquidation, large algo execution. Direction often follows.

**Use cases:**
- Regime detection: low intensity = quiet, mean-reversion. High = trending/volatile, breakout.
- Signal gating: TFI during intensity spikes is much more informative (real flow, larger sample)
- Microstructure event detection: 5σ intensity spike = something is definitely happening

**Limitations:**
- Direction-agnostic alone
- Time-of-day patterns dominate — must deseasonalize

**Upgrades:**
- **With L2:** Intensity spike + book thinning = liquidity vacuum, often precedes 30–80 bps moves
- **With OHLC:** High intensity + flat price = absorption — large player filling without moving price, often a prelude to directional move
- **With mark:** Intensity spike + mark unchanged = derivative-side noise (funding flow). Intensity spike + mark moving = real consensus shift
- **With spot:** Both spike together = market-wide event. Only one spikes = venue-specific stop-run

**Best combined use:**
```
intensity_z > 3σ above hour-of-week baseline AND TFI directional AND VPIN rising
→ high-conviction event-driven setup, best entries within 30s of intensity spike
```

---

## 5. Realized Volatility

**Construction:**
```
RV(t, w) = Σ (log(p_i) - log(p_{i-1}))²  for trades in window [t-w, t]
σ_realized = sqrt(RV × periods_per_year)
```

For noisy tape data, use two-scales realized volatility or realized kernels to filter microstructure noise (bid-ask bounce inflates naive RV).

**Why it matters:** Vol is the most persistent and predictable property of price. It's the denominator of every Sharpe calculation and input to every position size formula. Get this wrong and everything else is broken.

**Use cases:**
- **Position sizing:** target constant vol per trade — `position_size = target_vol / realized_vol`
- **Regime detection:** vol terciles define regimes. Mean-reversion works in low vol; breakout works in high vol.
- **Signal normalization:** express TFI, OFI, returns as multiples of realized vol → comparable across assets and time
- **Stop placement:** stops at N × σ_realized (adaptive) rather than fixed bps

**Limitations:**
- Backward-looking — by the time RV expands, the move already happened
- Short-window RV is noise-inflated — need to handle bid-ask bounce

**Upgrades:**
- **With L2:** Quoted spread vs. realized vol ratio. When RV << implied-from-spread vol, MMs are pricing risk in before it materializes.
- **With OHLC:** Compare estimators (close-close, Parkinson high-low, Garman-Klass OHLC). Discrepancies carry info about intrabar dynamics.
- **With mark:** Trade-price RV >> mark RV = microstructure churn. Mark RV leading = real movement.
- **With spot:** Realized vol basis (spot vs. perp) predicts funding. Wide gap usually mean-reverts.

**Note:** RV is rarely a signal alone — it is the **conditioning input to all other signals.**

---

## 6. Clustering / Burstiness

**Construction** (Goh & Barabási burstiness coefficient):
```
B = (σ_τ - μ_τ) / (σ_τ + μ_τ)
```
where τ = inter-arrival times between trades.
- B = -1: perfectly regular
- B = 0: Poisson (random)
- B = +1: extremely bursty

Alternative: ratio of trades in densest 10% of time vs. uniform expectation.

**Why it matters:** Poisson flow = many independent participants, no shared signal, noise. Bursty flow = coordinated activity — algo execution, liquidation cascade, news reaction, stop-run. Most predictable price movement happens in bursty regimes.

**Use cases:**
- TFI validity gate: TFI during bursty regime is more informative than during calm
- Iceberg detection: clusters of same-size trades at near-identical prices = algorithmic execution; track the side and follow
- Stop-run identification: short, intense, one-directional clusters = stop cascade; fade after the cluster expecting reversion

**Limitations:**
- Sensitive to window choice
- Can't distinguish "algo execution" (follow) from "panic" (get out) without other signals

**Upgrades:**
- **With L2:** Bursty trades + quote thinning = liquidity event. Bursty trades + stable quotes = absorption.
- **With OHLC:** Burstiness during low-OHLC-vol windows = something happening that hasn't shown up in price yet
- **With size-bucketed flow:** Bursty + small flow = retail FOMO/panic. Bursty + large flow = algo execution. Bursty + mixed = news reaction.

**Best combined use:**
```
burstiness > 0.7 AND large_TFI directional AND intensity_z > 2
→ algorithmic execution detected, follow direction, exit when burstiness reverts
```

---

## Combined Signal Template

Features aren't a strategy — they're a feature vector. A realistic combined entry:

```
LONG ENTRY:
  TFI_60s_z      > 2.0          # directional pressure
  OFI_60s_z      > 1.5          # confirmed in L2 book
  large_TFI_z    > 1.5          # institutional, not retail
  small_TFI_z    < 0.5          # no retail FOMO
  VPIN_rising    = true         # informed flow regime
  intensity_z    > 1.5          # real activity, not thin
  realized_vol   in [p25, p80]  # not dead, not chaotic
  spot_TFI       same sign      # cross-venue confirmation
  mark - last    not adverse    # not buying into divergence

EXIT (first condition hit):
  Time stop:    5 minutes
  Hard stop:    -1.5 × σ_realized
  Target:       +2.5 × σ_realized
  Soft exit:    TFI_z reverts below 0.5
  Regime exit:  VPIN spikes (volatility expansion = get out)
```

Each gate eliminates a failure mode. The conjunction is what produces tradeable setups.

---

## P&L Math

### Fee structure (Delta Exchange scalper offer)
- Opening leg only charged if position closed within 15 min (alts) / 30 min (BTC/ETH)
- Maker open: 2.36 bps — break-even gross = 2.36 bps
- Taker open: 5.90 bps — break-even gross = 5.90 bps
- Without scalper offer: 8.26 bps round trip

### The three scenarios (200 trades/day, $100k notional per trade)

**Pessimistic — losing money without realising it:**
```
Avg win = 25 bps gross, avg loss = 25 bps gross (symmetric)
Costs = 8 bps round trip (no scalper offer)
Net per winner = +17 bps, net per loser = -33 bps
Win rate = 52%
EV = 0.52 × 17 + 0.48 × (-33) = -7.0 bps/trade
Daily P&L on $100k notional: -$1,400
→ Losing money. Most common outcome.
```

**Realistic break-even-plus:**
```
Avg win = 35 bps gross, avg loss = 25 bps gross (1.4× asymmetry)
Costs = 6 bps (mostly maker)
Net per winner = +29 bps, net per loser = -31 bps
Win rate = 54%
EV = 0.54 × 29 + 0.46 × (-31) = +1.4 bps/trade
Daily P&L: +$280 → Annualized ~70% on $100k. Sharpe ~1.0–1.5.
```

**Genuinely good strategy:**
```
Avg win = 40 bps gross, avg loss = 20 bps gross (2× asymmetry)
Costs = 5 bps (mostly maker)
Net per winner = +35 bps, net per loser = -25 bps
Win rate = 55%
EV = 0.55 × 35 + 0.45 × (-25) = +8.0 bps/trade
Daily P&L: +$1,600 → Annualized ~400% gross. Sharpe 2–3.
```

### Realistic outcome ranges

| Outcome | Sharpe | Annualized return | Max DD |
|---------|--------|------------------|--------|
| What most retail systematic traders achieve | 0.3–0.8 | 10–30% | 15–25% |
| Median "strategy works" | 1.2–1.8 | 60–120% | 10–15% |
| Best case | 2.0–2.5 | 150–250% | 8–12% |

### Key insight

"20–40 bps win" sounds like it dominates costs. But losses are usually similar magnitude to wins, and costs apply to every trade. The math hinges on:

1. **Win rate** — must be > 50% by enough to overcome cost drag
2. **Win/loss asymmetry** — 1.5× ratio with 53% hit beats symmetric with 55% hit rate
3. **Costs are non-negotiable** — every bps of fees comes straight off EV

A 1.5× win/loss ratio requires asymmetric exit rules: tight stops (cut losers fast), wider profit targets (let winners run). This is harder to achieve than it sounds — requires exit logic that is as carefully engineered as the entry signal.

### What impresses an MM/HFT firm (Keyrock, Wintermute)

Not absolute return. What gets you in the door:
- Sharpe 1.5+, drawdown < 10%
- Methodology is rigorous and honest about data limitations
- Can explain the edge in microstructure terms (OFI, adverse selection, informed flow)
- Have correctly identified what L2 / colocation would add

The story is: "Here is what I found, here is what I could not test without L2, here is what the full setup would look like." That is a substantive technical conversation, not a retail backtest pitch.
