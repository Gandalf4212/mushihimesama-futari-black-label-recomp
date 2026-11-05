@echo off
rem Logs work-RAM words after every frame to watch.txt. Put watchlog.lua, inputs.lua and the input
rem script in the MAME folder; delete nvram\futaribl first.
rem Usage: watchlog.bat <input_script> [last_frame] [addr,addr,...]   (default 600, 0C051040,0C002310,0C056EA0)
set CAP_INPUT=%1
set WATCH_LAST=%2
set WATCH_ADDRS=%3
if "%WATCH_LAST%"=="" set WATCH_LAST=600
mame futaribl -window -norotate -nothrottle -skip_gameinfo -sound none -autoboot_script watchlog.lua
echo Watch log: watch.txt
