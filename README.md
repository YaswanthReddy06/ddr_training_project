# DDR-style delay-line training co-simulation

**📄 [Read the full project explainer](https://yaswanthreddy06.github.io/ddr_training_project/ddr_training_project_explainer.html)** —
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
- **`run_all.py`** — orchestrator. Runs the whole loop across 5
  different hidden windows (including edge cases at the range
  boundaries) without recompiling anything, and reports PASS/FAIL
  against the golden model for each.

## How to run it

```bash
iverilog -g2005 -o sim.vvp delay_line.v tb_top.v   # compile the RTL
python3 run_all.py                                  # run all 5 configs
```

Or run a single configuration with hardware-side debug logging:

```bash
./run_single.sh <lo> <hi> <tag>
# e.g.: ./run_single.sh 10 22 mytest
```

## Result

All 5 configurations pass, matching the golden model exactly:

| Hidden window | Firmware found | Center | Hardware queries |
|---|---|---|---|
| [10, 22] | [10, 22] | 16 | 9 |
| [5, 9]   | [5, 9]   | 7  | 21 |
| [20, 30] | [20, 30] | 25 | 20 |
| [0, 3]   | [0, 3]   | 1  | 31 |
| [27, 31] | [27, 31] | 29 | 33 |

Note the query-count pattern: the middle-of-range config (9 queries) is
far cheaper than the edge-of-range configs (31-33 queries). That's
because the initial "find any passing point" step does a linear probe
outward from the range center — cheap when the window is near the
middle, expensive when it's near an edge. See `KNOWN_LIMITATIONS.md`
for why, and what a better implementation would do.

## A real bug, found and fixed

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

## Files in this project

- `delay_line.v` — the DUT (fixed, correct version)
- `delay_line_KNOWN_GOOD.v.bak` — backup of the correct RTL (kept so
  the bug/fix story above can be reproduced)
- `tb_top.v` — testbench / co-simulation harness
- `training_fw.c` — the firmware (compile with `gcc -O2 -o training_fw training_fw.c`)
- `golden_model.py` — independent reference model
- `run_all.py` — orchestrator, runs all 5 configs and grades them
- `run_single.sh` — runs one config with hardware-side debug logging
- `window_config.txt` — written fresh by each run; not meant to be edited by hand
- `training.vcd` — waveform from the most recent run (open with GTKWave)
- `KNOWN_LIMITATIONS.md` — honest notes on what a production version would do differently
