-- Stop, Stopra and Stopga (513, 514, 515) given and set at Mission Mode's camp, then mission file 2, its own enemies
-- held back, with three of the area's enemies made a second before the walk presses its first cast
-- (port/tools/regress_walks/stop.input: Stopga, then Stopra and Stop while the enemies are held, then attacks whose
-- damage they keep), four units from the player: risen but not there yet as the cast takes them (held while rising,
-- a Shadow keeps its flat start); the port logs each hold ("stop:" lines), this mod the mission, the enemies made
-- and the defeats.
local STOP = {513, 514, 515}
local MAKE_AT = 3665 -- the mod's frame a second before the walk presses its first cast (the walk's 7390)
local camps, kind, made = 0, nil, false

local function summary(label)
  local d = deck.read()
  local cells = {}
  for _, p in ipairs(d.panels) do cells[#cells + 1] = p.panel .. "@" .. p.page .. "." .. p.row .. "." .. p.col end
  print(label, "open", d.open, "set", table.concat(cells, " "))
end

mod.on("scene", function(e)
  if e.to ~= 19 then return end
  camps = camps + 1
  if camps ~= 1 then return end
  for _, id in ipairs(STOP) do
    local info, why = deck.info(id)
    print("panel", id, info and info.category or why, info and info.kind or "")
    print("give", id, deck.give(id, true))
  end
  summary("deck")
  print("start", mission.start(2))
end)

mod.on("mission_start", function(e)
  print("mission start", e.mission, "its enemies held", enemies.hold_mission(true))
end)

mod.on("enemy_defeated", function(e)
  print("defeated", e.kind or "", mod.frame())
end)

mod.on("mission_end", function(e)
  print("mission end", e.mission, "ending", e.ending)
end)

mod.on("frame", function(e)
  if made or e.paused then return end
  if kind == nil then
    local kinds = enemies.kinds()
    if #kinds > 0 then kind = kinds[1].kind end
    return
  end
  if e.frame < MAKE_AT then return end
  local x, y, z = player.position()
  local m, why = enemies.make{ kind = kind, at = { x + 4, y, z }, count = 3, spread = 1 }
  made = true
  print("made", m and #m or 0, why or "")
end)
