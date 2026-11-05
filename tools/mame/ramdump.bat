@echo off
rem RAM dumps for debugging divergence. Put ramdump.lua, inputs.lua and the input script in the MAME folder.
rem Usage: ramdump.bat <input_script> <frame,frame,...>
set CAP_INPUT=%1
set DUMP_FRAMES=%2
mame futaribl -window -norotate -nothrottle -skip_gameinfo -sound none -autoboot_script ramdump.lua
echo RAM dumps: ram_*.bin
