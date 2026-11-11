local mod = dluma.mod()

local function update_radar(event, restore)
    dluma.log(string.format("HUD handler source=%d map=%d provider=%s handler=%s quest=%s phase=%s",
        event.source_kind, event.map_generation, event.provider, event.handler, event.quest_id, event.phase_id))
    -- Arguments are decoded into a Lua table. This example's quest supplies "reason".
    dluma.log("HUD handler reason=" .. tostring(event.arguments.reason))
    local ok, err
    if restore then
        ok, err = dluma.hud.reset("radar")
    else
        ok, err = dluma.hud.hide("radar")
    end
    if not ok then dluma.log("HUD action failed: " .. tostring(err and err.message)) end
end

assert(mod:handle("hide-radar", function(_, event) update_radar(event, false) end))
assert(mod:handle("restore-radar", function(_, event) update_radar(event, true) end))
