const assert = require('assert');
const Module = require('module');

class CompletionItem {
    constructor(label, kind) {
        this.label = label;
        this.kind = kind;
    }
}

class MarkdownString {
    constructor(value = '') {
        this.value = value;
    }

    appendCodeblock(value) {
        this.value += value;
    }

    appendMarkdown(value) {
        this.value += value;
    }
}

class SnippetString {
    constructor(value) {
        this.value = value;
    }
}

class Range {
    constructor(start, end) {
        this.start = start;
        this.end = end;
    }
}

class Diagnostic {
    constructor(range, message, severity) {
        this.range = range;
        this.message = message;
        this.severity = severity;
    }
}

const vscode = {
    CompletionItem,
    CompletionItemKind: {
        EnumMember: 1,
        Function: 2,
        Method: 3,
        Module: 4,
    },
    Diagnostic,
    DiagnosticSeverity: { Error: 1, Warning: 2 },
    MarkdownString,
    Range,
    SnippetString,
};

const originalLoad = Module._load;
Module._load = function load(request, parent, isMain) {
    if (request === 'vscode') return vscode;
    return originalLoad.call(this, request, parent, isMain);
};

const extension = require('../extension');
const { API } = require('../src/api');

function positionAt(text, offset) {
    const before = text.slice(0, offset).split(/\r?\n/);
    return { line: before.length - 1, character: before.at(-1).length };
}

function documentFor(text) {
    const lines = text.split(/\r?\n/);
    return {
        getText() {
            return text;
        },
        lineAt(line) {
            return { text: lines[line] || '' };
        },
        offsetAt(position) {
            let offset = 0;
            for (let line = 0; line < position.line; line++)
                offset += lines[line].length + 1;
            return offset + position.character;
        },
        positionAt(offset) {
            return positionAt(text, offset);
        },
    };
}

const source = [
    '-- local ignored = dluma.mod()',
    'local package_mod = dluma.mod()',
    'local literal = "local fake = dluma.mod()"',
    'local object, err = dluma.world.find("DLUMA_Object")',
].join('\n');
const document = documentFor(source);
const end = positionAt(source, source.length);
assert.deepStrictEqual(
    Array.from(extension.modVariableNames(document, end)).sort(),
    ['mod', 'package_mod']);

assert.strictEqual(API.version, '1.0');
assert.match(API.catalogHash, /^[A-F0-9]{64}$/);
assert.strictEqual(
    API.root.find(entry => entry.label === 'api_version').detail,
    'DLUMA API 1.0');
assert.strictEqual(
    API.root.find(entry => entry.label === 'capability').operationId,
    'core.capability.query');
assert.strictEqual(
    API.namespaces['events.sensor'].find(entry => entry.label === 'is_inside').operationId,
    'events.sensor.is_inside');
assert.strictEqual(
    API.namespaces.configuration.find(entry => entry.label === 'get').signature,
    'dluma.configuration.get(key) -> scalar|nil, error?');
assert.strictEqual(
    API.namespaces.quests.find(entry => entry.label === 'start').status,
    'removed');
assert.strictEqual(
    API.namespaces.actions.find(entry => entry.label === 'run_chain').capability,
    'actions.chain');
assert.strictEqual(
    API.namespaces.player_handle.some(entry => entry.label === 'health_ratio'),
    false);
assert.strictEqual(
    Object.prototype.hasOwnProperty.call(API.namespaces, 'debug'),
    false);
assert.strictEqual(
    Object.prototype.hasOwnProperty.call(API.namespaces, 'config'),
    false);

const missingAccess = extension.permissionDiagnostics(document, {
    path: 'mod.dluma.json',
    manifest: { access: [] },
});
assert.strictEqual(
    missingAccess.some(item => item.code === 'dluma.missingAccess' && item.message.includes('world.query')),
    true);
const allowed = extension.permissionDiagnostics(document, {
    path: 'mod.dluma.json',
    manifest: { access: ['world.query'] },
});
assert.strictEqual(allowed.some(item => item.code === 'dluma.missingAccess'), false);

const legacyDocument = documentFor('local mod = dlua.mod { permissions = {} }');
const legacy = extension.permissionDiagnostics(legacyDocument);
assert.strictEqual(legacy.some(item => item.code === 'dluma.legacyApi'), true);
const inlineDocument = documentFor('local mod = dluma.mod { access = {} }');
const inline = extension.permissionDiagnostics(inlineDocument);
assert.strictEqual(inline.some(item => item.code === 'dluma.inlineManifest'), true);

const completion = extension.completionFor({
    label: 'run_chain',
    signature: 'dluma.actions.run_chain(name)',
    documentation: 'Test entry.',
    insertText: 'run_chain(${1:"DLUMA_ActionChain"})',
});
assert(completion.insertText instanceof SnippetString);
assert.strictEqual(completion.insertText.value,
    'run_chain(${1:"DLUMA_ActionChain"})');

assert.strictEqual(
    extension.activeArgumentIndex('dluma.example("a,b", { x = 1, y = 2 }, '),
    2);

process.stdout.write('extension smoke checks: ok\n');
