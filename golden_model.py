#!/usr/bin/env python3
"""
golden_model.py

Independent "known-correct" reference. It is NOT part of the thing being
tested (firmware + RTL) -- it re-derives the expected answer directly
from the same hidden window parameters, the same way a verification
engineer's golden model checks a design without sharing any code with it.

Usage:
    python3 golden_model.py <lo> <hi>
Prints the true window and center, same format the orchestration script
compares against training_fw's reported result.
"""
import sys


def true_center(lo: int, hi: int) -> int:
    """The window [lo, hi] is fully known here (unlike firmware, which
    has to discover it), so the correct center is just arithmetic."""
    return lo + (hi - lo) // 2


def main():
    if len(sys.argv) != 3:
        print("usage: golden_model.py <lo> <hi>", file=sys.stderr)
        sys.exit(1)

    lo, hi = int(sys.argv[1]), int(sys.argv[2])
    center = true_center(lo, hi)
    print(f"GOLDEN left={lo} right={hi} center={center}")


if __name__ == "__main__":
    main()
