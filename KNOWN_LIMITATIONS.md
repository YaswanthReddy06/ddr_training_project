# Known limitations (and what a production version would do differently)

Being able to name these honestly is itself a good interview answer to
"what would you improve with more time" — pretending a small project has
no rough edges is less convincing than knowing exactly where they are.

1. **The initial "find any passing point" step is a linear probe, not a
   binary search.** `find_any_passing_value()` in `training_fw.c` walks
   outward from the middle of the whole range until it hits a passing
   value. For a window near the center of the range this is cheap (a
   handful of queries); for a window near the edge of the range it costs
   up to ~O(n) queries, which shows up directly in the results table
   (9 queries for a centered window vs. 31-33 for edge-of-range windows).
   A real implementation would likely either (a) start from a
   last-known-good value stored from the previous boot rather than the
   range midpoint, or (b) use a coarse-then-fine two-pass search. This
   project intentionally left the naive version in so the tradeoff is
   visible and discussable.

2. **One-dimensional, not two-dimensional.** Real DDR5 read/write
   centering sweeps delay *and* voltage reference together (2D training).
   This project only models the time axis, to keep the co-simulation
   plumbing (the actual point of the exercise) simple enough to build
   in a day.

3. **The "hardware" is a single registered comparator, not a real delay
   chain.** `delay_line.v` models the *outcome* of a pass/fail decision
   directly, rather than an actual chain of programmable delay elements
   feeding a phase comparator. This was a deliberate simplification —
   the goal was a real, working C-to-Verilog co-simulation loop with a
   genuine one-cycle hardware latency, not a physically accurate PHY.

4. **No per-bit deskew or multi-lane modeling.** Real DDR training
   handles multiple data bits independently after coarse centering. This
   project trains exactly one "lane."

5. **The FIFO-based handshake is a stand-in for a register interface.**
   Real firmware would talk to hardware via memory-mapped registers
   (see the Technical Study Guide, Chapter 3.1), not named pipes. Named
   pipes were used here because they give real, blocking, unbuffered
   synchronization between two separate OS processes without needing a
   full instruction-set simulator or DPI-C bridge — a pragmatic choice
   for a one-day project, not a claim that real firmware works this way.

6. **No timeout/retry logic.** A real training algorithm bounds every
   search with a maximum iteration count, in case hardware never
   reports a sane result (see Technical Study Guide, Chapter 3.2 —
   "defensive coding"). This version assumes the hardware always
   responds, which is fine for a simulation you control but would be a
   real robustness gap in production firmware.
