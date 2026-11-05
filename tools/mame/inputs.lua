-- Shared input-script handling for capture.lua / ramdump.lua (loaded with dofile).
local INPUT = os.getenv("CAP_INPUT")
-- input tokens as MAME names them (per-player inputs carry a P1_/P2_ prefix)
map = {
  SERVICE="SERVICE1", COIN1="COIN1", COIN2="COIN2", START1="START1", START2="START2", TEST="SERVICE",
  UP="P1_JOYSTICK_UP", DOWN="P1_JOYSTICK_DOWN", LEFT="P1_JOYSTICK_LEFT", RIGHT="P1_JOYSTICK_RIGHT",
  B1="P1_BUTTON1", B2="P1_BUTTON2", B3="P1_BUTTON3", B4="P1_BUTTON4",
  P2_UP="P2_JOYSTICK_UP", P2_DOWN="P2_JOYSTICK_DOWN", P2_LEFT="P2_JOYSTICK_LEFT", P2_RIGHT="P2_JOYSTICK_RIGHT",
  P2_B1="P2_BUTTON1", P2_B2="P2_BUTTON2", P2_B3="P2_BUTTON3", P2_B4="P2_BUTTON4",
}
fields_by_tok = {}
function field_for(tok)
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

events = {}
if INPUT then
  for raw in io.lines(INPUT) do
    local line = raw:gsub("#.*", "")
    local toks = {}
    for w in line:gmatch("%S+") do toks[#toks + 1] = w end
    if #toks >= 3 then
      local e = {start = tonumber(toks[1]), dur = tonumber(toks[2]), toks = {}}
      for i = 3, #toks do e.toks[#e.toks + 1] = toks[i] end
      events[#events + 1] = e
    end
  end
end


local held = {}
function apply_inputs(fr)
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
end
return #events
