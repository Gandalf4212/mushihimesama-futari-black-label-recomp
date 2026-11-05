@echo off
rem Polls a work-RAM word every few microseconds of emulated time and logs its changes to flags.txt.
rem Put flaglog.lua, inputs.lua and the input script in the MAME folder; delete nvram\futaribl first.
rem Usage: flaglog.bat <input_script> [first_frame] [last_frame] [addr_hex] [poll_us]
rem        (defaults 150 420 0C1DE144 5)
set CAP_INPUT=%1
set FLAG_FIRST=%2
set FLAG_LAST=%3
set FLAG_ADDR=%4
set POLL_US=%5
if "%FLAG_FIRST%"=="" set FLAG_FIRST=150
if "%FLAG_LAST%"=="" set FLAG_LAST=420
if "%FLAG_ADDR%"=="" set FLAG_ADDR=0C1DE144
if "%POLL_US%"=="" set POLL_US=5
mame futaribl -window -norotate -nothrottle -skip_gameinfo -sound none -autoboot_script flaglog.lua
echo Flag log: flags.txt
