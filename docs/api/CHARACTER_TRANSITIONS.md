# Character transition coordinator

Full character switching is not currently exposed by the DLUMA API. There is no
connected native character backend. The internal coordinator in
`src/core/dlw_character_transition.c` is a foundation for qualified backends;
passing its fake-backend tests does not demonstrate a Player transition.

An appearance preset is insufficient for a second playable character. The
inspected Player implementation changes saved appearance/accessory selection
and the chosen appearance perk. It does not provide separate inventory, XP or
progression. Existing stored/intrinsic progress snapshots produce fingerprints,
not restorable character saves. They must not be passed to this coordinator as
recovery payloads.

The coordinator requires a backend that can preserve and restore inventory,
XP, character progression, appearance and sound together, with an independently
qualified shared-world boundary and reload recovery. These requirements are
checked before capture or mutation. A backend must explicitly classify owned
fields and graphs: a changed world revision or a whole-save copy does not
establish which state belongs to a character.

The backend binds stable mod/map/save and source/target profile identities,
validates both profiles and acquires an authoritative operation lease. It must
exclude concurrent checkpoint/save writers, native owners changing and map
teardown for the whole operation. The source recovery payload and journal must
be durable before target application begins. Payloads contain owned values;
engine addresses and shallow copies of native object graphs are not saves.

| Durable phase | Recovery action |
| --- | --- |
| Prepared or applying | Restore and verify the source character. |
| Active | Preserve target progression durably, then restore the source. |
| Returning or restoring | Retry source restoration without recapturing the target. |

Returning validates the live target incarnation and commits its progression and
the returning marker in one durable transaction. A restart after that commit
cannot capture an older checkpoint over the preserved target. If an active
checkpoint contains a different live character, capture must fail explicitly.
Once returning is durable, the backend must keep target gameplay and checkpoint
writes blocked until recovery finishes, so later progress cannot bypass capture.
Shared world progress may advance while the target is active and
must remain current after return. Restoring a stale whole-world checkpoint is
not a valid character restoration.

Backend contract version 3 requires a durable world-boundary guard before each
mutation. An unfinished guard retains its original classified world evidence
across retries and reloads. Entering again must validate that evidence instead
of treating the changed world as a new baseline. A changed boundary keeps the
journal pending until independently trusted backend repair resolves it; the
coordinator has no repair or acknowledgement operation that discards the failure.
Only successfully verified guards may close and admit a later operation's world
progress. Journal structure version remains 1.

First entry requires either a validated saved target profile or a complete,
explicit initialization configuration. Missing configuration returns
`ConfigurationRequired` before source capture, journal preparation or mutation.
The backend must not silently clone the source or use chapter defaults. An
explicitly authorized disposable-save test plan can initialize its target from
a captured source baseline; this never edits the stored source snapshot or
becomes the production default. Source restoration and recovery remain available
if the starter configuration is later removed. Backends migrating from contract
2 must supply the target readiness callback; persisted journal version is unchanged.

Storage failures, invalid records, owner mismatches and failed restoration
remain explicit errors. A pending journal blocks another transition. It is
cleared only after source restoration and verification succeed and durable
completion succeeds. Retrying restoration is safe only when the backend's
restore operation is idempotent. The owner must retain the journal ID across
interruption and recover it before admitting a new transition.

The portable fake-backend harness covers repeated transitions, independent
target/world progression, partial application, storage/capture/restoration
failures, interrupted application and atomic target capture, malformed recovery
records, foreign scope/profile, wrong live checkpoint character, durable world
failure refusal across retry and simulated coordinator restart,
and reentrant callbacks. It uses synthetic state and never reads or changes
game saves. Native cutscene/checkpoint/reload acceptance remains required using
backup or disposable saves before a public capability can be enabled.

The separate internal codec in `src/core/dlw_character_levels.c` preserves a
pointer-free level section. It contains all eight slots: None, Runner, Fighter,
Status, Reputation, Legend, Driver and Hellraid. Each slot preserves two 16-bit
values, 64-bit total XP and 32-bit current XP. It does not contain inventory,
quests, appearance, sound or shared-world data.

Its versioned DLUMA format uses little-endian values and canonical names/order.
The decoder deliberately accepts reordered records and the recognized
`StatusLevel` alias for Status. It requires every slot exactly once, rejects
unknown names and malformed/trailing data, and accepts a legacy 64-bit current
XP field only when conversion to 32 bits is lossless. Decoding failure leaves
the destination unchanged. The owning journal must supply scope/save identity,
durability and integrity checks.

This codec does not parse native `fs::ifile` bytes or capture/apply live Player
state. Native capture still requires a separately qualified receiver lifetime;
restoration must account for level/XP setter side effects. No native PlayerState
reset or public transition capability is connected to it.
