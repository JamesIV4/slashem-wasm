#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

EXPECTED_FINGERPRINTS: dict[str, tuple[int, str]] = {
    "oracle.lev": (
        985,
        "dd293a27252ab9532f2e5ed18b7cdde9c7d2e7953f7cebcba7971268c09ee743",
    ),
    "dungeon": (
        4888,
        "9d137cfafa7fee7e3844f2cb4846f56e6096fb7fa301bf389e9fec611305279a",
    ),
}

ORACLE_PREFIX_OFFSET = 16
ORACLE_PREFIX = bytes(
    [0x01]
    + [0x00] * 15
    + [0x07, 0x07, 0x00]
    + list(b"central")
    + [0x00, 0x00, 0x03, 0x03, 0x0B, 0x09, 0x03, 0x03, 0x00, 0x00, 0x01, 0x01]
)


def fail(message: str) -> None:
    print(f"Validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def fingerprint(path: Path) -> tuple[int, str]:
    data = path.read_bytes()
    return len(data), hashlib.sha256(data).hexdigest()


def validate_fingerprint(path: Path) -> None:
    expected = EXPECTED_FINGERPRINTS.get(path.name)
    if expected is None:
        return

    actual = fingerprint(path)
    if actual != expected:
        fail(
            f"{path.name} fingerprint mismatch. "
            f"expected size={expected[0]} sha256={expected[1]}, "
            f"got size={actual[0]} sha256={actual[1]}"
        )


def validate_oracle_layout(path: Path) -> None:
    data = path.read_bytes()
    end = ORACLE_PREFIX_OFFSET + len(ORACLE_PREFIX)
    if len(data) < end:
        fail(f"{path.name} is too short ({len(data)} bytes)")

    actual_prefix = data[ORACLE_PREFIX_OFFSET:end]
    if actual_prefix != ORACLE_PREFIX:
        actual_hex = actual_prefix.hex(" ")
        expected_hex = ORACLE_PREFIX.hex(" ")
        fail(
            f"{path.name} has a non-wasm32 special-level header/layout.\n"
            f"expected bytes @{ORACLE_PREFIX_OFFSET}: {expected_hex}\n"
            f"actual   bytes @{ORACLE_PREFIX_OFFSET}: {actual_hex}"
        )

    if data[32] != 7:
        fail(f"{path.name} room count landed at byte 32 as {data[32]}, expected 7")

    if data[35:42] != b"central":
        fail(f"{path.name} first room name is not aligned at the wasm32 offset")


def print_fingerprints(dat_dir: Path) -> None:
    for name in ("oracle.lev", "dungeon"):
        path = dat_dir / name
        size, digest = fingerprint(path)
        print(f"{name}: size={size} sha256={digest}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate generated Slash'EM WASM data artifacts."
    )
    parser.add_argument("dat_dir", nargs="?", default="slashem-0.0.7E7F3/dat")
    parser.add_argument(
        "--print-fingerprints",
        action="store_true",
        help="Print current artifact sizes and sha256 fingerprints.",
    )
    args = parser.parse_args()

    dat_dir = Path(args.dat_dir)
    oracle = dat_dir / "oracle.lev"
    dungeon = dat_dir / "dungeon"

    for path in (oracle, dungeon):
        if not path.is_file():
            fail(f"missing generated artifact: {path}")

    if args.print_fingerprints:
        print_fingerprints(dat_dir)
        return 0

    validate_oracle_layout(oracle)
    validate_fingerprint(oracle)
    validate_fingerprint(dungeon)
    print("Validated wasm32 level artifacts: oracle.lev layout is 32-bit compatible.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
