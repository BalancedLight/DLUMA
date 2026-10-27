const vscode = require('vscode');
const {
    API,
    lookupApi,
    lookupMember,
    lookupMethod,
    signatureFor,
} = require('./src/api');

const LANGUAGE_SELECTOR = { language: 'lua' };

function enabled() {
    return vscode.workspace.getConfiguration('dyingLightLuaIntellisense').get('enabled', true);
}

function markdownFor(entry) {
    const markdown = new vscode.MarkdownString();
    markdown.isTrusted = false;
    markdown.appendCodeblock(entry.signature || entry.label, 'lua');
    markdown.appendMarkdown(`\n\n${entry.documentation}`);
    if (entry.access && entry.access.length)
        markdown.appendMarkdown(`\n\n**Manifest access:** ${entry.access.map(value => `\`${value}\``).join(', ')}`);
    else if (entry.permission)
        markdown.appendMarkdown(`\n\n**Manifest access:** \`${entry.permission}\``);
    if (entry.capability)
        markdown.appendMarkdown(`\n\n**Runtime capability:** \`${entry.capability}\``);
    if (entry.scopes)
        markdown.appendMarkdown(`\n\n**Scopes:** ${entry.scopes.map(value => `\`${value}\``).join(', ')}`);
    if (entry.thread)
        markdown.appendMarkdown(`\n\n**Thread:** \`${entry.thread}\``);
    if (entry.status === 'gated')
        markdown.appendMarkdown('\n\n> Present in every language; returns a structured unavailable result when its validated binding is not ready.');
    if (entry.status === 'planned')
        markdown.appendMarkdown('\n\n> Planned surface. It is visible for parity but currently returns a structured unavailable result.');
    if (entry.status === 'removed')
        markdown.appendMarkdown('\n\n> Removed in DLUMA API 1.0. Calls return a structured removed-operation result.');
    return markdown;
}

function completionFor(entry) {
    const item = new vscode.CompletionItem(
        entry.label,
        entry.kind || vscode.CompletionItemKind.Function);
    item.detail = entry.detail;
    item.documentation = markdownFor(entry);
    item.insertText = entry.insertText
        ? new vscode.SnippetString(entry.insertText)
        : entry.label;
    item.sortText = entry.label;
    return item;
}

function rangeWord(document, position) {
    return document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
}

function textBefore(document, position) {
    return document.lineAt(position.line).text.slice(0, position.character);
}

function namespaceBeforeMember(prefix) {
    const match = /\bdluma\.((?:[A-Za-z_]\w*\.)*)([A-Za-z_]\w*)?$/.exec(prefix);
    if (!match || !match[1]) return undefined;
    const namespaceName = match[1].slice(0, -1);
    return API.namespaces[namespaceName] ? namespaceName : undefined;
}

function memberContext(document, position) {
    const prefix = textBefore(document, position);
    const namespaceName = namespaceBeforeMember(prefix);
    if (namespaceName) return namespaceName;
    const methodMatch = /\b([A-Za-z_]\w*):([A-Za-z_]\w*)?$/.exec(prefix);
    if (!methodMatch) return undefined;
    return modVariableNames(document, position).has(methodMatch[1])
        ? 'mod'
        : 'method';
}

function apiContext(document, position) {
    return /\bdluma\.([A-Za-z_]\w*)?$/.test(textBefore(document, position));
}

function blankCommentsAndStrings(text) {
    const output = text.split('');
    let index = 0;
    let quote;
    while (index < text.length) {
        if (quote) {
            const start = index;
            if (text[index] === '\\') index += 2;
            else {
                if (text[index] === quote) quote = undefined;
                index += 1;
            }
            for (let cursor = start; cursor < Math.min(index, output.length); cursor++) {
                if (output[cursor] !== '\r' && output[cursor] !== '\n') output[cursor] = ' ';
            }
            continue;
        }
        if (text.startsWith('--[[', index)) {
            const end = text.indexOf(']]', index + 4);
            const stop = end < 0 ? text.length : end + 2;
            for (let cursor = index; cursor < stop; cursor++) {
                if (output[cursor] !== '\r' && output[cursor] !== '\n') output[cursor] = ' ';
            }
            index = stop;
            continue;
        }
        if (text.startsWith('--', index)) {
            const end = text.indexOf('\n', index + 2);
            const stop = end < 0 ? text.length : end;
            for (let cursor = index; cursor < stop; cursor++) output[cursor] = ' ';
            index = stop;
            continue;
        }
        if (text[index] === '"' || text[index] === "'") {
            quote = text[index];
            output[index] = ' ';
        }
        index += 1;
    }
    return output.join('');
}

function modVariableNames(document, position) {
    const text = document.getText();
    const limit = position ? document.offsetAt(position) : text.length;
    const code = blankCommentsAndStrings(text.slice(0, limit));
    const names = new Set(['mod']);
    const pattern = /\b(?:local\s+)?([A-Za-z_]\w*)\s*=\s*dluma\s*\.\s*mod\s*\(\s*\)/g;
    for (const match of code.matchAll(pattern)) names.add(match[1]);
    return names;
}

function permissionRules() {
    const rules = [];
    for (const [namespaceName, entries] of Object.entries(API.namespaces)) {
        for (const entry of entries) {
            if (!entry.permission || entry.status === 'removed') continue;
            let source = entry.source;
            if (!source) {
                source = namespaceName === 'mod'
                    ? `:${entry.label}`
                    : `dluma.${namespaceName}.${entry.label}`;
            } else if (source.includes(':')) {
                source = `:${entry.label}`;
            }
            rules.push({ source, permission: entry.permission });
        }
    }
    return rules;
}

const PERMISSION_RULES = permissionRules();

function diagnostic(document, start, length, message, severity, code) {
    const item = new vscode.Diagnostic(
        new vscode.Range(
            document.positionAt(start),
            document.positionAt(start + Math.max(1, length))),
        message,
        severity);
    item.code = code;
    item.source = 'DLUMA API';
    return item;
}

function permissionDiagnostics(document, manifestInfo) {
    const text = document.getText();
    const code = blankCommentsAndStrings(text);
    const diagnostics = [];
    const legacy = /\bdlua\b/.exec(code);
    if (legacy) {
        diagnostics.push(diagnostic(
            document,
            legacy.index,
            legacy[0].length,
            'Legacy Lua API 2.1 uses dlua and is unsupported. Migrate this package to dluma and mod.dluma.json.',
            vscode.DiagnosticSeverity.Error,
            'dluma.legacyApi'));
    }
    const inlineManifest = /\bdluma\s*\.\s*mod\s*\{/.exec(code);
    if (inlineManifest) {
        diagnostics.push(diagnostic(
            document,
            inlineManifest.index,
            inlineManifest[0].length,
            'DLUMA API 1.0 uses dluma.mod() with no arguments; metadata and access belong in mod.dluma.json.',
            vscode.DiagnosticSeverity.Error,
            'dluma.inlineManifest'));
    }
    if (manifestInfo && manifestInfo.error) {
        diagnostics.push(diagnostic(
            document,
            0,
            1,
            `Cannot parse ${manifestInfo.path}: ${manifestInfo.error}`,
            vscode.DiagnosticSeverity.Error,
            'dluma.invalidManifest'));
        return diagnostics;
    }
    const manifest = manifestInfo && manifestInfo.manifest;
    if (!manifest || !Array.isArray(manifest.access)) return diagnostics;
    const declared = new Set(manifest.access.map(value => String(value).toLowerCase()));
    const reported = new Set();
    for (const rule of PERMISSION_RULES) {
        if (declared.has(rule.permission) || reported.has(rule.permission)) continue;
        const index = code.indexOf(rule.source);
        if (index < 0) continue;
        reported.add(rule.permission);
        diagnostics.push(diagnostic(
            document,
            index,
            rule.source.length,
            `This API requires '${rule.permission}', but it is not declared in mod.dluma.json access.`,
            vscode.DiagnosticSeverity.Error,
            'dluma.missingAccess'));
    }
    return diagnostics;
}

async function manifestForDocument(document) {
    if (!document.uri || !vscode.Uri || !vscode.workspace.fs) return undefined;
    let directory = vscode.Uri.joinPath(document.uri, '..');
    for (let depth = 0; depth < 12; depth++) {
        const candidate = vscode.Uri.joinPath(directory, 'mod.dluma.json');
        let bytes;
        try {
            bytes = await vscode.workspace.fs.readFile(candidate);
        } catch (_) {
            bytes = undefined;
        }
        if (bytes) {
            try {
                return {
                    path: candidate.fsPath || candidate.path,
                    manifest: JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)),
                };
            } catch (error) {
                return {
                    path: candidate.fsPath || candidate.path,
                    error: error instanceof Error ? error.message : String(error),
                };
            }
        }
        const parent = vscode.Uri.joinPath(directory, '..');
        if (parent.path === directory.path) break;
        directory = parent;
    }
    return undefined;
}

function rootNamespaceCompletions() {
    const excluded = new Set(['mod', 'object', 'player_handle']);
    const names = new Set();
    for (const namespaceName of Object.keys(API.namespaces)) {
        const top = namespaceName.split('.')[0];
        if (!excluded.has(top)) names.add(top);
    }
    return Array.from(names, name => completionFor({
        label: name,
        detail: 'DLUMA API 1.0 namespace',
        documentation: `DLUMA \`${name}\` namespace.`,
        kind: vscode.CompletionItemKind.Module,
    }));
}

function activate(context) {
    const diagnosticCollection = vscode.languages.createDiagnosticCollection('dlumaAccess');
    const refreshDiagnostics = async document => {
        if (document.languageId !== 'lua' || !enabled()) {
            diagnosticCollection.delete(document.uri);
            return;
        }
        const manifestInfo = await manifestForDocument(document);
        diagnosticCollection.set(document.uri, permissionDiagnostics(document, manifestInfo));
    };
    for (const document of vscode.workspace.textDocuments) void refreshDiagnostics(document);
    context.subscriptions.push(
        diagnosticCollection,
        vscode.workspace.onDidOpenTextDocument(document => void refreshDiagnostics(document)),
        vscode.workspace.onDidChangeTextDocument(event => void refreshDiagnostics(event.document)),
        vscode.workspace.onDidCloseTextDocument(document => diagnosticCollection.delete(document.uri)),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('dyingLightLuaIntellisense.enabled')) {
                for (const document of vscode.workspace.textDocuments)
                    void refreshDiagnostics(document);
            }
        }),
    );

    const completion = vscode.languages.registerCompletionItemProvider(LANGUAGE_SELECTOR, {
        provideCompletionItems(document, position) {
            if (!enabled()) return undefined;
            const member = memberContext(document, position);
            if (member === 'method') return API.methods.map(completionFor);
            if (member) return (API.namespaces[member] || []).map(completionFor);
            if (apiContext(document, position))
                return API.root.map(completionFor).concat(rootNamespaceCompletions());
            return undefined;
        },
    }, '.', ':');

    const hover = vscode.languages.registerHoverProvider(LANGUAGE_SELECTOR, {
        provideHover(document, position) {
            if (!enabled()) return undefined;
            const wordRange = rangeWord(document, position);
            if (!wordRange) return undefined;
            const word = document.getText(wordRange);
            const prefix = document.lineAt(position.line).text.slice(0, wordRange.start.character);
            const namespaceMatch = /\bdluma\.((?:[A-Za-z_]\w*\.)+)$/.exec(prefix);
            const methodMatch = /\b([A-Za-z_]\w*):$/.exec(prefix);
            let entry;
            if (namespaceMatch) {
                const namespaceName = namespaceMatch[1].slice(0, -1);
                entry = lookupMember(namespaceName, word);
            } else if (methodMatch) {
                entry = modVariableNames(document, position).has(methodMatch[1])
                    ? lookupMember('mod', word)
                    : lookupMethod(word);
            } else {
                entry = lookupApi(word);
            }
            return entry ? new vscode.Hover(markdownFor(entry), wordRange) : undefined;
        },
    });

    const signatures = vscode.languages.registerSignatureHelpProvider(LANGUAGE_SELECTOR, {
        provideSignatureHelp(document, position) {
            if (!enabled()) return undefined;
            const prefix = textBefore(document, position);
            const methodMatch = /\b([A-Za-z_]\w*):([A-Za-z_]\w*)\([^()]*$/.exec(prefix);
            if (methodMatch) {
                const namespaceName = modVariableNames(document, position).has(methodMatch[1])
                    ? 'mod'
                    : 'method';
                return buildSignatureHelp(signatureFor(namespaceName, methodMatch[2]), prefix);
            }
            const match = /\bdluma\.((?:[A-Za-z_]\w*\.)*)([A-Za-z_]\w*)\([^()]*$/.exec(prefix);
            if (!match) return undefined;
            const namespaceName = match[1] ? match[1].slice(0, -1) : 'root';
            return buildSignatureHelp(signatureFor(namespaceName, match[2]), prefix);
        },
    }, '(', ',');

    context.subscriptions.push(completion, hover, signatures);
}

function buildSignatureHelp(entry, prefix) {
    if (!entry || !entry.parameters) return undefined;
    const signature = new vscode.SignatureInformation(entry.signature, entry.documentation);
    signature.parameters = entry.parameters.map(
        parameter => new vscode.ParameterInformation(parameter));
    const help = new vscode.SignatureHelp();
    help.signatures = [signature];
    help.activeSignature = 0;
    help.activeParameter = Math.min(
        activeArgumentIndex(prefix),
        Math.max(0, signature.parameters.length - 1));
    return help;
}

function activeArgumentIndex(prefix) {
    const open = prefix.lastIndexOf('(');
    if (open < 0) return 0;
    const argumentsText = prefix.slice(open + 1);
    let quote;
    let depth = 0;
    let commas = 0;
    for (let index = 0; index < argumentsText.length; index++) {
        const character = argumentsText[index];
        if (quote) {
            if (character === '\\') index += 1;
            else if (character === quote) quote = undefined;
            continue;
        }
        if (character === '"' || character === "'") quote = character;
        else if (character === '(' || character === '{' || character === '[') depth += 1;
        else if (character === ')' || character === '}' || character === ']') depth = Math.max(0, depth - 1);
        else if (character === ',' && depth === 0) commas += 1;
    }
    return commas;
}

function deactivate() {}

module.exports = {
    activate,
    deactivate,
    completionFor,
    manifestForDocument,
    permissionDiagnostics,
    modVariableNames,
    activeArgumentIndex,
};
