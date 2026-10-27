# Custom-map campaign credits-exit crash

## Affected condition

On the validated 2022 DevTools Player build, a Workshop/custom-map campaign
can crash after its credits have completed.  The crash occurs while the game
destroys the completed campaign and returns to the main menu.

Observed dumps are access violations at `gamedll_x64_rwdi.dll+0x01078A40` and
`+0x01072E90`. They are members of one animation-cleanup family: each
dereferences the `IAnimBind` result in `RAX` before making a virtual call. The
value is null on the affected campaign-finished teardown route.

Custom credits can expose the engine lifecycle bug, but a short or custom
`credits_pc.scr` is not by itself malformed and is not rewritten by this fix.

## Native fix

`GameFixes.CustomMapCampaignCreditsExitCrash=1` installs a reversible,
in-memory compatibility profile only after validating the loaded DevTools PE,
all four cleanup tails, their context, and the shared `GetAnimBind` import.
It installs reversible 16-byte atomic routes for aligned tails at `+0x1072E90`,
`+0x10739B0`, and `+0x1078A40`. Their stubs reproduce the original
instructions for normal results and skip only a scoped null virtual call.

The fourth equivalent tail (`+0x1074C13`) is not 16-byte aligned and every
nearby aligned replacement window splits an instruction. Rather than publish
an unsafe code patch, the profile registers a vectored handler which continues
only that exact null access, only in the verified teardown scope, at a small
stub that performs the original following state cleanup. All other access
violations continue through the game's normal exception path.

`GameDI::DestroyActiveLevel` mode `4` records a pending proof only when the
live source label identifies a non-frontend custom map and complete Workshop
project root. The engine returns from that call before its later
`SessionDI::OnDestroy` work reaches the faulting tail. The guard therefore
becomes live only around a session-destroy call whose session map and live
source label match that pending proof, then disarms when that call returns.
The engine may use a different thread for mode `4` and session destruction;
the active tail guard is still bound to the latter thread. Stock campaigns,
ordinary map exits, stale frontend transition
labels, ambiguous ownership, different threads, and unvalidated DLLs follow
the unmodified game path.

The ownership proof is produced by the existing custom-content observer, so
the guard requires `CustomMapAssetIsolation=1`.  This is a dependency for
attribution only; the credits guard does not start or relax the broad
all-or-nothing project-reset transaction.

```ini
[GameFixes]
CustomMapCampaignCreditsExitCrash=1
CustomMapAssetIsolation=1
```

The runtime log records each verified arm/disarm plus total and per-site
suppressed-null counts. Workshop Lua cannot enable, disable, or invoke the
native guard.

## Build support and safety

The first release supports only:

```text
TimeDateStamp: 0x623397AA
SizeOfImage:   0x02082000
```

This is the DevTools Player `gamedll_x64_rwdi.dll` that produced the observed
dump.  Shipping Dying Light has a different fingerprint and remains
unsupported until separately reversed and validated.

Routes and the vectored handler are restored only when the loader still owns
the exact installed state and no guarded teardown is active. If shutdown
cannot prove that, the loader remains pinned for the process lifetime instead
of leaving game code pointing at unloaded code.
