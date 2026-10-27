# DLUMA Quest Graph bridge protocol v2

`Dluma.QuestGraph` is a client. The validated Developer Tools loader is the server. The
server chooses a unique random pipe name and launch token for each companion process
and passes them as `--pipe` and `--session-token` arguments. The Editor PID is passed
as `--editor-pid`.

The transport is a local, current-user-only named pipe in byte mode. Each frame starts
with one unsigned 32-bit little-endian byte length followed by exactly that many bytes
of strict UTF-8 JSON. A zero-length frame or a frame larger than 16 MiB terminates the
session. Raw source snapshots are capped at 8 MiB. JSON nesting is capped at 32 levels.
Both peers bound strings, byte buffers, edits, and aggregate collections before
materializing native or managed objects.

Every frame uses the envelope below. Property names are case-sensitive.

```json
{
  "schemaVersion": 2,
  "type": "openDocument",
  "requestId": "32-lowercase-hex-characters",
  "sessionToken": "launch-secret",
  "payload": {}
}
```

Requests receive one envelope with the same request ID and type `result` or `error`.
Notifications use a freshly generated request ID but do not receive a response. The
token and request ID are checked on every frame. Unknown schema versions, types,
responses, IDs, trailing data, invalid UTF-8, or tokens fail the connection closed.

Client requests are `hello`, `applyEdits`, `restoreDocument`, `saveDocument`, `compileDocument`,
`getContext`, `activateDocument`, and `closeDocument`. Server notifications are
`openDocument`, `sourceChanged`, `activateDocument`, `contextSnapshot`, and
`disconnected`. Source changes carry revision, SHA-256, encoding name, and the full
bounded byte snapshot. Edits carry descending, non-overlapping byte ranges and are
admitted only against the stated base revision and hash. No pointer, HWND, engine
handle, or other process-local address is part of the protocol.

`restoreDocument` is the discard-on-close path. It carries the exact opening or
last-saved bytes, their SHA-256 identity, and the baseline `wasModified` savepoint
state. It intentionally has no expected revision or expected source hash: the
Editor replaces the currently active buffer in one undo action, verifies the
resulting bytes, advances the native revision, and restores the savepoint state.

The initial `hello` binds the client PID, expected Editor PID, protocol version, and
packaged executable hash. The server independently verifies the current-user SID,
child PID, executable identity, and exact supported Editor build before accepting any
other request.
