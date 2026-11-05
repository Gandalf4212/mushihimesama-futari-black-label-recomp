-- MAME autoboot script: applies an input script and saves reference snapshots.
-- Needs inputs.lua next to it.
-- Usage (from a MAME 0.289+ folder that has the futaribl set in roms/):
--   mame futaribl -window -norotate -nothrottle -skip_gameinfo -snapshot_directory snaps
--        -autoboot_script capture.lua
-- Configure with environment variables (or edit the defaults below):
--   CAP_INPUT  = path to input script (same format as run_headless --input), optional
--   CAP_FROM, CAP_TO, CAP_EVERY = frame range and step for snapshots (MAME frame numbers)
--   CAP_EXIT   = frame at which MAME quits
-- Snapshots: snaps/futaribl/f_<frame>.png  (raw 320x240 buffer because of -norotate)
local FROM  = tonumber(os.getenv("CAP_FROM")  or "1500")
local TO    = tonumber(os.getenv("CAP_TO")    or "2100")
local EVERY = tonumber(os.getenv("CAP_EVERY") or "1")
local EXIT  = tonumber(os.getenv("CAP_EXIT")  or tostring(TO + 1))

local nevents = dofile("inputs.lua")   -- shared input handling

local screen
local function on_frame()
  screen = screen or manager.machine.screens[":screen"]
  local fr = screen:frame_number()
  apply_inputs(fr)
  if fr >= FROM and fr <= TO and (fr - FROM) % EVERY == 0 then
    screen:snapshot(string.format("f_%05d.png", fr))
  end
  if fr >= EXIT then manager.machine:exit() end
end

local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.capture_sub = add(on_frame)
print(string.format("capture.lua: snapshots %d..%d every %d, exit at %d, %d input events", FROM, TO, EVERY, EXIT, nevents))
