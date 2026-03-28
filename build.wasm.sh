#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SLASHEM_DIR="$SCRIPT_DIR/slashem-0.0.7E7F3"
BUILD_DIR="$SCRIPT_DIR/build"
WASM_DATA_DIR="$SLASHEM_DIR/wasm-data"
EM_CACHE_DIR="$SCRIPT_DIR/.emcache"
WASM_HINTS="$SLASHEM_DIR/sys/unix/hints/linux-wasm"
WASM_SRC_NOOP="$SLASHEM_DIR/sys/unix/hints/linux-wasm-src-noop"

NATIVE_OVERRIDES=(
    CC=cc
    LINK=cc
    AR=ar
    LFLAGS=
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

if [ ! -f "$WASM_HINTS" ]; then
    echo "Error: missing make override file: $WASM_HINTS"
    exit 1
fi

if [ ! -f "$WASM_SRC_NOOP" ]; then
    echo "Error: missing src no-op make fragment: $WASM_SRC_NOOP"
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
echo "--- Phase 1: building native generators and data archives ---"
copy_bootstrap_sources

rm -f "$SLASHEM_DIR/src/"*.o
rm -f "$SLASHEM_DIR/util/"*.o

make -C "$SLASHEM_DIR/util" \
    -f Makefile -f ../sys/unix/hints/linux-wasm \
    "${NATIVE_OVERRIDES[@]}" \
    makedefs lev_comp dgn_comp dlb tilemap \
    ../include/onames.h ../include/pm.h ../include/vis_tab.h \
    ../src/monstr.c ../src/tile.c

make -C "$SLASHEM_DIR/src" \
    -f Makefile -f ../sys/unix/hints/linux-wasm \
    "${NATIVE_OVERRIDES[@]}" \
    ../include/date.h ../include/filename.h

make -C "$SLASHEM_DIR/dat" \
    -f Makefile -f ../sys/unix/hints/linux-wasm \
    "${NATIVE_OVERRIDES[@]}"

make -C "$SLASHEM_DIR" \
    -f Makefile -f sys/unix/hints/linux-wasm \
    "${NATIVE_OVERRIDES[@]}" \
    dlb

echo "  native generators and archives ready."

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
