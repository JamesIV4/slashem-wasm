# Slash'EM WASM

This repo builds the Slash'EM wasm port and now generates special-level data
with wasm32 phase-1 tools, not host-native 64-bit tools. That matters because
`lev_comp` and `dgn_comp` write old binary formats that depend on host type
sizes such as `long` and raw struct layout.

## Prerequisites

- `emcc`
- `node`
- `python3`

## Build

Run the full wasm build from the repo root:

```sh
./build.wasm.sh
```

That script does three things:

1. Builds the phase-1 generator tools as Node-runnable wasm32 executables.
2. Regenerates the data files and validates the generated special-level
   artifacts before packing the DLB archives.
3. Builds `build/slashem.js` and `build/slashem.wasm`.

## Deterministic Validation

The build already runs the validator automatically, but you can run it
directly:

```sh
python3 scripts/validate_wasm_artifacts.py slashem-0.0.7E7F3/dat
```

Expected success output:

```text
Validated wasm32 level artifacts: oracle.lev layout is 32-bit compatible.
```

To print the pinned artifact fingerprints:

```sh
python3 scripts/validate_wasm_artifacts.py --print-fingerprints slashem-0.0.7E7F3/dat
```

Current expected values:

- `oracle.lev`: size `985`, sha256 `dd293a27252ab9532f2e5ed18b7cdde9c7d2e7953f7cebcba7971268c09ee743`
- `dungeon`: size `4888`, sha256 `9d137cfafa7fee7e3844f2cb4846f56e6096fb7fa301bf389e9fec611305279a`

Optional byte-level check for the old layout bug:

```sh
xxd -g1 -l 48 -s 16 slashem-0.0.7E7F3/dat/oracle.lev
```

The important detail is that the first room count now starts at byte `0x20`
with `07`, not four bytes later as it did in the broken 64-bit-generated file.

## Runtime Verification

After rebuilding:

1. Launch the new `build/slashem.js` and `build/slashem.wasm`.
2. Load or start a game and descend into a special level such as Oracle.
3. Confirm the game no longer crashes with:
   `load_maze error: numpart = 0`

One concrete regression path is descending from dungeon level 4 to 5, which
previously triggered:

```text
Suddenly, the dungeon collapses.
load_maze error: numpart = 0
```

That failure should no longer occur with the regenerated wasm32-compatible
special-level artifacts.
