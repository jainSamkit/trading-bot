#!/usr/bin/env bash
# Sync the local trading-bot working tree to the AWS Tokyo EC2 box.
#
# Usage:  bash scripts/sync_to_aws.sh
#
# Edit EC2_HOST and SSH_KEY below if they change. --delete is on, so anything
# matching an exclude pattern is REMOVED from the EC2 each run — that's how
# we keep the box clean of practice/notebooks/build cruft even after earlier
# uploads accidentally pushed them.

set -euo pipefail

EC2_HOST="ubuntu@52.198.205.93"
SSH_KEY="$HOME/.ssh/trading-bot-key.pem"
LOCAL_DIR="$HOME/Desktop/trading-bot/"
REMOTE_DIR="~/trading-bot/"

rsync -avz --delete \
    --exclude='build/' \
    --exclude='build-test/' \
    --exclude='CMakeFiles/' \
    --exclude='_deps/' \
    --exclude='.git/' \
    --exclude='.venv/' \
    --exclude='.cursor/' \
    --exclude='.claude/' \
    --exclude='practice/' \
    --exclude='notebooks/' \
    --exclude='cpp_100_bundle/' \
    --exclude='.DS_Store' \
    --exclude='__pycache__/' \
    --exclude='*.pyc' \
    --exclude='*.log' \
    --exclude='.env' \
    --exclude='.env.*' \
    -e "ssh -i ${SSH_KEY}" \
    "${LOCAL_DIR}" \
    "${EC2_HOST}:${REMOTE_DIR}"

echo
echo "── Remote contents (should be small) ─────────────────────"
ssh -i "${SSH_KEY}" "${EC2_HOST}" 'du -sh ~/trading-bot/ ~/trading-bot/*/ 2>/dev/null | sort -h'
