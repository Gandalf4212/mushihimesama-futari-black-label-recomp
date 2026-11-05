-- MAME autoboot script: applies the same input script as capture.lua and dumps CV1000 work RAM
-- (8 MB at 0x0C000000) at chosen MAME frames to ram_<frame>.bin in the current folder.
-- Needs inputs.lua next to it. Run via ramdump.bat.
local frames, last = {}, 0
for s in (os.getenv("DUMP_FRAMES") or "2500"):gmatch("%d+") do local f = tonumber(s); frames[f] = true; if f > last then last = f end end
local n = dofile("inputs.lua")
local screen, space
local function on_frame()
  screen = screen or manager.machine.screens[":screen"]
  space = space or manager.machine.devices[":maincpu"].spaces["program"]
  local fr = screen:frame_number()
  apply_inputs(fr)
  if frames[fr] then
    local data = space:read_range(0x0C000000, 0x0C7FFFFF, 8)
    local fh = io.open(string.format("ram_%05d.bin", fr), "wb"); fh:write(data); fh:close()
    print("ramdump.lua: wrote ram_" .. fr .. ".bin (" .. #data .. " bytes)")
  end
  if fr >= last then manager.machine:exit() end
end
local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.ramdump_sub = add(on_frame)
print("ramdump.lua: " .. n .. " input events, dumping up to frame " .. last)
