// delay_line.v
//
// This models ONE slice of what a real DDR PHY does during read/write
// "eye" centering: a programmable delay tap feeding a comparator.
// In real hardware, the "pass" region is determined by physical timing
// margins (trace length, silicon process variation, temperature).
// Here, we model that with a simple parameterized window [LO, HI] on a
// 5-bit delay code (0-31), which is exactly how a real PHY's delay-tap
// pass/fail region behaves from firmware's point of view: a black box
// that says PASS or FAIL for a given setting.

module delay_line (
    input  wire        clk,
    input  wire [4:0]  delay_setting,   // the value firmware is trying
    input  wire [4:0]  lo,              // low edge of the passing window
    input  wire [4:0]  hi,              // high edge of the passing window
    output reg         pass             // registered pass/fail result
);

    // Real hardware would compare the delayed signal against a sampling
    // window using an actual delay chain + phase comparator. We model
    // the *outcome* of that comparison directly, registered on the
    // clock edge so behavior is realistic pipeline-latency-wise (there IS
    // a one-cycle delay between applying a setting and seeing the result,
    // just like real hardware).
    always @(posedge clk) begin
        pass <= (delay_setting >= lo) && (delay_setting <= hi);
    end

endmodule
