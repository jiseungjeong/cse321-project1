#!/usr/bin/env bash
# Reproduce all four mandatory experiments for the CSE321 Project #1.
# Builds the binary if missing, then runs the full matrix
# (3 trees × 3 d values × 4 experiments) and writes CSVs into results/.
#
# Override defaults via environment variables:
#   CSV=...        path to dataset (default: student.csv)
#   OUT_DIR=...    output directory (default: results)
#   SEED=...       RNG seed (default: 42)
#   QUERIES=...    number of point-search queries (default: 10000)
#   LO / HI        range query window (defaults: 202000000 / 202010000)
#   RATIOS=...     comma-separated delete ratios (default: 0.02,0.10,0.20)
#   DS=...         comma-separated tree orders (default: 3,5,10)
#
# All defaults match plan.md §0 / §12.

set -euo pipefail

cd "$(dirname "$0")/.."

CSV="${CSV:-student.csv}"
OUT_DIR="${OUT_DIR:-results}"
SEED="${SEED:-42}"
QUERIES="${QUERIES:-10000}"
LO="${LO:-202000000}"
HI="${HI:-202010000}"
RATIOS="${RATIOS:-0.02,0.10,0.20}"
DS="${DS:-3,5,10}"

if [[ ! -x ./run ]]; then
    echo "[run_all] binary not found, building..."
    make
fi

if [[ ! -f "$CSV" ]]; then
    echo "[run_all] ERROR: dataset not found at $CSV" >&2
    exit 1
fi

echo "[run_all] CSV=$CSV  OUT_DIR=$OUT_DIR  SEED=$SEED"
echo "[run_all] DS=$DS  RATIOS=$RATIOS  QUERIES=$QUERIES  LO=$LO  HI=$HI"

mkdir -p "$OUT_DIR"

./run all \
    --csv="$CSV" \
    --out-dir="$OUT_DIR" \
    --seed="$SEED" \
    --queries="$QUERIES" \
    --lo="$LO" \
    --hi="$HI" \
    --ratios="$RATIOS" \
    --ds="$DS"

echo
echo "[run_all] CSV outputs:"
ls -lh "$OUT_DIR"/{insert,search,range,delete}.csv

echo
echo "[run_all] sanity: row counts (expect 1+3*3=10 / 10 / 10 / 1+3*3*3=28)"
for f in insert search range delete; do
    printf "  %-7s : %s rows\n" "$f.csv" "$(wc -l < "$OUT_DIR/$f.csv")"
done
