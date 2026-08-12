/* training_fw.c
 *
 * This is "firmware" in the same sense as the rest of the interview
 * prep: bare-metal-style C that owns a training loop and talks to real
 * hardware through a register-like interface. Here the "hardware" is
 * the Verilog simulation in tb_top.v, and the "registers" are two named
 * pipes standing in for a memory-mapped interface.
 *
 * Algorithm: binary-search-for-edges, exactly the pattern described in
 * the Technical Study Guide (Chapter 3) and used by real DDR PHY
 * read/write centering firmware -- find the left edge of the passing
 * window, find the right edge, then the trained value is the midpoint.
 * This is O(log n) queries per edge instead of O(n) for a full sweep,
 * which is what makes it fast enough to run at every boot.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define MIN_DELAY 0
#define MAX_DELAY 31
#define QUIT_CODE 255

static FILE *req_fp  = NULL;
static FILE *resp_fp = NULL;
static int   query_count = 0;

/* query_pass(): the one function that actually talks to the hardware.
 * Writes a candidate delay setting to the request pipe, blocks until
 * the Verilog testbench replies with pass (1) or fail (0). This is the
 * exact seam described in the hands-on project doc where a real
 * register-mapped implementation would plug in instead. */
static int query_pass(int delay) {
    fprintf(req_fp, "%d\n", delay);
    fflush(req_fp);

    int result = -1;
    if (fscanf(resp_fp, "%d", &result) != 1) {
        fprintf(stderr, "FW: FATAL - lost sync with hardware at delay=%d\n", delay);
        exit(1);
    }
    query_count++;
    fprintf(stderr, "FW:   query delay=%2d -> %s\n", delay, result ? "PASS" : "fail");
    return result;
}

/* find_left_edge(): binary search for the first passing value in
 * [lo, hi], given that we already know something in [lo, hi] passes.
 * Standard "find first true in a sorted false/true sequence" search. */
static int find_left_edge(int lo, int hi) {
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (query_pass(mid)) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

/* find_right_edge(): mirror image of find_left_edge -- binary search
 * for the last passing value. */
static int find_right_edge(int lo, int hi) {
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (query_pass(mid)) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

/* find_any_passing_value(): before we can binary-search for edges, we
 * need at least one known-passing point to search from. A real PHY
 * training algorithm typically starts from a "safe" default (often the
 * middle of the range) and widens out if that fails. We do the same:
 * a small linear probe outward from the center, bounded so it can never
 * hang forever if the whole range somehow fails. */
static int find_any_passing_value(int lo, int hi) {
    int mid = lo + (hi - lo) / 2;
    /* mid is closer to lo than to hi whenever (hi - lo) is odd (integer
     * division above rounds down), so bounding the probe by (hi - lo) / 2
     * under-covers the hi side by one -- delay_setting == hi is never
     * tried. Use the larger of the two distances so both ends of the
     * range are fully covered. */
    int max_offset = (mid - lo > hi - mid) ? (mid - lo) : (hi - mid);
    for (int offset = 0; offset <= max_offset; offset++) {
        if (mid - offset >= lo && query_pass(mid - offset)) return mid - offset;
        if (mid + offset <= hi && query_pass(mid + offset)) return mid + offset;
    }
    return -1; /* nothing in range passes -- training has genuinely failed */
}

int main(int argc, char **argv) {
    const char *req_pipe_path  = "req_pipe";
    const char *resp_pipe_path = "resp_pipe";

    fprintf(stderr, "FW: connecting to hardware...\n");

    /* Open order matters for FIFOs: we open req_pipe for WRITE and
     * resp_pipe for READ. Each open() blocks until the Verilog side has
     * opened the matching end -- this is what keeps firmware and
     * hardware in lockstep with no polling or sleep() calls needed. */
    req_fp = fopen(req_pipe_path, "w");
    if (!req_fp) { perror("fopen req_pipe"); return 1; }

    resp_fp = fopen(resp_pipe_path, "r");
    if (!resp_fp) { perror("fopen resp_pipe"); return 1; }

    fprintf(stderr, "FW: connected. Starting training sequence.\n");

    int known_good = find_any_passing_value(MIN_DELAY, MAX_DELAY);
    if (known_good < 0) {
        fprintf(stderr, "FW: TRAINING FAILED - no passing setting found in range\n");
        fprintf(req_fp, "%d\n", QUIT_CODE);
        fflush(req_fp);
        return 2;
    }
    fprintf(stderr, "FW: found initial passing point at delay=%d\n", known_good);

    int left  = find_left_edge(MIN_DELAY, known_good);
    int right = find_right_edge(known_good, MAX_DELAY);
    int center = left + (right - left) / 2;

    fprintf(stderr, "FW: window = [%d, %d], trained center = %d\n", left, right, center);
    fprintf(stderr, "FW: total hardware queries used = %d\n", query_count);

    /* tell the hardware side we're done */
    fprintf(req_fp, "%d\n", QUIT_CODE);
    fflush(req_fp);

    /* machine-readable result line for the orchestration script */
    printf("RESULT left=%d right=%d center=%d queries=%d\n", left, right, center, query_count);

    fclose(req_fp);
    fclose(resp_fp);
    return 0;
}
