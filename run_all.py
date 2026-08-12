#!/usr/bin/env python3
"""
run_all.py

Runs the full firmware + Verilog co-simulation loop across several
different hidden "chip" configurations -- exactly like a verification
team sweeping process/voltage/temperature corners against the same RTL
and firmware binary, without recompiling anything.

For each config:
  1. Write window_config.txt (the hidden pass/fail window for this run)
  2. Launch the Verilog simulation (vvp) in the background
  3. Run the real training_fw C binary against it over the FIFOs
  4. Compare training_fw's result against golden_model's independent answer
  5. Report PASS/FAIL

This is the single script to run for the whole project demo.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from golden_model import true_center

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
REQ_PIPE = os.path.join(PROJECT_DIR, "req_pipe")
RESP_PIPE = os.path.join(PROJECT_DIR, "resp_pipe")
VCD_FILE = os.path.join(PROJECT_DIR, "training.vcd")

# Five "chips": deliberately includes edge cases (window touching 0 and 31)
# in addition to comfortable middle-of-range windows.
CONFIGS = [
    (10, 22),
    (5, 9),
    (20, 30),
    (0, 3),
    (27, 31),
]


def fresh_pipes():
    for p in (REQ_PIPE, RESP_PIPE):
        if os.path.exists(p):
            os.remove(p)
        os.mkfifo(p)


def run_one_config(lo, hi, tag):
    print(f"\n{'=' * 60}")
    print(f"CONFIG {tag}: hidden window = [{lo}, {hi}]")
    print(f"{'=' * 60}")

    fresh_pipes()
    with open(os.path.join(PROJECT_DIR, "window_config.txt"), "w") as f:
        f.write(f"{lo} {hi}\n")

    # 1. start the "hardware" (Verilog simulation) in the background
    vvp_log = open(os.path.join(PROJECT_DIR, f"tb_log_{tag}.txt"), "w")
    vvp_proc = subprocess.Popen(
        ["vvp", "sim.vvp"], cwd=PROJECT_DIR, stdout=vvp_log, stderr=subprocess.STDOUT
    )

    # 2. run the real firmware binary against it
    fw_proc = subprocess.run(
        ["./training_fw"], cwd=PROJECT_DIR, capture_output=True, text=True, timeout=30
    )
    print(fw_proc.stderr, end="")  # firmware's live query log

    vvp_proc.wait(timeout=10)
    vvp_log.close()

    # 3. parse firmware's reported result
    result_line = next((l for l in fw_proc.stdout.splitlines() if l.startswith("RESULT")), None)
    if not result_line:
        print("FAIL: firmware produced no RESULT line (see tb_log for details)")
        return False

    parts = dict(kv.split("=") for kv in result_line.split()[1:])
    fw_left, fw_right, fw_center, queries = (
        int(parts["left"]), int(parts["right"]), int(parts["center"]), int(parts["queries"])
    )

    # 4. compare against the independent golden model
    expected_center = true_center(lo, hi)

    print(f"\nFirmware found:  window=[{fw_left},{fw_right}]  center={fw_center}  ({queries} hardware queries)")
    print(f"Golden expects:  window=[{lo},{hi}]  center={expected_center}")

    ok = (fw_left == lo) and (fw_right == hi) and (fw_center == expected_center)
    print("RESULT: " + ("PASS ✓" if ok else "FAIL ✗ -- mismatch against golden model"))
    return ok


def main():
    os.chdir(PROJECT_DIR)
    all_pass = True
    for i, (lo, hi) in enumerate(CONFIGS, start=1):
        ok = run_one_config(lo, hi, tag=str(i))
        all_pass = all_pass and ok

    print(f"\n{'#' * 60}")
    print("OVERALL: " + ("ALL CONFIGS PASSED ✓" if all_pass else "AT LEAST ONE CONFIG FAILED ✗"))
    print(f"{'#' * 60}")
    print(f"\nWaveform for the last run saved to: {VCD_FILE}")
    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
