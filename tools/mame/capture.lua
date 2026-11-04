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

local map = {
  SERVICE={"SERVICE1",0}, COIN1={"COIN1",0}, COIN2={"COIN2",0}, START1={"START1",0}, START2={"START2",0},
  TEST={"SERVICE",0},
  UP={"JOYSTICK_UP",0}, DOWN={"JOYSTICK_DOWN",0}, LEFT={"JOYSTICK_LEFT",0}, RIGHT={"JOYSTICK_RIGHT",0},
  B1={"BUTTON1",0}, B2={"BUTTON2",0}, B3={"BUTTON3",0}, B4={"BUTTON4",0},
  P2_UP={"JOYSTICK_UP",1}, P2_DOWN={"JOYSTICK_DOWN",1}, P2_LEFT={"JOYSTICK_LEFT",1}, P2_RIGHT={"JOYSTICK_RIGHT",1},
  P2_B1={"BUTTON1",1}, P2_B2={"BUTTON2",1}, P2_B3={"BUTTON3",1}, P2_B4={"BUTTON4",1},
}
local fields_by_tok = {}
local function field_for(tok)
  if fields_by_tok[tok] ~= nil then return fields_by_tok[tok] end
  local spec = map[tok]; if not spec then return nil end
  local ioport = manager.machine.ioport
  local want_type = ioport:token_to_input_type(spec[1])
  for _, port in pairs(ioport.ports) do
    for _, f in pairs(port.fields) do
      if f.type == want_type and f.player == spec[2] then fields_by_tok[tok] = f; return f end
    end
  end
  print("capture.lua: no field for " .. tok); fields_by_tok[tok] = false; return false
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
