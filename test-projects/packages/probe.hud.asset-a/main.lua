local mod = dluma.mod()
local current
local effect_active = false
local panel, visual = "EffectPanel", "EffectPanel/Visual"
local function log(s) dluma.log("FEATURE_TEST " .. s) end
assert(mod:handle("shared", function(_, event) log(string.format("handler=A shared source=%s map=%s provider=%s handler=%s quest=%s phase=%s", tostring(event.source_kind), tostring(event.map_generation), tostring(event.provider), tostring(event.handler), tostring(event.quest_id), tostring(event.phase_id))) end))
local function report(name, ok, err)
    if not ok then log(name .. " failed: " .. tostring(err and err.code)); return false end
    return true
end
local effect_timer
local effect_failed = false
local function cancel_effect_timer()
    if effect_timer then dluma.timer.cancel(effect_timer); effect_timer = nil end
end
local function effect_call(name, ok, err)
    if report(name, ok, err) then return true end
    cancel_effect_timer()
    effect_failed = true
    log("control sequence stopped; explicit reset required")
    return false
end
local function effect_baseline()
    return effect_call("position restore", dluma.hud.set_position(panel, 400, 160, 0)) and
           effect_call("color restore", dluma.hud.set_color(visual, 1, 1, 1, 1)) and
           effect_call("opacity restore", dluma.hud.set_opacity(visual, 1))
end
local function effects(enable, explicit)
    if not enable then
        cancel_effect_timer()
        if not effect_active then return end
        if effect_failed and not explicit then log("control cleanup requires explicit reset"); return end
        effect_failed = false
        if not effect_baseline() or not effect_call("visibility reset", dluma.hud.reset(panel)) then return end
        effect_active = false
        return
    end
    if effect_active then log("control sequence already active; reset it first"); return end
    effect_active = true
    effect_failed = false
    if not effect_call("show", dluma.hud.show(panel)) then return end
    log("controls baseline shown")
    local step = 0
    local function next_step()
        effect_timer = nil
        step = step + 1
        if step == 1 then
            if not effect_call("position", dluma.hud.set_position(panel, 600, 160, 0)) or
               not effect_call("color", dluma.hud.set_color(visual, 1, 0, 0, 1)) or
               not effect_call("opacity", dluma.hud.set_opacity(visual, 0.65)) then return end
            log("controls moved and recolored")
        elseif step == 2 then
            if not effect_call("hide", dluma.hud.hide(panel)) then return end
            log("controls hidden")
        elseif step == 3 then
            if not effect_call("show again", dluma.hud.show(panel)) then return end
            log("controls shown again")
        else
            if effect_baseline() then log("controls baseline restored") end
            return
        end
        effect_timer = dluma.timer.after(2, next_step)
        if not effect_timer then effect_failed = true; log("control timer unavailable; explicit reset required") end
    end
    effect_timer = dluma.timer.after(2, next_step)
    if not effect_timer then effect_failed = true; log("control timer unavailable; explicit reset required") end
end
local poll, begin
local function restore()
    if not current then return end
    if current.restoring then
        if not current.timer then
            current.ticks = 0
            current.timer = dluma.timer.every(0.25, poll)
            if not current.timer then log("restore status timer unavailable; inspect manually") end
        end
        return
    end
    local ok, err
    if current.kind == "document" then ok, err = dluma.hud.restore_document(current.token)
    else ok, err = dluma.hud.restore_flashback_textures(current.token) end
    if report("restore request", ok, err) then
        current.restoring = true; current.ticks = 0
        if not current.timer then current.timer = dluma.timer.every(0.25, poll) end
        if not current.timer then log("restore status timer unavailable; inspect manually") end
    end
end
poll = function()
    if not current then return end
    local s, err
    if current.kind == "document" then s, err = dluma.hud.document_status(current.token)
    else s, err = dluma.hud.flashback_texture_status(current.token) end
    if not s then log("status failed: " .. tostring(err and err.code) .. "; token retained"); dluma.timer.cancel(current.timer); current.timer = nil; return end
    local ready = not current.restoring and ((current.kind == "document" and s.state == 4) or (current.kind == "textures" and (s.state == 2 or s.state == 3)))
    if not ready then current.ticks = current.ticks + 1 else current.ticks = 0 end
    local ready_count = current.kind == "textures" and s.ready_count or nil
    local asset_count = current.kind == "textures" and s.asset_count or nil
    if s.state ~= current.last_state or s.reason ~= current.last_reason or
       ready_count ~= current.last_ready_count or asset_count ~= current.last_asset_count then
        local counts = current.kind == "textures" and
            (" ready=" .. tostring(ready_count) .. " assets=" .. tostring(asset_count)) or ""
        log(current.kind .. " state=" .. s.state .. " reason=" .. s.reason .. counts)
        current.last_state, current.last_reason = s.state, s.reason
        current.last_ready_count, current.last_asset_count = ready_count, asset_count
    end
    local done = current.restoring and ((current.kind == "document" and s.state == 6) or (current.kind == "textures" and s.state == 5))
    local failed = (current.kind == "document" and (s.state == 7 or s.state == 8)) or (current.kind == "textures" and s.state == 6)
    if done then
        local next_variant = current.next_variant
        local kind = current.kind
        dluma.timer.cancel(current.timer); current = nil
        if next_variant then
            log("red restored; requesting green document")
            begin("document", next_variant)
        else
            if kind == "document" then log("restoration confirmed; normal HUD")
            else log("restoration confirmed; original flashback textures") end
        end
    elseif failed then
        log("failed or recovery required; token retained; invoke reset to request restoration")
        dluma.timer.cancel(current.timer); current.timer = nil
        current.restoring = false
        current.next_variant = nil
    elseif current.ticks >= 240 then
        log("status timed out; token retained, no automatic retry")
        dluma.timer.cancel(current.timer); current.timer = nil
        current.next_variant = nil
    elseif current.kind == "document" and s.state == 4 and not current.restoring and current.variant ~= "controls" then
        current.applied_ticks = (current.applied_ticks or 0) + 1
        if current.applied_ticks == 1 then
            if current.variant == "missing" then log("ERROR missing document unexpectedly applied")
            else log("document applied=" .. current.variant .. "; check centered square") end
        end
        if current.applied_ticks >= 48 then restore() end
    end
end
begin = function(kind, variant)
    if current then log("another request is active; reset it first"); return end
    local token, err
    if kind == "document" then
        variant = variant or "red"
        local original = variant == "missing" and "menu/hud/__dluma_missing_document__.xui" or "menu/hud/hud_normal.xui"
        local path = variant == "controls" and "menu/hud/hud_test.xui" or
            variant == "missing" and "menu/hud/hud_red.xui" or "menu/hud/hud_" .. variant .. ".xui"
        token, err = dluma.hud.swap_document(original, path)
    else
        local pairs = {}
        for i=0,10 do
            local replacement = variant == "missing" and i == 5 and "dluma_test_missing_5" or "dluma_test_flash_" .. i
            pairs[i+1] = {original="hud_bossfight_flash_" .. i, replacement=replacement}
        end
        token, err = dluma.hud.swap_flashback_textures(pairs)
    end
    if not token then log(kind .. " request failed: " .. tostring(err and err.code)); return end
    current = {kind=kind, token=token, ticks=0, variant=variant, next_variant=variant == "red" and "green" or nil}
    current.timer = dluma.timer.every(0.25, poll)
    if not current.timer then log("status timer unavailable; requesting restore"); restore() end
end
assert(mod:handle("document", function() log("run=document"); begin("document") end))
assert(mod:handle("document-missing", function() log("run=document-missing"); begin("document", "missing") end))
assert(mod:handle("textures-missing", function() log("run=textures-missing"); begin("textures", "missing") end))
assert(mod:handle("document-controls", function() log("run=document-controls"); begin("document", "controls") end))
assert(mod:handle("textures", function() log("run=textures"); begin("textures") end))
assert(mod:handle("controls", function() log("run=controls"); effects(true) end))
assert(mod:handle("reset", function() log("reset"); if current then current.next_variant = nil end; effects(false, true); restore() end))
mod:on_config_changed(function(_, change)
    if change.key == "effects" then effects(change.value, true)
    elseif change.key == "document" or change.key == "textures" then
        if change.value then begin(change.key) elseif current and current.kind == change.key then current.next_variant = nil; restore() end
    end
end)
mod:on_map_unload(function() effects(false); if current then current.next_variant = nil; restore() end end)
mod:on_unload(function() effects(false); if current then current.next_variant = nil; restore() end end)
return mod
