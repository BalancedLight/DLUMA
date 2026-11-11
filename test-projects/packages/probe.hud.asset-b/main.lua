local mod = dluma.mod()
assert(mod:handle("shared", function(_, event) dluma.log("FEATURE_TEST " .. string.format("handler=B shared source=%s map=%s provider=%s handler=%s quest=%s phase=%s", tostring(event.source_kind), tostring(event.map_generation), tostring(event.provider), tostring(event.handler), tostring(event.quest_id), tostring(event.phase_id))) end))
return mod
