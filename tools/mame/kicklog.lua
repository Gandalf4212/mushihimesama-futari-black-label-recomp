-- MAME autoboot script: logs the emulated time of every blitter kick (write of bit 0 to the blitter
-- execute register, CPU 0xB8000004 = physical 0x18000004) to kicks.txt as "<frame> <seconds>".
-- Same format as `run_headless --kick-log`; compare with tools/kick_diff.py. Run via kicklog.bat.
-- Needs inputs.lua next to it (applies the same input script as capture.lua).
local last = tonumber(os.getenv("KICK_LAST") or "600")
local n = dofile("inputs.lua")
local screen, out, space
local writes, kicks, err, raw = 0, 0, nil, 0

local function now()
  local t = manager.machine.time
  local ok, v = pcall(function() return t.seconds + t.attoseconds / 1e18 end)
  if ok then return v end
  return t:as_double()
end

local function on_write(offset, data, mask)
  writes = writes + 1
  if raw < 8 then
    raw = raw + 1
    print(string.format("kicklog.lua: raw write offset %08x data %016x mask %016x", offset, data, mask))
  end
  -- 64-bit big-endian bus: a 32-bit write to 0x18000004 arrives at offset 0x18000000 (or 0x18000004)
  -- with the mask selecting the low word.
  local word, v
  if space.data_width == 64 then
    local base = offset & ~7
    if (mask & 0xFFFFFFFF) ~= 0 then word, v = base + 4, data & 0xFFFFFFFF
    else word, v = base, (data >> 32) & 0xFFFFFFFF end
  else
    word, v = offset, data & mask
  end
  if word == 0x18000004 and (v & 1) ~= 0 then
    kicks = kicks + 1
    out:write(string.format("%d %.9f\n", screen:frame_number(), now()))
    out:flush()
  end
end

local function install()
  out = io.open("kicks.txt", "w")
  space = manager.machine.devices[":maincpu"].spaces["program"]
  print(string.format("kicklog.lua: program space data width %d bits", space.data_width))
  _G.kick_tap = space:install_write_tap(0x18000000, 0x1800007F, "kicklog", function(offset, data, mask)
    if err then return end
    local ok, e = pcall(on_write, offset, data, mask)
    if not ok then err = tostring(e); print("kicklog.lua: ERROR in tap: " .. err) end
  end)
  print("kicklog.lua: tap installed")
end

local function on_frame()
  local ok, e = pcall(function()
    screen = screen or manager.machine.screens[":screen"]
    local fr = screen:frame_number()
    -- install at frame 50: after the driver installs the blitter handlers at reset, long before the
    -- game's first blitter kick (~frame 155)
    if not out and fr >= 50 then install() end
    apply_inputs(fr)
    if fr % 50 == 0 then print(string.format("kicklog.lua: frame %d, blitter writes %d, kicks %d", fr, writes, kicks)) end
    if fr >= last then
      if out then out:close() end
      manager.machine:exit()
    end
  end)
  if not ok then print("kicklog.lua: ERROR in frame callback: " .. tostring(e)) end
end

local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.kicklog_sub = add(on_frame)
print("kicklog.lua: " .. n .. " input events, logging blitter kicks up to frame " .. last)
