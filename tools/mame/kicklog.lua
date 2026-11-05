-- MAME autoboot script: logs the emulated time of every blitter kick (write of bit 0 to the blitter
-- execute register, CPU 0xB8000004 = physical 0x18000004) to kicks.txt as "<frame> <seconds>".
-- Same format as `run_headless --kick-log`; compare with tools/kick_diff.py. Run via kicklog.bat.
-- Needs inputs.lua next to it (applies the same input script as capture.lua).
local last = tonumber(os.getenv("KICK_LAST") or "600")
local n = dofile("inputs.lua")
local screen, out
local function now()
  local t = manager.machine.time
  return t.seconds + t.attoseconds / 1e18
end
local function install()
  out = io.open("kicks.txt", "w")
  local space = manager.machine.devices[":maincpu"].spaces["program"]
  _G.kick_tap = space:install_write_tap(0x18000004, 0x18000007, "kicklog", function(offset, data, mask)
    if (data & mask & 1) ~= 0 then
      out:write(string.format("%d %.9f\n", screen:frame_number(), now()))
    end
  end)
end
local function on_frame()
  screen = screen or manager.machine.screens[":screen"]
  if not out then install() end   -- first frame: long before the game's first blitter kick (~frame 150)
  local fr = screen:frame_number()
  apply_inputs(fr)
  if fr >= last then out:close(); manager.machine:exit() end
end
local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.kicklog_sub = add(on_frame)
print("kicklog.lua: " .. n .. " input events, logging blitter kicks up to frame " .. last)
