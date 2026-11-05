-- MAME autoboot script: polls one 32-bit work-RAM word every POLL_US microseconds of emulated time
-- and logs each change to flags.txt as "<frame> <seconds> <hex value>". Same format as
-- `run_headless --change-log file addr`; compare with tools/flag_diff.py. Run via flaglog.bat.
-- Default word: 0x0C1DE144 (futaribl main loop "frame ready" flag: 0->1 = work done, 1->0 = flipped).
-- Uses a coroutine with emu.wait (no memory taps: a tap on the blitter made MAME 0.289 exit).
local addr  = tonumber(os.getenv("FLAG_ADDR") or "0C1DE144", 16)
local first = tonumber(os.getenv("FLAG_FIRST") or "150")
local last  = tonumber(os.getenv("FLAG_LAST") or "420")
local step  = tonumber(os.getenv("POLL_US") or "5") / 1e6
-- FLAG_IPRA0=1: write IPRA (0xFFFFFEE2) = 0 after every frame from frame 150 on, i.e. timers run but
-- their interrupts (TUNI0-2) are never taken; used to measure the cost of the timer interrupts.
local ipra0 = os.getenv("FLAG_IPRA0") == "1"
local n = dofile("inputs.lua")
local screen, space, out
local function now() local t = manager.machine.time; return t.seconds + t.attoseconds / 1e18 end
local function poller()
  local prev = nil
  while true do
    local fr = screen:frame_number()
    if fr > last then break end
    local v = space:read_u32(addr) & 0xFFFFFFFF
    if v ~= prev then out:write(string.format("%d %.9f %08x\n", fr, now(), v)); prev = v end
    emu.wait(step)
  end
  out:close(); print("flaglog.lua: done"); manager.machine:exit()
end
local started = false
local function on_frame()
  local ok, e = pcall(function()
    screen = screen or manager.machine.screens[":screen"]
    space = space or manager.machine.devices[":maincpu"].spaces["program"]
    local fr = screen:frame_number()
    apply_inputs(fr)
    if ipra0 and fr >= 150 then space:write_u16(0xFFFFFEE2, 0) end
    if fr % 50 == 0 then print("flaglog.lua: frame " .. fr) end
    if not started and fr >= first then
      started = true
      out = io.open("flags.txt", "w")
      local co = coroutine.create(function()
        local ok2, e2 = pcall(poller)
        if not ok2 then print("flaglog.lua: ERROR in poller: " .. tostring(e2)) end
      end)
      local ok3, e3 = coroutine.resume(co)
      if not ok3 then print("flaglog.lua: ERROR starting poller: " .. tostring(e3)) end
    end
  end)
  if not ok then print("flaglog.lua: ERROR: " .. tostring(e)) end
end
local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.flaglog_sub = add(on_frame)
print(string.format("flaglog.lua: %d input events, polling %08x every %.1f us, frames %d-%d%s", n, addr, step * 1e6, first, last, ipra0 and ", IPRA forced to 0" or ""))
