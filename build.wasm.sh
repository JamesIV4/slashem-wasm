#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SLASHEM_DIR="$SCRIPT_DIR/slashem-0.0.7E7F3"
BUILD_DIR="$SCRIPT_DIR/build"
WASM_DATA_DIR="$SLASHEM_DIR/wasm-data"
EM_CACHE_DIR="$SCRIPT_DIR/.emcache"
WASM_HINTS="$SLASHEM_DIR/sys/unix/hints/linux-wasm"
WASM_SRC_NOOP="$SLASHEM_DIR/sys/unix/hints/linux-wasm-src-noop"
VALIDATION_SCRIPT="$SCRIPT_DIR/scripts/validate_wasm_artifacts.py"

TOOL_OVERRIDES=(
    CC=emcc
    LINK=emcc
    AR=emar
    "LFLAGS=-O3 -s WASM=1 -s ENVIRONMENT=node -s NODERAWFS=1 -s EXIT_RUNTIME=1"
)

mkdir -p "$EM_CACHE_DIR"
export EM_CACHE="$EM_CACHE_DIR"

copy_bootstrap_sources() {
    cp "$SLASHEM_DIR/sys/share/lev_yacc.c" "$SLASHEM_DIR/util/lev_yacc.c"
    cp "$SLASHEM_DIR/sys/share/lev_lex.c" "$SLASHEM_DIR/util/lev_lex.c"
    cp "$SLASHEM_DIR/sys/share/dgn_yacc.c" "$SLASHEM_DIR/util/dgn_yacc.c"
    cp "$SLASHEM_DIR/sys/share/dgn_lex.c" "$SLASHEM_DIR/util/dgn_lex.c"
    cp "$SLASHEM_DIR/sys/share/lev_comp.h" "$SLASHEM_DIR/include/lev_comp.h"
    cp "$SLASHEM_DIR/sys/share/dgn_comp.h" "$SLASHEM_DIR/include/dgn_comp.h"
}

if ! command -v emcc >/dev/null 2>&1; then
    echo "Error: emcc not found. Install and activate the Emscripten SDK first."
    exit 1
fi

if ! command -v node >/dev/null 2>&1; then
    echo "Error: node not found. The wasm32 phase-1 generator tools run under Node.js."
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 not found. It is required for WASM data validation."
    exit 1
fi

if [ ! -f "$WASM_HINTS" ]; then
    echo "Error: missing make override file: $WASM_HINTS"
    exit 1
fi

if [ ! -f "$WASM_SRC_NOOP" ]; then
    echo "Error: missing src no-op make fragment: $WASM_SRC_NOOP"
    exit 1
fi

if [ ! -f "$VALIDATION_SCRIPT" ]; then
    echo "Error: missing validation script: $VALIDATION_SCRIPT"
    exit 1
fi

echo "=== Slash'EM WASM Build ==="
echo "Source: $SLASHEM_DIR"
echo

echo "--- Setup: generating Unix Makefiles ---"
(
    cd "$SLASHEM_DIR"
    sh sys/unix/setup.sh
)
echo "  done."

echo
echo "--- Phase 1: building wasm32 generator tools and data sources ---"
copy_bootstrap_sources

rm -f "$SLASHEM_DIR/src/"*.o
rm -f "$SLASHEM_DIR/util/"*.o
rm -f "$SLASHEM_DIR/util/"*.wasm

make -C "$SLASHEM_DIR/util" \
    -f Makefile -f ../sys/unix/hints/linux-wasm \
    "${TOOL_OVERRIDES[@]}" \
    makedefs lev_comp dgn_comp dlb tilemap \
    ../include/onames.h ../include/pm.h ../include/vis_tab.h \
    ../src/monstr.c ../src/tile.c

make -C "$SLASHEM_DIR/src" \
    -f Makefile -f ../sys/unix/hints/linux-wasm \
    "${TOOL_OVERRIDES[@]}" \
    ../include/date.h ../include/filename.h

make -C "$SLASHEM_DIR/dat" \
    -f Makefile -f ../sys/unix/hints/linux-wasm \
    "${TOOL_OVERRIDES[@]}"

python3 "$VALIDATION_SCRIPT" "$SLASHEM_DIR/dat"

make -C "$SLASHEM_DIR" \
    -f Makefile -f sys/unix/hints/linux-wasm \
    "${TOOL_OVERRIDES[@]}" \
    dlb

echo "  wasm32 generators, validated level data, and archives ready."

echo
echo "--- Phase 2: preparing embedded WASM data ---"
rm -rf "$WASM_DATA_DIR"
mkdir -p "$WASM_DATA_DIR"

cp "$SLASHEM_DIR/dat/nhshare" "$WASM_DATA_DIR/nhshare"
cp "$SLASHEM_DIR/dat/nhushare" "$WASM_DATA_DIR/nhushare"
touch "$WASM_DATA_DIR/perm"
touch "$WASM_DATA_DIR/record"
touch "$WASM_DATA_DIR/logfile"
touch "$WASM_DATA_DIR/paniclog"

echo "  nhshare:  $(wc -c < "$WASM_DATA_DIR/nhshare") bytes"
echo "  nhushare: $(wc -c < "$WASM_DATA_DIR/nhushare") bytes"

echo
echo "--- Phase 3: building slashem.js + slashem.wasm ---"
rm -f "$SLASHEM_DIR/src/"*.o
rm -f "$SLASHEM_DIR/src/Sysunix" "$SLASHEM_DIR/src/Wasmunix" "$SLASHEM_DIR/src/slashem" \
    "$SLASHEM_DIR/src/slashem.js" "$SLASHEM_DIR/src/slashem.wasm"

make -C "$SLASHEM_DIR/src" \
    -f Makefile \
    -f ../sys/unix/hints/linux-wasm \
    -f ../sys/unix/hints/linux-wasm-src-noop \
    Wasmunix

mkdir -p "$BUILD_DIR"
cp "$SLASHEM_DIR/src/slashem.js" "$BUILD_DIR/slashem.js"
cp "$SLASHEM_DIR/src/slashem.wasm" "$BUILD_DIR/slashem.wasm"

echo
echo "=== Build complete ==="
ls -lh "$BUILD_DIR/slashem.js" "$BUILD_DIR/slashem.wasm"
