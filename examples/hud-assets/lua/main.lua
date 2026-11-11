-- Register the two HUD files and eleven replacement texture aliases in your project first.
local mod = dluma.mod()
local current
local function log(text) dluma.log("HUD assets: " .. text) end
local function stop_polling()
    if current and current.timer then dluma.timer.cancel(current.timer); current.timer = nil end
end
local function clear()
    stop_polling()
    current = nil
end
local function request_restore()
    if not current then return false end
    if current.restoring then return true end
    local ok, err
    if current.kind == "document" then ok, err = dluma.hud.restore_document(current.token)
    else ok, err = dluma.hud.restore_flashback_textures(current.token) end
    if not ok then
        log("restore request failed: " .. tostring(err and err.code))
        stop_polling()
        return false
    end
    current.restoring, current.ticks = true, 0
    return true
end
local function poll()
    if not current then return end
    local status, err
    if current.kind == "document" then status, err = dluma.hud.document_status(current.token)
    else status, err = dluma.hud.flashback_texture_status(current.token) end
    if not status then log("status failed: " .. tostring(err and err.code)); stop_polling(); return end
    current.ticks = current.ticks + 1
    log("state=" .. status.state .. " reason=" .. status.reason)
    local applied = current.kind == "document" and status.state == 4 or current.kind == "texture" and status.state == 3
    local restored = current.kind == "document" and status.state == 6 or current.kind == "texture" and status.state == 5
    local failed = current.kind == "document" and (status.state == 7 or status.state == 8) or current.kind == "texture" and status.state == 6
    if restored then clear(); return end
    if failed then log("failed or requires recovery; inspect reason, then use restore-assets"); stop_polling(); return end
    if applied and not current.restoring then request_restore(); return end
    if current.ticks >= 120 then
        log("status check timed out; inspect the request, then use restore-assets")
        stop_polling()
    end
end
local function start_polling()
    if not current or current.timer then return end
    current.ticks = 0
    current.timer = dluma.timer.every(0.25, poll)
    if not current.timer then log("could not schedule status checks; use restore-assets to try again") end
end
local function start(kind, token, err)
    if not token then log("request failed: " .. tostring(err and err.code)); return end
    log("request token=" .. tostring(token))
    current = {kind=kind, token=token, ticks=0, restoring=false}
    start_polling()
    if not current.timer then request_restore() end
end
assert(mod:handle("swap-document", function()
    if current then log("request already active"); return end
    local token, err = dluma.hud.swap_document("menu/hud/original.xui", "menu/hud/custom.xui")
    start("document", token, err)
end))
assert(mod:handle("swap-flashbacks", function()
    if current then log("request already active"); return end
    local pairs = {}
    for i=0,10 do pairs[i+1] = {original="hud_bossfight_flash_" .. i, replacement="example_flash_" .. i} end
    local token, err = dluma.hud.swap_flashback_textures(pairs)
    start("texture", token, err)
end))
assert(mod:handle("restore-assets", function()
    if not current then log("no active request"); return end
    if request_restore() then start_polling() end
end))
mod:on_unload(function() if current then request_restore(); stop_polling() end end)
return mod
