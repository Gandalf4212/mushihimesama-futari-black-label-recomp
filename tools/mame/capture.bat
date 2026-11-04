@echo off
rem Reference frame capture for frame_check.py. Put this file and capture.lua in your MAME folder.
rem Usage: capture.bat [from] [to] [every] [input_script]
set CAP_FROM=%1
set CAP_TO=%2
set CAP_EVERY=%3
set CAP_INPUT=%4
if "%CAP_FROM%"=="" set CAP_FROM=1500
if "%CAP_TO%"=="" set CAP_TO=2100
if "%CAP_EVERY%"=="" set CAP_EVERY=1
if "%CAP_INPUT%"=="" set CAP_INPUT=
mame futaribl -window -norotate -nothrottle -skip_gameinfo -sound none -snapshot_directory snaps -autoboot_script capture.lua
echo Snapshots are in snaps\futaribl\
