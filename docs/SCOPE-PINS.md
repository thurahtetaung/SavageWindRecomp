# Native companion scope pins

`scopepin x y z` uses the native map manager to place the first available color.
Read `scopepin` again for the request's id, state, slot, and any refusal reason.
The command preserves existing occupied slots and refuses when all five are used.
Requests run on the game thread, only in MainField, and expire after five seconds.

The manager and placement method are an optional pattern group. The layout is
enabled only for 1.9.0; missing sites or a mismatched object layout refuse the
operation. The method activates the native object, whose normal update maintains
the active icon list and save flags. No save file is edited by the command.

Fieldnotes exposes POST `/api/game/scope-pin` and MCP `place_scope_pin`.
Fieldnotes atlas pins and its overlay beacon remain separate actions.

Validation on 2026-11-09: Windows, BotW 1.9.0, 120 FPS, NVIDIA RTX 5070 Ti Laptop
GPU, driver 610.78, Vulkan. Built regenerated game code and the runtime. Tested
in a hidden instance on copied saves with a separate control port and settings.
Five placements used slots 0 through 4; a sixth refused. Inspected the native
Sheikah Slate map and confirmed all five colored pins. Saved through the game's
System menu and parsed the copied save: MapApp_MapIconNo contained 37..41 and
MapApp_MapIconPos contained all five requested positions. The real save was not
modified by this test. Game-site checks: 46 passed. Fieldnotes tests: 20 passed.
