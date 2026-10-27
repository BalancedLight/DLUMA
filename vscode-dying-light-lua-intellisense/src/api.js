const vscode = require('vscode');
const generated = require('./generated-api.json');

const functionKind = vscode.CompletionItemKind.Function;
const methodKind = vscode.CompletionItemKind.Method;
const moduleKind = vscode.CompletionItemKind.Module;

function api(label, signature, documentation, options = {}) {
    return {
        label,
        signature,
        documentation,
        kind: functionKind,
        detail: 'DLUMA API 1.0',
        status: 'implemented',
        ...options,
    };
}

function namespace(label, documentation) {
    return api(label, `dluma.${label}`, documentation, {
        kind: moduleKind,
        detail: 'DLUMA API 1.0 namespace',
    });
}

const contracts = new Map(generated.operations.map(operation => [operation.id, operation]));

function operation(id, signature, parameters = [], options = {}) {
    const contract = contracts.get(id);
    if (!contract) throw new Error(`Missing generated DLUMA API operation: ${id}`);
    const lastName = contract.name.split(/[.:]/).at(-1);
    return api(options.label || lastName, signature, contract.documentation, {
        parameters,
        operationId: id,
        numericId: contract.numeric_id,
        capability: contract.capability,
        permission: contract.access[0],
        access: contract.access,
        scopes: contract.scopes,
        thread: contract.thread,
        status: contract.status,
        source: contract.name,
        ...options,
    });
}

const API = {
    version: generated.api_version,
    catalogHash: generated.catalog_sha256,
    root: [
        api('api_version', 'dluma.api_version() -> string', 'Returns the public DLUMA API version.'),
        api('loader_version', 'dluma.loader_version() -> string', 'Returns the running DLUMA release version.'),
        operation('core.capability.query', 'dluma.capability(id) -> capability_status|nil, error?', ['id: string'], {
            insertText: 'capability(${1:"world.find"})',
        }),
        api('current_map', 'dluma.current_map() -> string|nil', 'Returns the active internal map name, if any.'),
        api('current_project', 'dluma.current_project() -> string|nil', 'Returns the active Workshop project name, if any.'),
        api('script_scope', 'dluma.script_scope() -> "global"|"project"|"map"', 'Returns the scope declared by mod.dluma.json.'),
        operation('core.log', 'dluma.log([level], message) -> true|nil, error?', ['[level: string]', 'message: string'], {
            insertText: 'log(${1:"info"}, ${2:message})',
        }),
        api('mod', 'dluma.mod() -> mod', 'Creates this package\'s mod object. Metadata and requested access come only from mod.dluma.json.', {
            insertText: 'mod()\n$0',
        }),
    ],
    namespaces: {
        clock: [
            operation('core.clock.monotonic_ms', 'dluma.clock.monotonic_ms() -> integer'),
        ],
        timer: [
            operation('core.timer.after', 'dluma.timer.after(seconds, callback) -> token|nil, error?', ['seconds: number', 'callback: function']),
            operation('core.timer.every', 'dluma.timer.every(seconds, callback) -> token|nil, error?', ['seconds: number', 'callback: function']),
            api('cancel', 'dluma.timer.cancel(token) -> boolean', 'Cancels an active owner-bound timer token.', { parameters: ['token: integer'] }),
        ],
        storage: [
            operation('storage.get', 'dluma.storage.get(key, [fallback]) -> scalar|nil, error?', ['key: string', '[fallback: boolean|number|string]']),
            operation('storage.set', 'dluma.storage.set(key, value) -> true|nil, error?', ['key: string', 'value: boolean|number|string']),
            operation('storage.delete', 'dluma.storage.delete(key) -> boolean|nil, error?', ['key: string']),
        ],
        configuration: [
            operation('configuration.get', 'dluma.configuration.get(key) -> scalar|nil, error?', ['key: string']),
        ],
        world: [
            operation('world.find', 'dluma.world.find(name, [expected_class]) -> object|nil, error?', ['name: string', '[expected_class: string]'], {
                insertText: 'find(${1:"ExactRuntimeName"}${2:, "ExpectedClass"})',
            }),
            operation('world.set_enabled_many', 'dluma.world.set_enabled_many(names, enabled, [options]) -> result|nil, error?', ['names: string[]', 'enabled: boolean', '[options: table]']),
        ],
        events: [namespace('sensor', 'Sensor state and subscriptions are owner-bound and dispatched on the game thread.')],
        'events.sensor': [
            operation('events.sensor.is_inside', 'dluma.events.sensor.is_inside(name) -> boolean|nil, error?', ['name: string'], {
                insertText: 'is_inside(${1:"DLUMA_Sensor"})',
            }),
        ],
        player: [
            operation('player.local', 'dluma.player.local_player() -> player|nil, error?'),
            operation('player.variable_names', 'dluma.player.variable_names() -> string[]|nil, error?'),
            operation('player.vehicle_state', 'dluma.player.vehicle_state() -> vehicle_state|nil, error?'),
        ],
        ui: [
            operation('ui.subtitle.show', 'dluma.ui.send_subtitle(text, seconds, [force]) -> true|nil, error?', ['text: string', 'seconds: number', '[force: boolean]']),
            operation('ui.subtitle.update', 'dluma.ui.update_subtitle(text, [fade]) -> true|nil, error?', ['text: string', '[fade: boolean]']),
            operation('ui.quest_hud.snapshot', 'dluma.ui.quest_hud() -> hud|nil, error?'),
            operation('ui.quest_hud.override', 'dluma.ui.override_quest_hud(target_id, text) -> override|nil, error?', ['target_id: string', 'text: string']),
            operation('ui.toast', 'dluma.ui.toast(message) -> nil, error', ['message: string']),
        ],
        audio: [
            namespace('bank', 'Manifest-approved CSB banks use owner-bound opaque tokens.'),
            namespace('cue', 'CSB cue operations are bounded and owner-scoped.'),
            namespace('music', 'Exclusive stock-music ownership is process-wide and transactional.'),
            operation('audio.radio_dialog.play', 'dluma.audio.play_radio_dialog(speaker, group) -> true|nil, error?', ['speaker: string', 'group: string']),
            operation('audio.cue.play', 'dluma.audio.play(cue, [options]) -> sound|nil, error?', ['cue: string', '[options: table]']),
            operation('audio.cue.stop', 'dluma.audio.stop(sound, [fade_seconds]) -> true|nil, error?', ['sound', '[fade_seconds: number]']),
        ],
        'audio.bank': [
            operation('audio.bank.open', 'dluma.audio.bank.open(companion_token, bank_id) -> bank_token|nil, error?', ['companion_token: integer', 'bank_id: string']),
        ],
        'audio.cue': [
            operation('audio.cue.duration', 'dluma.audio.cue.duration(bank_token, cue) -> integer|nil, error?', ['bank_token: integer', 'cue: string']),
            operation('audio.cue.play', 'dluma.audio.cue.play(bank_token, cue, [options]) -> cue_token|nil, error?', ['bank_token: integer', 'cue: string', '[options: table]']),
            operation('audio.cue.state', 'dluma.audio.cue.state(cue_token) -> cue_state|nil, error?', ['cue_token: integer']),
            operation('audio.cue.seek', 'dluma.audio.cue.seek(cue_token, position_ms) -> true|nil, error?', ['cue_token: integer', 'position_ms: integer']),
            operation('audio.cue.set_gain', 'dluma.audio.cue.set_gain(cue_token, gain) -> true|nil, error?', ['cue_token: integer', 'gain: 0..4']),
            operation('audio.cue.set_filter', 'dluma.audio.cue.set_filter(cue_token, wet_mix, cutoff_hz) -> nil, error', ['cue_token: integer', 'wet_mix: 0..1', 'cutoff_hz: 20..18000']),
            operation('audio.cue.stop', 'dluma.audio.cue.stop(cue_token, [fade_out_ms]) -> true|nil, error?', ['cue_token: integer', '[fade_out_ms: 0..60000]']),
        ],
        'audio.music': [
            operation('audio.music.acquire', 'dluma.audio.music.acquire([fade_out_ms]) -> music_token|nil, error?', ['[fade_out_ms: 0..60000]']),
            operation('audio.music.state', 'dluma.audio.music.state(music_token) -> music_state|nil, error?', ['music_token: integer']),
            operation('audio.music.set_filter', 'dluma.audio.music.set_filter(music_token, wet_mix, cutoff_hz) -> nil, error', ['music_token: integer', 'wet_mix: 0..1', 'cutoff_hz: 20..18000']),
            operation('audio.music.release', 'dluma.audio.music.release(music_token, [resume_delay_ms]) -> true|nil, error?', ['music_token: integer', '[resume_delay_ms: 0..60000]']),
        ],
        quests: [
            operation('quests.advance_to_phase', 'dluma.quests.advance_to_phase(quest_id, phase_id) -> true|nil, error?', ['quest_id: string', 'phase_id: string'], {
                insertText: 'advance_to_phase(${1:"DLUMA_Quest"}, ${2:"DLUMA_Phase"})',
            }),
            operation('quests.start', 'dluma.quests.start(quest_id, [phase_id]) -> nil, error', ['quest_id: string', '[phase_id: string]']),
        ],
        actions: [
            operation('actions.chain.run', 'dluma.actions.run_chain(name) -> true|nil, error?', ['name: string'], {
                insertText: 'run_chain(${1:"DLUMA_ActionChain"})',
            }),
        ],
        companion: [
            operation('companion_content.open', 'dluma.companion.open(relative_path) -> artifact|nil, error?', ['relative_path: string']),
        ],
        companion_content: [
            operation('companion_content.open', 'dluma.companion_content.open(relative_path) -> companion_token|nil, error?', ['relative_path: string']),
            operation('companion_content.entry.read', 'dluma.companion_content.read_entry(companion_token, entry_path) -> string|nil, error?', ['companion_token: integer', 'entry_path: string']),
        ],
        mod: [
            api('on_load', 'mod:on_load(callback) -> token|nil, error?', 'Registers a callback when the Lua package starts.', { kind: methodKind, parameters: ['callback: function'] }),
            api('on_unload', 'mod:on_unload(callback) -> token|nil, error?', 'Registers a callback before the Lua package stops.', { kind: methodKind, parameters: ['callback: function'] }),
            api('on_map_load', 'mod:on_map_load(callback) -> token|nil, error?', 'Registers a callback when a map becomes active.', { kind: methodKind, parameters: ['callback: function'] }),
            api('on_map_unload', 'mod:on_map_unload(callback) -> token|nil, error?', 'Registers a callback before the active map exits.', { kind: methodKind, parameters: ['callback: function'] }),
            operation('events.update.subscribe', 'mod:on_update(callback) -> token|nil, error?', ['callback: function'], { kind: methodKind }),
            api('on_key', 'mod:on_key(key, callback) -> token|nil, error?', 'Registers a named key callback.', { kind: methodKind, parameters: ['key: string', 'callback: function'] }),
            operation('events.sensor.enter', 'mod:on_sensor_enter(name, callback) -> token|nil, error?', ['name: string', 'callback: function'], {
                kind: methodKind,
                insertText: 'on_sensor_enter(${1:"DLUMA_Sensor"}, function(${2:ctx}, ${3:sensor})\n\t$0\nend)',
            }),
            operation('events.sensor.leave', 'mod:on_sensor_leave(name, callback) -> token|nil, error?', ['name: string', 'callback: function'], { kind: methodKind }),
            operation('events.interaction.begin', 'mod:on_interact_begin(name, callback) -> token|nil, error?', ['name: string', 'callback: function'], { kind: methodKind }),
            operation('events.enemy_killed', 'mod:on_enemy_killed(callback) -> token|nil, error?', ['callback: function'], { kind: methodKind }),
            operation('quest_phases.provider.register', 'mod:handle_quest_phase(handler_id, callback) -> token|nil, error?', ['handler_id: string', 'callback: function(ctx, phase)'], {
                kind: methodKind,
                insertText: 'handle_quest_phase(${1:"handler_id"}, function(${2:ctx}, ${3:phase})\n\t$0\nend)',
            }),
            operation('quest_phases.observer.register', 'mod:observe_quest_phase(contract_id, callback) -> token|nil, error?', ['contract_id: string', 'callback: function(ctx, phase)'], {
                kind: methodKind,
                insertText: 'observe_quest_phase(${1:"provider.mod:handler_id"}, function(${2:ctx}, ${3:phase})\n\t$0\nend)',
            }),
            operation('configuration.changed', 'mod:on_config_changed(callback) -> token|nil, error?', ['callback: function(ctx, change)'], { kind: methodKind }),
            api('watch', 'mod:watch(predicate, options) -> token|nil, error?', 'Polls a bounded predicate and emits edge callbacks.', { kind: methodKind, parameters: ['predicate: function', 'options: table'] }),
            operation('core.token.cancel', 'mod:off(token) -> true|nil, error?', ['token: integer'], { kind: methodKind }),
        ],
        object: [
            operation('world.position', 'object:position() -> vec3|nil, error?', [], { kind: methodKind }),
            operation('world.forward_direction', 'object:forward_direction() -> vec3|nil, error?', [], { kind: methodKind }),
            operation('world.local_enabled', 'object:is_enabled() -> boolean|nil, error?', [], { kind: methodKind }),
        ],
        player_handle: [
            operation('player.position', 'player:position() -> vec3|nil, error?', [], { kind: methodKind }),
            operation('player.look_direction', 'player:look_direction() -> vec3|nil, error?', [], { kind: methodKind }),
            operation('player.body_direction', 'player:body_direction() -> vec3|nil, error?', [], { kind: methodKind }),
            operation('player.health', 'player:health() -> number|nil, error?', [], { kind: methodKind }),
            operation('player.max_health', 'player:max_health() -> number|nil, error?', [], { kind: methodKind }),
            operation('player.alive', 'player:is_alive() -> boolean|nil, error?', [], { kind: methodKind }),
            operation('player.variable_get', 'player:variable(name) -> snapshot|nil, error?', ['name: string'], { kind: methodKind }),
            operation('player.variable_set', 'player:set_variable(name, value) -> true|nil, error?', ['name: string', 'value: number'], { kind: methodKind }),
            operation('player.variable_reset', 'player:reset_variable(name) -> boolean|nil, error?', ['name: string'], { kind: methodKind }),
            api('watch_variable', 'player:watch_variable(name, callback, [options]) -> token|nil, error?', 'Watches a copied allowlisted PlayerVariables snapshot using the bounded timer service.', { kind: methodKind, parameters: ['name: string', 'callback: function', '[options: table]'], capability: 'player.read', permission: 'player.read', status: 'gated' }),
        ],
        cond: [
            api('true_', 'dluma.cond.true_() -> predicate', 'Returns a predicate that always succeeds.'),
            api('false_', 'dluma.cond.false_() -> predicate', 'Returns a predicate that always fails.'),
            api('all', 'dluma.cond.all(...predicates) -> predicate', 'Combines predicates and preserves structured errors.'),
            api('any', 'dluma.cond.any(...predicates) -> predicate', 'Combines predicates and preserves structured errors.'),
            api('not_', 'dluma.cond.not_(predicate) -> predicate', 'Negates a predicate without swallowing errors.', { parameters: ['predicate: function'] }),
            api('inside_sensor', 'dluma.cond.inside_sensor(name) -> predicate', 'Builds an exact SensorArea predicate.', { parameters: ['name: string'], capability: 'events.sensor', permission: 'events.sensor', status: 'gated' }),
            api('object_enabled', 'dluma.cond.object_enabled(name_or_object) -> predicate', 'Builds an object-enabled predicate.', { parameters: ['name_or_object'], capability: 'world.local_enable', permission: 'world.query', status: 'gated' }),
            api('player_health_below', 'dluma.cond.player_health_below(fraction) -> predicate', 'Builds a player-health predicate.', { parameters: ['fraction: 0..1'], capability: 'player.vitals', permission: 'player.read', status: 'gated' }),
            api('player_health_above', 'dluma.cond.player_health_above(fraction) -> predicate', 'Builds a player-health predicate.', { parameters: ['fraction: 0..1'], capability: 'player.vitals', permission: 'player.read', status: 'gated' }),
            api('player_health_between', 'dluma.cond.player_health_between(minimum, maximum) -> predicate', 'Builds an inclusive player-health predicate.', { parameters: ['minimum: 0..1', 'maximum: 0..1'], capability: 'player.vitals', permission: 'player.read', status: 'gated' }),
            api('storage_equals', 'dluma.cond.storage_equals(key, scalar) -> predicate', 'Builds an owner-storage equality predicate.', { parameters: ['key: string', 'scalar'] }),
            api('elapsed', 'dluma.cond.elapsed(seconds, [options]) -> predicate', 'Builds a monotonic elapsed-time predicate.', { parameters: ['seconds: number', '[options: table]'] }),
            api('distance', 'dluma.cond.distance(left, right, options) -> predicate', 'Builds a copied-position distance predicate.', { parameters: ['left', 'right', 'options: table'], capability: 'world.position', permission: 'world.query', status: 'gated' }),
        ],
        flow: [
            api('sequence', 'dluma.flow.sequence(options) -> sequence', 'Creates a bounded typed sequence.', { parameters: ['options: table'] }),
            api('wait', 'dluma.flow.wait(seconds) -> step', 'Creates a wait step.', { parameters: ['seconds: number'] }),
            api('enable', 'dluma.flow.enable(names, enabled, [options]) -> step', 'Creates a local-enable step.', { parameters: ['names: string[]', 'enabled: boolean', '[options: table]'], capability: 'world.local_enable', permission: 'world.write', status: 'gated' }),
            api('run_chain', 'dluma.flow.run_chain(name) -> step', 'Creates an authored ActionChain step.', { parameters: ['name: string'], capability: 'actions.chain', permission: 'world.write', status: 'gated' }),
            api('subtitle', 'dluma.flow.subtitle(text, duration, [force]) -> step', 'Creates a subtitle step.', { parameters: ['text: string', 'duration: number', '[force: boolean]'], capability: 'ui.subtitles', permission: 'ui.overlay', status: 'gated' }),
            api('radio_dialog', 'dluma.flow.radio_dialog(speaker, group) -> step', 'Creates a radio-dialog step.', { parameters: ['speaker: string', 'group: string'], capability: 'audio.radio_dialog', permission: 'audio.play', status: 'gated' }),
            api('storage_set', 'dluma.flow.storage_set(key, scalar) -> step', 'Creates an owner-storage step.', { parameters: ['key: string', 'scalar'], permission: 'storage.mod' }),
            api('quest_start', 'dluma.flow.quest_start(id) -> step', 'Creates a removed quest-start step; execution returns a structured removed-operation error.', { parameters: ['id: string'], capability: 'quests.native', permission: 'quest.write', status: 'removed' }),
            api('quest_advance', 'dluma.flow.quest_advance(id, phase) -> step', 'Creates a validated authored quest-transition step.', { parameters: ['id: string', 'phase: string'], capability: 'quests.native', permission: 'quest.write', status: 'gated' }),
            api('call', 'dluma.flow.call(callback) -> step', 'Creates a bounded Lua callback step.', { parameters: ['callback: function'] }),
            api('nested', 'dluma.flow.nested(sequence) -> step', 'Creates a nested sequence step.', { parameters: ['sequence'] }),
            api('random', 'dluma.flow.random(steps, [options]) -> step', 'Creates a deterministic owner-seeded random step.', { parameters: ['steps: step[]', '[options: table]'] }),
        ],
        std: [
            api('once', 'dluma.std.once(callback) -> callback', 'Returns a callback that runs once.', { parameters: ['callback: function'] }),
            api('cooldown', 'dluma.std.cooldown(seconds, callback) -> callback', 'Returns a cooldown-wrapped callback.', { parameters: ['seconds: number', 'callback: function'] }),
            api('all', 'dluma.std.all(...predicates) -> predicate', 'Compatibility alias for dluma.cond.all.'),
            api('any', 'dluma.std.any(...predicates) -> predicate', 'Compatibility alias for dluma.cond.any.'),
            api('not_', 'dluma.std.not_(predicate) -> predicate', 'Compatibility alias for dluma.cond.not_.'),
            api('player_health_below', 'dluma.std.player_health_below(fraction) -> predicate', 'Compatibility alias for dluma.cond.player_health_below.', { parameters: ['fraction: number'] }),
            api('inside_sensor', 'dluma.std.inside_sensor(name) -> predicate', 'Compatibility alias for dluma.cond.inside_sensor.', { parameters: ['name: string'] }),
            api('object_enabled', 'dluma.std.object_enabled(name) -> predicate', 'Compatibility alias for dluma.cond.object_enabled.', { parameters: ['name'] }),
            api('require_object', 'dluma.std.require_object(name, [class_name]) -> predicate', 'Builds an exact object lookup predicate.', { parameters: ['name: string', '[class_name: string]'] }),
            api('rule', 'dluma.std.rule(mod, specification) -> token|nil, error?', 'Creates a bounded polling rule.', { parameters: ['mod', 'specification: table'] }),
        ],
    },
};

API.methods = [...API.namespaces.mod, ...API.namespaces.object, ...API.namespaces.player_handle];

function lookupApi(label) {
    return API.root.find(entry => entry.label === label);
}

function lookupMember(namespaceName, label) {
    return (API.namespaces[namespaceName] || []).find(entry => entry.label === label);
}

function lookupMethod(label) {
    return API.methods.find(entry => entry.label === label);
}

function signatureFor(namespaceName, label) {
    if (namespaceName === 'root') return lookupApi(label);
    if (namespaceName === 'method') return lookupMethod(label);
    return lookupMember(namespaceName, label);
}

module.exports = { API, lookupApi, lookupMember, lookupMethod, signatureFor };
