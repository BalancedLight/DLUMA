# Boss phase mod handlers

Boss scenario scripts can call a registered mod handler from a phase:

```scr
CallMod("example.mod", "on_phase", "{\"wave\":2}")
```

Use the stable mod ID and the handler ID registered by that mod. The third
argument is a string containing a JSON object, with the same size and depth
limits as other mod handler calls. All three arguments are required.

When its phase runs, the instruction queues one handler call for a subsequent
loader frame on the game thread. The scenario does not wait for the callback.
Entering that phase again resets the instruction. The handler receives the map
generation, boss scenario path, and phase name. Queued calls are discarded if
the map changes or the registered handler's owner or registration is retired
before dispatch. Native producers copy metadata without reading a mod registry
or invoking mod code on their callback thread. Acceptance means queued, not
delivered. A change to either named-handler registry before the authoritative
pump conservatively retires queued native calls; inspect the retirement log
before deciding whether to issue a new request. A missing handler, full queue, or failed callback is logged and
the scenario continues. Rollback does not undo mod handler effects.

The game owns each instruction until its boss phase is destroyed. After factory
installation, DLUMA retains its factory code, original relay, and game module
until process exit. Stopping custom work drains admitted callbacks; stock
shutdown can then destroy remaining instructions. Later instruction execution
only marks completion, and stock factory calls continue through the retained
original factory.

The loader recognizes `DLUMAPeriodicChoiceStart` with three authored names
(flashback template phase, chatter template phase, and chatter group), and
`DLUMAPeriodicChoiceStop` with no arguments. Names are 1–127 ASCII letters,
digits, underscores, hyphens, or periods; the two phase names must differ.
Periodic choice execution is **not available in this build**: valid commands
log that status and complete without starting a scheduler. Invalid commands
are logged and also complete, so they do not block a phase transition. Do not
use these commands in a scenario expecting an effect yet. The offline scheduler
and template checks are not evidence of an enabled game integration.

## Windows adapter

`dlw_boss_actions.c` currently permits factory installation only for the exact
Developer Tools Player profile below, from its current pre-original-initializer
entry. It checks the shared exact Player/A18 engine profile, the actual outer
export caller and thread, and unpublished engine manager/factory state. The
entry proof is checked again immediately before publication. Warm calls, retail,
and Editor startup remain unsupported. The legacy internal start function refuses
installation without this current entry proof.

The adapter also checks the game DLL hash, PE identity, executable sections,
factory and parser instructions, base virtual table, and imported CRT allocator.
A mismatch disables `CallMod`. The qualified Player game DLL has SHA-256
`CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95`,
timestamp `0x623397AA`, and image size `0x02082000`.

| Contract | Windows RVA or layout |
| --- | --- |
| Instruction factory | `0x00A916F0`; parser cursor, owning phase |
| Factory entry copied to trampoline | 15 bytes: three register stores; no relative operands |
| Command name | Parser current node, string pointer at `+56` |
| Parameter list and count | Current node `+72`, `+88` |
| String evaluation | `0x01289FE0`; five-word parameter iterator |
| String allocator | Game's `MSVCR100.dll`; `free` import at `0x013CC198` |
| Base virtual table | `0x017C1FA8`; seven function pointers |
| Instruction header | Virtual table `+0`, completion byte `+8`, phase pointer `+16` |
| Windows virtual methods | Destroy, load, execute, reset, save rollback, rollback, debug |
| Phase name | Owning phase string at `+16` |
| Scenario path | Phase owner pointer at `+0`, owner's string at `+72` |

The phase constructor at `0x00A90990` creates instructions through the factory
and calls their load method. Phase destruction at `0x00A90BC0` invokes their
deleting destructor. Boss update at `0x00A8EE30` checks the completion byte and
calls virtual slot 2. Phase entry resets slot 3; its first start saves rollback
state through slot 4 at `0x00A90CC0`. These Windows slots define this adapter's
virtual table.

The previously inspected Editor DLL hash
`8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54`
does not establish an Editor startup contract. Its factory is not installed by
the current cold-start adapter.
