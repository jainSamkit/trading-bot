#!/usr/bin/env bash
# AWS Tokyo (ap-northeast-1) one-shot host setup for the trading bot.
#
# Target: c7gn.2xlarge running Ubuntu 24.04 LTS Arm.
# Run as: sudo bash aws_tokyo_setup.sh
#
# What it does (idempotent — safe to re-run):
#   1. Installs build deps (cmake, g++, OpenSSL headers, git) + Docker.
#   2. Adds the kernel isolation cmdline to GRUB (isolcpus / nohz_full /
#      rcu_nocbs on cores 2-7; hugepages reservation).
#   3. Raises RLIMIT_MEMLOCK so mlockall() succeeds for the ubuntu user.
#   4. Installs a systemd unit (irq-affinity.service) that, on every boot,
#      pins every movable hardware IRQ to cores 0-1 — keeps interrupts off
#      the hot cores.
#   5. Adds `ubuntu` to the docker group so non-sudo `docker` works.
#
# After this script finishes, **REBOOT ONCE**. Then verify with the
# `aws_tokyo_verify.sh` companion (or by hand — instructions printed at end).
#
# What this script does NOT do:
#   • Clone the repo or build the bot — you do that post-reboot.
#   • Start InfluxDB/Grafana — docker compose handles that.
#   • Set CPU frequency governor — Graviton's clock is fixed; no DVFS to tune.
#     (On Intel/AMD you'd want `cpupower frequency-set -g performance` here.)

set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "ERROR: run as root: sudo bash $0" >&2
    exit 1
fi

# ── Cores that hot threads will pin to (matches src/config/config.hpp) ───────
ISOLATED_CORES="2,3,4,5,6,7"
HUGEPAGES_2M=1024  # 2048 MB reserved as 2 MB hugepages — covers SHM + ring slack

echo
echo "──────────────────────────────────────────────────"
echo " AWS Tokyo host setup — trading bot"
echo " Isolated cores: ${ISOLATED_CORES}"
echo " 2 MB hugepages: ${HUGEPAGES_2M} (= $((HUGEPAGES_2M*2)) MB)"
echo "──────────────────────────────────────────────────"
echo

# ── 1. apt packages ──────────────────────────────────────────────────────────
echo "[1/5] Installing build dependencies and observability tools…"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
    build-essential cmake pkg-config git curl ca-certificates \
    libssl-dev libboost-dev \
    hwloc-nox util-linux \
    htop sysstat linux-tools-common linux-tools-aws \
    chrony

# Docker via the official convenience script — gets us a recent stable build.
if ! command -v docker >/dev/null 2>&1; then
    echo "[1/5] Installing Docker…"
    curl -fsSL https://get.docker.com | sh
    systemctl enable --now docker
fi

# Let the ubuntu user run docker without sudo (takes effect on next login).
usermod -aG docker ubuntu || true

# ── 2. GRUB cmdline ──────────────────────────────────────────────────────────
echo "[2/5] Configuring kernel cmdline (GRUB)…"

# The exact cmdline we want appended. We DO NOT touch console=, root=, etc.
GRUB_EXTRA="isolcpus=${ISOLATED_CORES} nohz_full=${ISOLATED_CORES} rcu_nocbs=${ISOLATED_CORES} transparent_hugepage=never default_hugepagesz=2M hugepagesz=2M hugepages=${HUGEPAGES_2M} mitigations=auto"

# CRITICAL: Ubuntu's AWS AMI ships /etc/default/grub.d/50-cloudimg-settings.cfg
# which RESETS GRUB_CMDLINE_LINUX_DEFAULT (rather than appending). Files in
# /etc/default/grub.d/ are sourced AFTER /etc/default/grub in lexical order,
# so writing our cmdline to /etc/default/grub gets silently wiped. Write to
# 99-trading-bot.cfg instead so we load AFTER the cloud snippet and win the
# assignment race.
GRUB_D_DIR=/etc/default/grub.d
GRUB_TB_FILE="${GRUB_D_DIR}/99-trading-bot.cfg"
mkdir -p "$GRUB_D_DIR"
cat > "$GRUB_TB_FILE" <<EOF
# trading-bot — kernel isolation for low-latency hot threads.
# Loads after 50-cloudimg-settings.cfg, so this assignment wins.
GRUB_CMDLINE_LINUX_DEFAULT="\$GRUB_CMDLINE_LINUX_DEFAULT ${GRUB_EXTRA}"
EOF

# Also clean up any older trading-bot edit in /etc/default/grub from a prior
# run of this script (harmless if absent).
GRUB_FILE=/etc/default/grub
cp -n "$GRUB_FILE" "${GRUB_FILE}.orig"     # one-time backup
sed -i '/# BEGIN trading-bot/,/# END trading-bot/d' "$GRUB_FILE"

update-grub

# Quick sanity check — the regenerated grub.cfg should now contain our flags.
if grep -q 'isolcpus' /boot/grub/grub.cfg; then
    echo "[2/5] ✓ isolcpus / nohz_full / rcu_nocbs / hugepages baked into /boot/grub/grub.cfg"
else
    echo "[2/5] ⚠ WARNING: grub.cfg does NOT contain isolcpus after update-grub."
    echo "      Check /etc/default/grub.d/ for another file overriding GRUB_CMDLINE_LINUX_DEFAULT."
fi

# ── 3. RLIMIT_MEMLOCK (so mlockall succeeds without root) ────────────────────
echo "[3/5] Raising memlock limit for ubuntu user…"

LIMITS_FILE=/etc/security/limits.d/99-trading-bot.conf
cat > "$LIMITS_FILE" <<'EOF'
# Allow the trading bot to lock arbitrary amounts of memory via mlockall().
# Without this, mlockall() returns ENOMEM at ~64 KB on stock Ubuntu.
ubuntu  soft  memlock  unlimited
ubuntu  hard  memlock  unlimited
root    soft  memlock  unlimited
root    hard  memlock  unlimited
EOF

# pam_limits is enabled by default on Ubuntu sshd, so a fresh SSH session
# picks this up — no service restart needed.

# ── 4. IRQ affinity — keep hardware interrupts off the hot cores ─────────────
echo "[4/5] Installing IRQ affinity systemd unit…"

cat > /usr/local/sbin/pin-irqs-housekeeping.sh <<'EOF'
#!/usr/bin/env bash
# Pin every movable hardware IRQ to cores 0-1 only. Some IRQs are per-CPU
# (timer, IPI) and refuse the write — we ignore those errors.
set -u
HOUSEKEEPING_MASK="03"   # binary 0000 0011 — cores 0 and 1

for irq_dir in /proc/irq/[0-9]*; do
    irq=$(basename "$irq_dir")
    echo "$HOUSEKEEPING_MASK" > "$irq_dir/smp_affinity" 2>/dev/null || true
done

# Also tell the kernel where new IRQs should land by default.
echo "$HOUSEKEEPING_MASK" > /proc/irq/default_smp_affinity 2>/dev/null || true
EOF
chmod +x /usr/local/sbin/pin-irqs-housekeeping.sh

cat > /etc/systemd/system/irq-affinity.service <<'EOF'
[Unit]
Description=Pin movable hardware IRQs to housekeeping cores 0-1
After=multi-user.target
ConditionPathExists=/usr/local/sbin/pin-irqs-housekeeping.sh

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/pin-irqs-housekeeping.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable irq-affinity.service

# ── 5. Done ──────────────────────────────────────────────────────────────────
echo "[5/5] Setup complete."
echo
echo "──────────────────────────────────────────────────"
echo " NEXT STEPS"
echo "──────────────────────────────────────────────────"
echo
echo " 1. REBOOT the instance now:"
echo "      sudo reboot"
echo
echo " 2. After reboot, SSH back in and verify:"
echo
echo "      cat /proc/cmdline | tr ' ' '\\n' | grep -E 'isolcpus|nohz_full|rcu_nocbs|huge'"
echo "      # should show: isolcpus=${ISOLATED_CORES}, nohz_full=${ISOLATED_CORES},"
echo "      #              rcu_nocbs=${ISOLATED_CORES}, hugepages=${HUGEPAGES_2M}"
echo
echo "      cat /proc/meminfo | grep -i huge"
echo "      # should show: HugePages_Total = ${HUGEPAGES_2M}"
echo
echo "      ulimit -l   # should print: unlimited"
echo
echo "      lscpu | grep -E 'Model name|Core|Thread'"
echo "      # should show: Thread(s) per core: 1, Core(s) per socket: 8"
echo
echo "      systemctl status irq-affinity.service   # should be: active (exited)"
echo
echo " 3. Then clone the repo and build:"
echo
echo "      git clone <your-repo-url> ~/trading-bot && cd ~/trading-bot"
echo "      # copy .env from your laptop:"
echo "      #   scp -i ~/.ssh/trading-bot-key.pem ~/Desktop/trading-bot/.env ubuntu@<ip>:~/trading-bot/.env"
echo "      # edit .env on the EC2 to set INFLUX_HOST=localhost (since the bot"
echo "      # runs natively on the host while Influx runs in Docker)."
echo
echo "      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPORTABLE_RELEASE=ON"
echo "      cmake --build build -j\$(nproc)"
echo
echo " 4. Start the observability stack (Influx + Grafana, NOT the bot container):"
echo
echo "      docker compose up -d influxdb grafana"
echo
echo " 5. Run the bot (sudo so SCHED_FIFO + mlockall work without capability juggling):"
echo
echo "      sudo ./build/trading_bot"
echo
echo " 6. From your laptop, tunnel Grafana + Influx:"
echo
echo "      ssh -i ~/.ssh/trading-bot-key.pem \\"
echo "          -L 3000:localhost:3000 -L 8086:localhost:8086 \\"
echo "          ubuntu@<ec2-public-ip>"
echo
echo "      then browse http://localhost:3000  (admin / admin)"
echo
