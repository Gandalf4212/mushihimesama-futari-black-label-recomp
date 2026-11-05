-- MAME autoboot script: after every frame, logs a few 32-bit work-RAM words to watch.txt as
-- "<frame> <hex> <hex> ...". Same format as `run_headless --watch-log file addr,addr,...`; compare
-- with tools/watch_diff.py. Run via watchlog.bat. Needs inputs.lua next to it.
-- (A write tap on the blitter registers makes MAME 0.289 exit in this driver, so we poll RAM instead.)
local last = tonumber(os.getenv("WATCH_LAST") or "600")
local addrs = {}
for s in (os.getenv("WATCH_ADDRS") or "0C051040,0C002310,0C056EA0"):gmatch("%x+") do addrs[#addrs + 1] = tonumber(s, 16) end
local n = dofile("inputs.lua")
local screen, space, out
local function on_frame()
  local ok, e = pcall(function()
    screen = screen or manager.machine.screens[":screen"]
    space = space or manager.machine.devices[":maincpu"].spaces["program"]
    out = out or io.open("watch.txt", "w")
    local fr = screen:frame_number()
    apply_inputs(fr)
    local line = tostring(fr)
    for _, a in ipairs(addrs) do line = line .. string.format(" %08x", space:read_u32(a) & 0xFFFFFFFF) end
    out:write(line .. "\n")
    if fr % 100 == 0 then print("watchlog.lua: frame " .. fr) end
    if fr >= last then out:close(); manager.machine:exit() end
  end)
  if not ok then print("watchlog.lua: ERROR: " .. tostring(e)) end
end
local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.watchlog_sub = add(on_frame)
print("watchlog.lua: " .. n .. " input events, logging " .. #addrs .. " words per frame up to frame " .. last)
