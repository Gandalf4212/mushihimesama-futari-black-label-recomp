@echo off
rem Statistical PC profile (256-byte code regions) to pcprof.txt. Put pclog.lua, inputs.lua and the
rem input script in the MAME folder; delete nvram\futaribl first.
rem Usage: pclog.bat <input_script> [first_frame] [last_frame] [poll_us]   (defaults 200 400 5)
rem Set FLAG_IPRA0=1 before running to disable the on-chip timer interrupts from frame 150.
set CAP_INPUT=%1
set PC_FIRST=%2
set PC_LAST=%3
set POLL_US=%4
if "%PC_FIRST%"=="" set PC_FIRST=200
if "%PC_LAST%"=="" set PC_LAST=400
if "%POLL_US%"=="" set POLL_US=5
mame futaribl -window -norotate -nothrottle -skip_gameinfo -sound none -autoboot_script pclog.lua
echo PC profile: pcprof.txt
