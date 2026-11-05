@echo off
rem Logs the emulated time of every blitter kick to kicks.txt. Put kicklog.lua, inputs.lua and the
rem input script in the MAME folder; delete nvram\futaribl first.
rem Usage: kicklog.bat <input_script> [last_frame]
set CAP_INPUT=%1
set KICK_LAST=%2
if "%KICK_LAST%"=="" set KICK_LAST=600
mame futaribl -window -norotate -nothrottle -skip_gameinfo -sound none -autoboot_script kicklog.lua
echo Blitter kick log: kicks.txt
