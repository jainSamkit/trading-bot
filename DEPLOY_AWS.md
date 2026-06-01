# Reproducing the AWS Tokyo measurement

This walks through deploying the bot to a fresh EC2 in `ap-northeast-1` (Tokyo) and reproducing the **tick-to-trade p50 = 6.40 µs / p99 = 9.73 µs** measurement from [`README.md`](README.md). The host is `c7gn.2xlarge` (Graviton 3E, Neoverse-V1, 8 physical cores, no SMT).

Cost: a 3-4 hour measurement run is about **$1.50-2.00** on-demand. The EBS volume auto-deletes on terminate, so there's no ongoing cost.

---

## 1. Provision EC2

In the AWS Console:

- **Region:** `ap-northeast-1` (Tokyo) — verify the top-right region selector before launching, this is the most common mistake.
- **AMI:** Ubuntu Server 24.04 LTS, **64-bit Arm**. An x86 AMI will fail to boot on a Graviton instance.
- **Instance type:** `c7gn.2xlarge`. Fallback: `c7g.2xlarge` if c7gn is unavailable.
- **Key pair:** create a new RSA pair, download the `.pem`, then `chmod 400 ~/.ssh/your-key.pem` or `ssh` will refuse to use it.
- **Security group:** SSH (22) from `0.0.0.0/0` is fine for a short-lived measurement run (key-only auth, no password). Don't allocate an Elastic IP.
- **Storage:** 30 GB gp3, "Delete on termination" = Yes (default).
- **Spot:** OFF. The whole point is measurement stability.

Test connect:

```bash
ssh -i ~/.ssh/your-key.pem ubuntu@<public-ip>
```

---

## 2. Run the setup script

From your laptop:

```bash
EC2=ubuntu@<public-ip>
scp -i ~/.ssh/your-key.pem scripts/aws_tokyo_setup.sh $EC2:~/
scp -i ~/.ssh/your-key.pem .env                       $EC2:~/trading-bot/.env
```

On the EC2:

```bash
sudo bash ~/aws_tokyo_setup.sh
sudo reboot
```

The script installs build deps (including `libboost-dev`), Docker, and writes:

- `/etc/security/limits.d/99-trading-bot.conf` — `memlock unlimited` so `mlockall` works
- `irq-affinity.service` — pins all device IRQs to cores 0-1 on every boot
- `/etc/default/grub` — kernel cmdline for isolated cores

---

## 3. Fix the GRUB cloud-image override (always hits Ubuntu AMIs on AWS)

After the first reboot, you'll find `cat /proc/cmdline` doesn't show `isolcpus` etc. The reason: `/etc/default/grub.d/50-cloudimg-settings.cfg` resets `GRUB_CMDLINE_LINUX_DEFAULT` *after* the setup script wrote to `/etc/default/grub`. Override it from a higher-priority file:

```bash
sudo tee /etc/default/grub.d/99-trading-bot.cfg > /dev/null <<'EOF'
GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT isolcpus=2,3,4,5 nohz_full=2,3,4,5 rcu_nocbs=2,3,4,5 transparent_hugepage=never default_hugepagesz=2M hugepagesz=2M hugepages=1024"
EOF
sudo sed -i '/# BEGIN trading-bot/,/# END trading-bot/d' /etc/default/grub
sudo update-grub
sudo reboot
```

The `99-` prefix ensures this loads *after* the cloud snippet. The `EOF` line must be at column 0 — leading whitespace causes the heredoc to never close.

After the second reboot, verify:

```bash
cat /proc/cmdline | tr ' ' '\n' | grep -E 'isolcpus|nohz_full|rcu_nocbs|huge'
grep HugePages_Total /proc/meminfo          # → 1024
ulimit -l                                    # → unlimited
lscpu | grep -E 'Model|Core|Thread'          # Neoverse-V1, 8 cores, 1 thread/core
systemctl is-active irq-affinity.service     # active
cat /proc/irq/27/smp_affinity                # → 03  (cores 0+1 only)
```

---

## 4. Sync the codebase

`scripts/sync_to_aws.sh` rsyncs the tree to the EC2 with sensible exclusions (`.env`, `build/`, `.git/`, notebooks, etc.). Edit `EC2_HOST` at the top, then:

```bash
bash scripts/sync_to_aws.sh
```

Critical exclusions — the script handles them but they matter:

- `.env` — your laptop's may have `INFLUX_HOST=influxdb` (Docker-network hostname), which won't work on the EC2 where the bot runs native and Influx is on localhost.
- `build/`, `_deps/`, `.git/`, `notebooks/`, `practice/`, `.venv/` — don't ship these.

zsh-on-Mac gotcha: `rsync --exclude-from=~/foo` does not expand `~` after `=`. Use `$HOME` or an absolute path.

---

## 5. Build (Release, native ARM)

On the EC2:

```bash
cd ~/trading-bot
# Free RAM for LTO — c7gn.2xlarge has 16 GB; LTO + Docker + hugepages can OOM otherwise
docker compose stop influxdb grafana 2>/dev/null

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release    # NOT -DPORTABLE_RELEASE=ON; we want -march=native -flto
cmake --build build -j2                            # -j8 OOMs; -j2 is safe

ls -lh build/trading_bot                           # ~10-15 MB. 0 bytes = link failed.

docker compose start influxdb grafana
```

`shm_inspect` is gated behind `-DBUILD_SHM_INSPECT=ON` (default OFF) because it can lag the venue-aware `SharedState` layout.

---

## 6. `.env` on the EC2

The bot runs **natively on the host**; Influx runs **in Docker** with `8086:8086` published. So:

```bash
sed -i 's/^INFLUX_HOST=.*/INFLUX_HOST=localhost/' ~/trading-bot/.env
```

Not `INFLUX_HOST=influxdb` — that's the Docker-network hostname, only reachable from inside the compose network.

Org / bucket / token must match `docker-compose.yml`'s `DOCKER_INFLUXDB_INIT_*` values (defaults: `trading` / `latency` / `dev-trading-bot-token`).

---

## 7. Start observability + bot

```bash
cd ~/trading-bot
docker compose up -d influxdb grafana          # NOT the trading-bot service; bot runs native
curl -s http://localhost:8086/health           # → status:"pass"

# The bot must run as root to get SCHED_FIFO + mlockall; run in tmux to survive SSH disconnect
sudo rm -f /dev/shm/trading_bot_state          # MUST clean before restart — the kernel object survives pkill
tmux kill-session -t bot 2>/dev/null
tmux new -d -s bot 'cd ~/trading-bot && sudo ./build/trading_bot'
sleep 5
tmux capture-pane -t bot -p | grep '\[pin\]'   # confirm 4× SCHED_FIFO succeeded
```

Expected `[pin]` output (any `WARN` means a tuning step is missing):

```
[pin] feed/ws_reactor → core 2
[pin] feed/ws_reactor → SCHED_FIFO prio 50
[pin] feed/market_state → core 3
[pin] feed/market_state → SCHED_FIFO prio 50
[pin] strategy → core 4
[pin] strategy → SCHED_FIFO prio 50
[pin] oms/execution_manager → core 5
[pin] oms/execution_manager → SCHED_FIFO prio 50
```

SHM cleanup is mandatory on every restart. `pkill -9` kills the process, but `/dev/shm/trading_bot_state` survives.

---

## 8. SSH tunnel for Grafana (from your laptop)

```bash
ssh -i ~/.ssh/your-key.pem -L 3000:localhost:3000 -L 8086:localhost:8086 ubuntu@<public-ip>
```

Leave that terminal open. Then `http://localhost:3000` on your laptop opens Grafana (admin/admin). Add the InfluxDB datasource:

- Query language: **Flux** (not InfluxQL — easiest mistake)
- URL: `http://influxdb:8086` (Docker-network hostname; Grafana is inside the compose network)
- Org: `trading`, Token: `dev-trading-bot-token`, Default bucket: `latency`

---

## 9. Verify the data flow

Wait 60 seconds after the bot starts, then:

```bash
docker exec trading-bot-influxdb influx query --org trading --token dev-trading-bot-token \
  'from(bucket: "latency") |> range(start: -2m) |> filter(fn: (r) => r._measurement == "latency" and r._field == "count") |> last() |> group() |> keep(columns: ["event","target","_value"]) |> sort(columns: ["event","target"])'
```

You must see `queue_time/strategy`, `queue_time/execution_manager`, `tick_to_trade`, and `wire_out` all with non-zero counts. If only the feed-side events show up, the latency push-thread isn't running on a housekeeping core — see the pitfall index below.

---

## 10. Capture results and terminate

Let the bot run for **at least 15 minutes** to collect `n ≥ 500` on `tick_to_trade`. Then on the EC2:

```bash
docker exec trading-bot-influxdb influx query \
  --org trading --token dev-trading-bot-token --raw \
  'from(bucket: "latency") |> range(start: -2h) |> filter(fn: (r) => r._measurement == "latency") |> filter(fn: (r) => r._field == "p50" or r._field == "p90" or r._field == "p99" or r._field == "count") |> last()' \
  > ~/aws_tokyo_$(date +%F).csv
```

From your laptop:

```bash
scp -i ~/.ssh/your-key.pem ubuntu@<public-ip>:~/aws_tokyo_*.csv docs/data/
python3 scripts/plot_latency.py docs/data/aws_tokyo_<date>.csv
```

Two PNGs land next to the CSV — the per-stage bar chart (linear) and the log-scale variant. Take a Grafana screenshot too.

Then terminate in the EC2 console (Instance state → Terminate, leave "Skip OS shutdown" unchecked). Confirm: EBS volume auto-deleted, no Elastic IP allocated, dashboard shows 0 instances.

---

## Pitfalls index

Hit any of these on the first deploy. Sharing so you don't re-debug them.

| Symptom | Root cause | Fix |
|---|---|---|
| `cat /proc/cmdline` missing `isolcpus` after reboot | `/etc/default/grub.d/50-cloudimg-settings.cfg` overrides `GRUB_CMDLINE_LINUX_DEFAULT` after our edit | Put cmdline in `/etc/default/grub.d/99-trading-bot.cfg` (loads later) |
| `Could NOT find Boost` at cmake | `libboost-dev` not in setup script | `sudo apt-get install -y libboost-dev` |
| `lto1: Killed`, `build/trading_bot` is 0 bytes | LTO link-time OOM on 16 GB | `docker compose stop influxdb grafana` first, build with `-j2` |
| `shm_open create failed` on bot restart | `/dev/shm/trading_bot_state` survives `pkill` | `sudo rm -f /dev/shm/trading_bot_state` before restart |
| Feed histograms populate but strategy/OMS are empty | The metrics push thread inherited `SCHED_FIFO` + isolated core from its parent and is being starved | The bot now creates the push thread *before* CPU pin / SCHED_FIFO promotion, and explicitly re-pins it to housekeeping cores 0-1 with `SCHED_OTHER`. Verify this hasn't regressed. |
| Every snapshot logs `RiskOverlay::SkipUninit` | `best_bid` / `best_ask` not populated in `MarketSnapshot` | `orderbook_impl.hpp::snapshot()` must set them from `bestBidTick()` / `bestAskTick()`. |
| All snapshots logged as `SkipWideSpread` | `strategy_config.hpp::max_spread_ticks` too tight for high-price instruments | Raise for measurement runs (`Tick{10000}` on BTCUSD is plenty). |
| `tick_to_trade` n < 50 after 10 min | `requote_threshold_ticks` throttles intent emission | Drop to `Tick{1}` for measurement; restore to production value for live. |
| `WARN SCHED_FIFO(50) failed: Operation not permitted` | Bot not running as root | Use `sudo` (passwordless on Ubuntu AWS AMI by default) or grant `CAP_SYS_NICE`. |
| Grafana panel hangs forever | Datasource URL is `http://localhost:8086` (Grafana hits its own loopback inside its container) | Must be `http://influxdb:8086` (Docker network name). |
| Heredoc never closes (`>` prompts forever) | `EOF` line has leading whitespace | `EOF` must be at column 0. |
| `rsync --exclude-from=~/...` "no such file" | zsh on Mac doesn't tilde-expand after `=` | Use `$HOME` or an absolute path. |

---

## What this measurement does and does not claim

See [`LIMITATIONS.md`](LIMITATIONS.md) for the full picture. The short version:

- This is **single-venue** (Delta) latency. The Phase 2 Bybit client is not yet wired up.
- The strategy is a **fixed-spread placeholder**, not the real microprice + OFI quoter.
- The execution path is **shadow mode** — HMACs are signed and the REST body is constructed, but the bytes are not sent to the exchange. So `wire_out` measures memory-to-stack-buffer, not `send()`-to-socket round trip.
- `n = 1,694` is enough to land p50 and p90 stably; the p99.9 / max tails are exploratory.
