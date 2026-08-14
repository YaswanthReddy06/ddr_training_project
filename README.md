# DDR-style delay-line training co-simulation

**📄 [Read the full project explainer](https://yaswanthreddy06.github.io/ddr_training_project/)** —
architecture diagrams, the training algorithm walked through with real
numbers, waveform reading, and both bugs traced with actual log output.
(GitHub's file viewer only shows HTML source, not the rendered page —
that link is the live rendered version.)

A small, real, working demonstration of the exact pattern described in
Cadence's R55225 job description: **bare-metal-style C firmware driving
real Verilog RTL through simulation, before any silicon exists**, using
a binary-search training algorithm and validated against an independent
Python golden model.

This mirrors one slice of DDR PHY read/write "eye centering" training:
sweeping a delay setting to find the widest passing window, then
picking its center.

## Architecture

```
 training_fw (C)                    tb_top.v (Verilog, via vvp)
 ------------------                  ---------------------------
 binary-search training  --req_pipe-->  drives delay_line.v,
 algorithm                              registers pass/fail
                          <--resp_pipe--
```

- **`delay_line.v`** — the "hardware": a registered comparator modeling
  a programmable delay tap with a pass/fail window `[lo, hi]`. This
  stands in for the analog PHY's actual timing margin.
- **`tb_top.v`** — the testbench harness. Loads the hidden window from
  `window_config.txt`, then serves requests from firmware over two
  named pipes (FIFOs), one setting at a time, with a real one-cycle
  register latency. Dumps `training.vcd` for waveform inspection.
- **`training_fw.c`** — the "firmware": binary-search-for-edges
  algorithm (find left edge, find right edge, center = midpoint).
  Talks to hardware through exactly one function, `query_pass()` —
  the seam where a real register-mapped driver would plug in instead.
- **`golden_model.py`** — independent reference. Given the same hidden
  window, computes the correct center directly (it doesn't search for
  anything — it already "knows" the answer), and is used to grade
  firmware's result.
- **`run_all.py`** — orchestrator. Runs the whole loop across 7
  different hidden windows (including edge cases at the range
  boundaries) without recompiling anything, and reports PASS/FAIL
  against the golden model for each.

## How to run it

```bash
iverilog -g2005 -o sim.vvp delay_line.v tb_top.v   # compile the RTL
python3 run_all.py                                  # run all 7 configs
```

Or run a single configuration with hardware-side debug logging:

```bash
./run_single.sh <lo> <hi> <tag>
# e.g.: ./run_single.sh 10 22 mytest
```

## Result

All 7 configurations pass, matching the golden model exactly (the last
two are the regression configs added after bug #2 below — single-tap
windows sitting exactly on each end of the range):

| Hidden window | Firmware found | Center | Hardware queries |
|---|---|---|---|
| [10, 22] | [10, 22] | 16 | 9 |
| [5, 9]   | [5, 9]   | 7  | 21 |
| [20, 30] | [20, 30] | 25 | 20 |
| [0, 3]   | [0, 3]   | 1  | 31 |
| [27, 31] | [27, 31] | 29 | 33 |
| [0, 0]   | [0, 0]   | 0  | 36 |
| [31, 31] | [31, 31] | 31 | 38 |

Note the query-count pattern: the middle-of-range config (9 queries) is
far cheaper than the edge-of-range configs (33-38 queries). That's
because the initial "find any passing point" step does a linear probe
outward from the range center — cheap when the window is near the
middle, expensive when it's near an edge. See `KNOWN_LIMITATIONS.md`
for why, and what a better implementation would do.

## Bug #1, found and fixed: the RTL off-by-one

While building this, `delay_line.v`'s comparator was written as:

```verilog
pass <= (delay_setting > lo) && (delay_setting <= hi);   // BUG: off-by-one
```

instead of:

```verilog
pass <= (delay_setting >= lo) && (delay_setting <= hi);  // correct
```

This is a genuine, easy-to-make off-by-one on an inclusive lower bound.
Running the full suite against this version, **all 5 configs failed** —
every reported left edge was exactly one higher than the golden model
expected (e.g. hidden window `[10,22]` was trained as `[11,22]`).

**Triage process** (same pattern as Chapter 4 of the technical study
guide — is it a firmware bug or an RTL bug?):

1. Reproduced deterministically — same config, same wrong answer every
   time.
2. Checked the firmware-side query log at the failing boundary:
   `FW:   query delay=10 -> fail`
3. Cross-referenced against the hardware-side log for the *same* query:
   `TB:   received delay_setting=10 -> pass=0  (window is [10,22])`
4. Both sides agree on what happened — firmware correctly received and
   reported what the hardware said. That rules out a firmware or
   pipe-communication bug. The hardware itself computed the wrong
   answer for a value that, per spec, should have passed.
5. Conclusion: RTL bug in the comparator, not a firmware bug. Fixed the
   operator, rebuilt, reran — `delay=10` now correctly returns `pass=1`
   and all 5 configs pass.

This is the exact "is it firmware or RTL" triage skill the interview
JD calls out directly.

## Bug #2, found and fixed: tap 31 was unreachable

A second, purely algorithmic bug turned up in `find_any_passing_value()`
in `training_fw.c` — the linear probe that finds an initial known-good
setting before the binary-search edges can run:

```c
// BUG: loop bound rounds toward the low side, never reaching the top of the range
static int find_any_passing_value(int lo, int hi) {
    int mid = lo + (hi - lo) / 2;
    for (int offset = 0; offset <= (hi - lo) / 2; offset++) {
        if (mid - offset >= lo && query_pass(mid - offset)) return mid - offset;
        if (mid + offset <= hi && query_pass(mid + offset)) return mid + offset;
    }
    return -1;
}
```

For the full range `[0, 31]`, `mid = 15` and the loop bound is
`(31-0)/2 = 15` (integer division truncates). The farthest tap this
could ever reach on the high side was `mid + 15 = 30` — **tap 31 was
structurally unreachable**, no matter how many offsets ran. A hidden
window whose only valid setting was `31` (e.g. `[31,31]`) would make
firmware report `TRAINING FAILED - no passing setting found in range`,
even though `31` is a perfectly legal delay setting.

**Triage** (same checklist as bug #1, opposite conclusion): reproduced
deterministically with `window_config.txt = "31 31"` → checked
firmware's log (never sends `delay=31`) → checked hardware's log (never
receives `delay_setting=31`) → both sides agree, and what they agree on
is that the query never happened. That rules out an RTL bug — the
comparator was never given the chance to answer wrong. The bug was
firmware's search-loop bound, not the hardware.

**Notably, this bug didn't show up in the original 5-config suite** —
`[27,31]` happens to find a passing value (27) before the probe runs out
of budget, so none of the original configs exercised the exact failure
mode. It was only caught by reasoning about the loop's coverage
directly.

The fix:

```c
// FIX: size the bound off the larger of the two distances to each end of the range
static int find_any_passing_value(int lo, int hi) {
    int mid = lo + (hi - lo) / 2;
    int max_offset = (mid - lo > hi - mid) ? (mid - lo) : (hi - mid);
    for (int offset = 0; offset <= max_offset; offset++) {
        if (mid - offset >= lo && query_pass(mid - offset)) return mid - offset;
        if (mid + offset <= hi && query_pass(mid + offset)) return mid + offset;
    }
    return -1;
}
```

Verified independently of `iverilog` by porting the exact algorithm to
Python and running it against **all 528 possible `[lo, hi]` windows**
in `[0, 31]` — not just the 5 in the regular suite. Before the fix:
exactly 1 mismatch (`[31,31]`). After the fix: 0 mismatches. Two
regression configs, `(0, 0)` and `(31, 31)`, were added to `run_all.py`
so the standard suite catches this class of bug going forward.

## Files in this project

- `delay_line.v` — the DUT (fixed, correct version)
- `delay_line_KNOWN_GOOD.v.bak` — backup of the correct RTL (kept so
  the bug/fix story above can be reproduced)
- `tb_top.v` — testbench / co-simulation harness
- `training_fw.c` — the firmware (compile with `gcc -O2 -o training_fw training_fw.c`)
- `golden_model.py` — independent reference model
- `run_all.py` — orchestrator, runs all 7 configs and grades them
- `run_single.sh` — runs one config with hardware-side debug logging
- `window_config.txt` — written fresh by each run; not meant to be edited by hand
- `training.vcd` — waveform from the most recent run (open with GTKWave)
- `KNOWN_LIMITATIONS.md` — honest notes on what a production version would do differently
