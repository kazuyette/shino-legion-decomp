-- Shinobi Legions: BizHawk <-> Claude file bridge.
-- Load in BizHawk: Tools > Lua Console > Script > Open Script > this file.
-- Protocol: Claude writes bridge/cmd.txt ("<id>" on line 1, then one command per line);
-- the script runs the commands and writes bridge/out.txt ("<id>" on line 1, then results).
-- While idle the emulator is paused, so the game only advances on "frames"/"press"/"run".

local DIR = "E:/Cowork artifacts/Projects/decomp/Shinobi_Legions_(USA)_(DW0536)/tools/bizhawk/bridge/"
local last_id = nil
local free_run = false

local function split(s) local t = {} for w in s:gmatch("%S+") do t[#t+1] = w end return t end
local function readfile(p) local f = io.open(p, "rb"); if not f then return nil end local s = f:read("a"); f:close(); return s end
local function writefile(p, s)
  local tmp = p .. ".tmp"
  local f = io.open(tmp, "wb"); f:write(s); f:close()
  os.remove(p); os.rename(tmp, p)
end

-- Saturn address -> memory domain + offset (domain names found by "info"; patched below)
local DOM = { hwram = nil, lwram = nil, vdp1 = nil, vdp2 = nil, cram = nil, bus = nil }
local function pick(list, cands)
  for _, c in ipairs(cands) do for _, d in ipairs(list) do if d:lower() == c:lower() then return d end end end
  for _, c in ipairs(cands) do for _, d in ipairs(list) do if d:lower():find(c:lower(), 1, true) then return d end end end
  return nil
end
local function init_domains()
  local l = memory.getmemorydomainlist()
  DOM.bus   = pick(l, {"System Bus"})
  DOM.hwram = pick(l, {"Work Ram High", "WorkRamHigh", "HWRAM", "Work RAM High", "WRAMH"})
  DOM.lwram = pick(l, {"Work Ram Low", "WorkRamLow", "LWRAM", "Work RAM Low", "WRAML"})
  DOM.vdp1  = pick(l, {"VDP1 Ram", "VDP1 VRAM", "VDP1"})
  DOM.vdp2  = pick(l, {"VDP2 Ram", "VDP2 VRAM", "VDP2"})
  DOM.cram  = pick(l, {"VDP2 CRam", "VDP2 CRAM", "CRAM", "Color RAM"})
end
local function map(a)
  local r = a & 0x0FFFFFFF
  if r >= 0x06000000 and r < 0x08000000 and DOM.hwram then return DOM.hwram, r & 0xFFFFF end
  if r >= 0x00200000 and r < 0x00300000 and DOM.lwram then return DOM.lwram, r & 0xFFFFF end
  if r >= 0x05C00000 and r < 0x05C80000 and DOM.vdp1 then return DOM.vdp1, r & 0x7FFFF end
  if r >= 0x05E00000 and r < 0x05E80000 and DOM.vdp2 then return DOM.vdp2, r & 0x7FFFF end
  if r >= 0x05F00000 and r < 0x05F01000 and DOM.cram then return DOM.cram, r & 0xFFF end
  if DOM.bus then return DOM.bus, a end
  return nil, nil
end
local function readbytes(a, n)
  local d, o = map(a); if not d then return nil end
  return memory.read_bytes_as_array(o, n, d)
end

local held = {}
local function setpad() local t = {} for k, v in pairs(held) do t[k] = v end joypad.set(t) end
local function advance(n)
  client.unpause()
  for _ = 1, n do setpad(); emu.frameadvance() end
  if not free_run then client.pause() end
end

local function hex(arr) local t = {} for i = 1, #arr do t[i] = string.format("%02X", arr[i]) end return table.concat(t) end

local function run(cmd, out)
  local a = split(cmd); local c = a[1] and a[1]:lower()
  if not c or c:sub(1,1) == "#" then return end
  if c == "ping" then out[#out+1] = "pong frame " .. emu.framecount()
  elseif c == "info" then
    init_domains()
    out[#out+1] = "system " .. tostring(emu.getsystemid()) .. " frame " .. emu.framecount()
    out[#out+1] = "domains " .. table.concat(memory.getmemorydomainlist(), " | ")
    for k, v in pairs(DOM) do out[#out+1] = "map " .. k .. " = " .. tostring(v) end
    local keys = {} for k, _ in pairs(joypad.get()) do keys[#keys+1] = k end table.sort(keys)
    out[#out+1] = "buttons " .. table.concat(keys, " | ")
  elseif c == "hold" then for i = 2, #a do held[a[i]:gsub("_", " ")] = true end out[#out+1] = "ok"
  elseif c == "release" then if #a == 1 then held = {} else for i = 2, #a do held[a[i]:gsub("_", " ")] = nil end end out[#out+1] = "ok"
  elseif c == "frames" then advance(tonumber(a[2]) or 1); out[#out+1] = "frame " .. emu.framecount()
  elseif c == "press" then   -- press BTN [hold frames=3]
    local b = a[2]:gsub("_", " "); held[b] = true; advance(tonumber(a[3]) or 3); held[b] = nil; advance(1)
    out[#out+1] = "frame " .. emu.framecount()
  elseif c == "read" then    -- read ADDR LEN  -> hex
    local r = readbytes(tonumber(a[2]), tonumber(a[3]) or 2)
    out[#out+1] = r and hex(r) or "ERR unmapped"
  elseif c == "dump" then    -- dump ADDR LEN FILE (binary, into bridge/)
    local addr, n = tonumber(a[2]), tonumber(a[3]); local f = io.open(DIR .. a[4], "wb")
    local CH = 0x4000
    for off = 0, n - 1, CH do
      local r = readbytes(addr + off, math.min(CH, n - off)); if not r then f:close(); out[#out+1] = "ERR unmapped"; return end
      local s = {} for i = 1, #r do s[i] = string.char(r[i]) end f:write(table.concat(s))
    end
    f:close(); out[#out+1] = "ok " .. a[4]
  elseif c == "write" then   -- write ADDR HEXBYTES
    local d, o = map(tonumber(a[2])); local h = a[3]
    for i = 0, #h / 2 - 1 do memory.writebyte(o + i, tonumber(h:sub(2*i+1, 2*i+2), 16), d) end
    out[#out+1] = "ok"
  elseif c == "shot" then client.screenshot(DIR .. (a[2] or "shot.png")); out[#out+1] = "ok " .. (a[2] or "shot.png")
  elseif c == "save" then savestate.save(DIR .. (a[2] or "state.State")); out[#out+1] = "ok"
  elseif c == "load" then savestate.load(DIR .. (a[2] or "state.State")); out[#out+1] = "ok frame " .. emu.framecount()
  elseif c == "run" then free_run = true; client.unpause(); out[#out+1] = "running"
  elseif c == "pause" then free_run = false; client.pause(); out[#out+1] = "paused frame " .. emu.framecount()
  elseif c == "speed" then client.speedmode(tonumber(a[2]) or 100); out[#out+1] = "ok"
  else out[#out+1] = "ERR unknown command " .. c end
end

init_domains()
client.pause()
console.log("Shinobi Legions bridge ready: " .. DIR)
writefile(DIR .. "status.txt", "ready frame " .. emu.framecount() .. "\n")
while true do
  local s = readfile(DIR .. "cmd.txt")
  if s then
    local id = s:match("^(%S+)")
    if id and id ~= last_id then
      last_id = id
      local out = { id }
      local first = true
      for line in s:gmatch("[^\r\n]+") do
        if first then first = false else
          local ok, err = pcall(run, line, out)
          if not ok then out[#out+1] = "ERR " .. tostring(err) end
        end
      end
      writefile(DIR .. "out.txt", table.concat(out, "\n") .. "\n")
    end
  end
  if free_run then emu.frameadvance() else emu.yield() end
end
