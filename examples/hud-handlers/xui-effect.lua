-- Add this handler to a mod with ui.overlay and quest.phase.handle access.
-- Merge the EffectPanel from xui-effect-controls.xui and the strings from
-- xui-effect-texts.scr into your project's existing HUD and locale sources.
local mod = dluma.mod()
local panel, label, visual, timeline = "EffectPanel", "EffectPanel/Label",
    "EffectPanel/Visual", "EffectPanel"
local keys = { "DLUMA_Example_EffectOne", "DLUMA_Example_EffectTwo" }
-- Set these to the authored baseline values for your own controls.
local baseline_position = { 400, 160, 0 }
local baseline_color = { 1, 1, 1, 1 }
local baseline_opacity = 1
local timer
local active = false

local function call(name, ok, err)
    if ok then return true end
    dluma.log(name .. " failed: " .. tostring(err and err.code))
    return false
end

local function stop()
    if timer then dluma.timer.cancel(timer); timer = nil end
    if not active then return end
    -- Hide first, then restore the known authored baseline. Calls may fail if
    -- the screen has already retired. An authored animation may still run.
    dluma.hud.hide(panel)
    dluma.hud.set_position(panel, baseline_position[1], baseline_position[2], baseline_position[3])
    dluma.hud.set_color(visual, baseline_color[1], baseline_color[2], baseline_color[3], baseline_color[4])
    dluma.hud.set_opacity(visual, baseline_opacity)
    dluma.hud.reset(panel)
    active = false
end

assert(mod:handle("play-effect", function()
    stop()
    if not call("show", dluma.hud.show(panel)) then return end
    active = true
    if not call("text", dluma.hud.set_text_key(label, keys[math.random(#keys)])) or
       not call("move", dluma.hud.set_position(panel,
           baseline_position[1] + math.random(-10, 10), baseline_position[2], baseline_position[3])) or
       not call("opacity", dluma.hud.set_opacity(visual, 1)) or
       not call("color", dluma.hud.set_color(visual, math.random(), math.random(), math.random(), 1)) or
       not call("animation", dluma.hud.start_animation(timeline, "Shake")) then
        stop()
        return
    end
    local step = 0
    local function fade()
        step = step + 1
        if not call("fade", dluma.hud.set_opacity(visual, 1 - step / 5)) then stop(); return end
        if step == 5 then timer = nil; stop(); return end
        timer = dluma.timer.after(0.1, fade)
        if not timer then stop() end
    end
    timer = dluma.timer.after(0.5, fade)
    if not timer then stop() end
end))
mod:on_map_unload(stop)
mod:on_unload(stop)
return mod
