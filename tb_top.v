// tb_top.v
//
// This is the "hardware side" of the co-simulation link described in the
// Cadence prep materials: real firmware (a separate C process) drives
// real Verilog hardware (delay_line.v) through this testbench, over two
// named pipes (FIFOs) that stand in for the register interface a real
// embedded core would use to talk to PHY hardware.
//
// Protocol (all lines are plain decimal integers, one per line):
//   firmware -> hardware (req_pipe):  a delay code 0-31, or 255 = "quit"
//   hardware -> firmware (resp_pipe): 1 (pass) or 0 (fail)
//
// The hidden pass/fail window [LO, HI] is read once at time 0 from
// window_config.txt, so we can test several different "chips" without
// recompiling anything -- just like a real verification team sweeps
// process/voltage/temperature corners against the same RTL.

`timescale 1ns/1ps

module tb_top;

    reg         clk = 0;
    reg  [4:0]  delay_setting = 0;
    reg  [4:0]  lo = 0;
    reg  [4:0]  hi = 31;
    wire        pass;

    integer     cfg_fd, req_fd, resp_fd;
    integer     code, got;

    delay_line dut (
        .clk(clk),
        .delay_setting(delay_setting),
        .lo(lo),
        .hi(hi),
        .pass(pass)
    );

    // free-running clock
    always #5 clk = ~clk;

    initial begin
        // waveform dump for post-mortem debugging (Chapter 4 triage flow)
        $dumpfile("training.vcd");
        $dumpvars(0, tb_top);

        // 1. read the hidden window for this "chip" / test config
        cfg_fd = $fopen("window_config.txt", "r");
        if (cfg_fd == 0) begin
            $display("TB: FATAL - could not open window_config.txt");
            $finish;
        end
        got = $fscanf(cfg_fd, "%d %d", lo, hi);
        $fclose(cfg_fd);
        $display("TB: hidden window loaded  LO=%0d HI=%0d", lo, hi);

        // 2. open the two FIFOs. These calls BLOCK until the firmware
        //    process (training_fw) opens the other end -- this is the
        //    real synchronization mechanism, no polling/sleeping needed.
        req_fd  = $fopen("req_pipe", "r");
        resp_fd = $fopen("resp_pipe", "w");
        $display("TB: pipes connected, ready for firmware");

        // 3. serve requests until firmware sends 255 ("quit")
        forever begin
            got = $fscanf(req_fd, "%d", code);
            if (got <= 0) begin
                $display("TB: request pipe closed, exiting");
                $finish;
            end
            if (code == 255) begin
                $display("TB: got quit, exiting");
                $fclose(req_fd);
                $fclose(resp_fd);
                $finish;
            end
            delay_setting = code[4:0];

            // let the registered comparator see this setting
            @(posedge clk);
            #1; // let the non-blocking update settle

            $fdisplay(resp_fd, "%0d", pass);
            $fflush(resp_fd);
            $display("TB:   received delay_setting=%0d -> pass=%0d  (window is [%0d,%0d])", delay_setting, pass, lo, hi);
        end
    end

endmodule
