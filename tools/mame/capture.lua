-- MAME autoboot script: applies an input script and saves reference snapshots.
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
local INPUT = os.getenv("CAP_INPUT")

-- input tokens as MAME names them (per-player inputs carry a P1_/P2_ prefix)
local map = {
  SERVICE="SERVICE1", COIN1="COIN1", COIN2="COIN2", START1="START1", START2="START2", TEST="SERVICE",
  UP="P1_JOYSTICK_UP", DOWN="P1_JOYSTICK_DOWN", LEFT="P1_JOYSTICK_LEFT", RIGHT="P1_JOYSTICK_RIGHT",
  B1="P1_BUTTON1", B2="P1_BUTTON2", B3="P1_BUTTON3", B4="P1_BUTTON4",
  P2_UP="P2_JOYSTICK_UP", P2_DOWN="P2_JOYSTICK_DOWN", P2_LEFT="P2_JOYSTICK_LEFT", P2_RIGHT="P2_JOYSTICK_RIGHT",
  P2_B1="P2_BUTTON1", P2_B2="P2_BUTTON2", P2_B3="P2_BUTTON3", P2_B4="P2_BUTTON4",
}
local fields_by_tok = {}
local function field_for(tok)
  if fields_by_tok[tok] ~= nil then return fields_by_tok[tok] end
  local name = map[tok]; if not name then return nil end
  local ioport = manager.machine.ioport
  local want_type, want_player = ioport:token_to_input_type(name)
  for _, port in pairs(ioport.ports) do
    for _, f in pairs(port.fields) do
      if f.type == want_type and f.player == (want_player or 0) then fields_by_tok[tok] = f; return f end
    end
  end
  print("capture.lua: no field for " .. tok .. " (" .. name .. ")"); fields_by_tok[tok] = false; return false
end

local events = {}
if INPUT then
  for line in io.lines(INPUT) do
    line = line:gsub("#.*", "")
    local toks = {}
    for w in line:gmatch("%S+") do toks[#toks + 1] = w end
    if #toks >= 3 then
      local e = {start = tonumber(toks[1]), dur = tonumber(toks[2]), toks = {}}
      for i = 3, #toks do e.toks[#e.toks + 1] = toks[i] end
      events[#events + 1] = e
    end
  end
end

local screen
local held = {}
local function on_frame()
  screen = screen or manager.machine.screens[":screen"]
  local fr = screen:frame_number()
  local want = {}
  for _, e in ipairs(events) do
    if fr >= e.start and fr < e.start + e.dur then for _, t in ipairs(e.toks) do want[t] = true end end
  end
  for t, _ in pairs(map) do
    local f = field_for(t)
    if f then
      if want[t] and not held[t] then f:set_value(1); held[t] = true
      elseif not want[t] and held[t] then f:clear_value(); held[t] = nil end
    end
  end
  if fr >= FROM and fr <= TO and (fr - FROM) % EVERY == 0 then
    screen:snapshot(string.format("f_%05d.png", fr))
  end
  if fr >= EXIT then manager.machine:exit() end
end

local add = emu.add_machine_frame_notifier or emu.register_frame_done
_G.capture_sub = add(on_frame)
print(string.format("capture.lua: snapshots %d..%d every %d, exit at %d, %d input events", FROM, TO, EVERY, EXIT, #events))
