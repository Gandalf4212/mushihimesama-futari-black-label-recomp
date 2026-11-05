-- MAME autoboot script: samples the main CPU's program counter every POLL_US microseconds of emulated
-- time between frames PC_FIRST and PC_LAST and writes a histogram of 256-byte code regions to
-- pcprof.txt as "<region hex> <samples>". Same format as `run_headless --pc-profile file first last us`;
-- compare with tools/pc_diff.py. Run via pclog.bat. Needs inputs.lua next to it.
-- FLAG_IPRA0=1: like flaglog.lua, write IPRA = 0 after every frame from frame 150 (no timer IRQs).
local first = tonumber(os.getenv("PC_FIRST") or "200")
local last  = tonumber(os.getenv("PC_LAST") or "400")
local step  = tonumber(os.getenv("POLL_US") or "5") / 1e6
local ipra0 = os.getenv("FLAG_IPRA0") == "1"
local n = dofile("inputs.lua")
local screen, space, cpu
local hist, total = {}, 0
local function sampler()
  local pcst = cpu.state["PC"]
  while screen:frame_number() < last do
    local r = (pcst.value & 0x1FFFFFFF) >> 8
    hist[r] = (hist[r] or 0) + 1
    total = total + 1
    emu.wait(step)
  end
  local out = io.open("pcprof.txt", "w")
  local keys = {}
  for k in pairs(hist) do keys[#keys + 1] = k end
  table.sort(keys)
  for _, k in ipairs(keys) do out:write(string.format("%06x %d\n", k, hist[k])) end
  out:close()
  print("pclog.lua: " .. total .. " samples written to pcprof.txt")
  manager.machine:exit()
end
local started = false
local function on_frame()
  local ok, e = pcall(function()
    screen = screen or manager.machine.screens[":screen"]
    space = space or manager.machine.devices[":maincpu"].spaces["program"]
    cpu = cpu or manager.machine.devices[":maincpu"]
    local fr = screen:frame_number()
    apply_inputs(fr)
    if ipra0 and fr >= 150 then space:write_u16(0xFFFFFEE2, 0) end
    if fr % 50 == 0 then print("pclog.lua: frame " .. fr .. ", samples " .. total) end
    if not started and fr >= first then
      started = true
      local co = coroutine.create(function()
        local ok2, e2 = pcall(sampler)
        if not ok2 then print("pclog.lua: ERROR in sampler: " .. tostring(e2)) end
      end)
      local ok3, e3 = coroutine.resume(co)
      if not ok3 then print("pclog.lua: ERROR starting sampler: " .. tostring(e3)) end
    end
  end)
  if not ok then print("pclog.lua: ERROR: " .. tostring(e)) end
end
local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.pclog_sub = add(on_frame)
print(string.format("pclog.lua: %d input events, sampling PC every %.1f us, frames %d-%d%s", n, step * 1e6, first, last, ipra0 and ", IPRA forced to 0" or ""))
