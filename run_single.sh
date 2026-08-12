#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

LO="$1"
HI="$2"
TAG="${3:-single}"

rm -f req_pipe resp_pipe
mkfifo req_pipe resp_pipe
echo "$LO $HI" > window_config.txt

vvp sim.vvp > "tb_debug_${TAG}.txt" 2>&1 &
VVP_PID=$!

./training_fw > "fw_debug_stdout_${TAG}.txt" 2> "fw_debug_stderr_${TAG}.txt"

wait "$VVP_PID"
echo "done: tb_debug_${TAG}.txt / fw_debug_stderr_${TAG}.txt"
