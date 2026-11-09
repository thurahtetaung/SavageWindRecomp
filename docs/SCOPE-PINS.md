# Native companion scope pins

`scopepin x y z` uses the native map manager to place the first available color.
Read `scopepin` again for the request's id, state, slot, and any refusal reason.
The command preserves existing occupied slots and refuses when all five are used.
Requests run on the game thread, only in MainField, and expire after five seconds.

Combined navigation also uses:

```
scopepin move <slot> <oldX> <oldY> <oldZ> <x> <y> <z>
scopepin clear <slot> <oldX> <oldY> <oldZ>
```

Both compare the occupied slot and its previous position on the game thread.
A changed pin returns `conflict: true` and remains untouched. Clearing a slot
that is already empty succeeds without changing anything. Removal calls the
pin's native activation method with false, updating its active list and saved
entry. Status advertises `managed: true` only with the removal helper present.

The manager and placement method are an optional pattern group. The layout is
enabled only for 1.9.0; missing sites or a mismatched object layout refuse the
operation. The method activates the native object, whose normal update maintains
the active icon list and save flags. No save file is edited by the command.

Fieldnotes combines its compass, overlay beacon, and owned scope pin through
POST `/api/game/beacon`. POST `/api/game/scope-pin` and MCP `place_scope_pin`
are aliases for navigation; MCP `set_beacon` also supports `clear: true`.
Fieldnotes persists ownership and deferred clears, reuses its slot when changing
destination, and checks ownership again after reconnecting. Full slots, missing
support, or unknown destination height keep beacon navigation working and return
an explanatory status. Fieldnotes journal pins remain independent saved places.

Validation on 2026-11-09: Windows, BotW 1.9.0, 120 FPS, NVIDIA RTX 5070 Ti Laptop
GPU, driver 610.78, Vulkan. Built regenerated game code and the runtime. Tested
in a hidden instance on copied saves with a separate control port and settings.
Five placements used slots 0 through 4; a sixth refused. Inspected the native
Sheikah Slate map and confirmed all five colored pins. Saved through the game's
System menu and parsed the copied save: MapApp_MapIconNo contained 37..41 and
MapApp_MapIconPos contained all five requested positions. The real save was not
modified by this test. Game-site checks: 46 passed. Fieldnotes tests: 20 passed.

Combined-navigation validation on the same setup: created a managed pin in a
free slot, moved it without consuming another slot, rejected stale expected
coordinates, and preserved a pin changed outside the navigation service. Cleared
the owned pin, verified the active icon list fell to four, and compared all four
other pin positions before and after. Filled the fifth slot with an independent
pin and confirmed navigation fell back to the beacon while preserving it.
Saved through the native System menu and parsed the copied save: the managed
pin was absent and the four independent pin types 38..41 remained. Fieldnotes:
30 tests passed, including serialized actions, old-fork/full-slot fallback,
reconnects, and persisted ownership/deferred clear across a companion restart.
