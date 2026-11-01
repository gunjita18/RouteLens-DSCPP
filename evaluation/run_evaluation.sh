#!/usr/bin/env bash
# Reproduces the Phase II algorithm evaluation (docs/evaluation_phase2.md).
#
#   evaluation/run_evaluation.sh                 # 1000 pairs, 100 alternative-route pairs
#   PAIRS=200 ALT_PAIRS=20 evaluation/run_evaluation.sh   # quick smoke run
#
# Needs data/osm/nodes.csv and edges.csv (regenerate with preprocess/export_osm.py)
# and Python with networkx (the project .venv has it).
set -euo pipefail

cd "$(dirname "$0")/.."

PAIRS="${PAIRS:-1000}"
ALT_PAIRS="${ALT_PAIRS:-100}"
SEED="${SEED:-42}"
RESULTS="evaluation/results"

if [ ! -f data/osm/nodes.csv ] || [ ! -f data/osm/edges.csv ]; then
    echo "data/osm/nodes.csv and edges.csv are missing; run preprocess/export_osm.py first" >&2
    exit 1
fi

PYTHON=python3
if [ -x .venv/bin/python ]; then
    PYTHON=.venv/bin/python
fi
"$PYTHON" -c "import networkx" 2>/dev/null || {
    echo "networkx is not installed for $PYTHON" >&2
    exit 1
}

# Timings from an unoptimised build are meaningless, so always build Release.
cmake -S . -B build-eval -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build-eval --target routelens_eval -j

mkdir -p "$RESULTS"
./build-eval/routelens_eval data/osm "$RESULTS" --pairs "$PAIRS" --alt-pairs "$ALT_PAIRS" --seed "$SEED"

"$PYTHON" evaluation/compare_networkx.py "$RESULTS" --report docs/evaluation_phase2.md
