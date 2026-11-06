@echo off
rem Instruction trace with MAME's cycle counter, for exact per-instruction timing comparisons.
rem Runs MAME with the debugger, executes trace_irq.cmd (run to 3830 ms = ~frame 230, trace 5 ms of
rem emulated time to trace.txt with totalcycles before every instruction, then quit).
rem Put trace_irq.cmd in the MAME folder and delete nvram\futaribl first. The debugger window opens;
rem if it does not close by itself after the trace, close MAME.
mame futaribl -window -norotate -nothrottle -skip_gameinfo -sound none -debug -debugscript trace_irq.cmd
echo Trace: trace.txt
