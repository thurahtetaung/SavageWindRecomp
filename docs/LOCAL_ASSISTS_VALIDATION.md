# Local-assist validation record

## Expanded assist checks

The same-day expansion adds health, equipment durability, arrows, and food alongside stamina. All eight patch
sites were verified against the supplied 1.9.0 NSO. Patch guards validate all sites before changing the patch map.
The C tests now also cover health protection/healing, assist independence, food classification, and rejection of
enemy, animal, and tree names by the equipment classifier. The expanded generated code and runtime compiled.

For resource tests, a disposable copy of [Torphedo's publicly shared 100% Switch save](https://github.com/Torphedo/BOTW-CompletedSaveFile)
was loaded using `SAVAGEWIND_SAVE_DIR`. Download: release `1.0`, asset `Switch-100.zip`, SHA-256
`ea56c064c2674cc926e2273eca2f4ba3c1df143f4dba6af993444168ff25b020`. The downloaded save, extracted contents,
gameplay screenshots, and raw memory/inventory snapshots remain local in ignored folders; they are not redistributed.
The user's own save folders were not used for these resource tests.

| Expanded check | Result |
| --- | --- |
| All five toggle rows visible in the Gameplay menu | Passed |
| Stamina on the 100% save: drain off, protected on, drain restored off | Passed |
| Health: fall damage off, blocked on, normal damage restored off | Passed |
| Arrows: actual firing consumes ammo off, preserves ammo on, consumes again off | Passed |
| Bow durability: actual firing consumes wear off, preserves wear on, consumes again off | Passed |
| Food: eating still heals; the item is retained on and consumed off | Passed |
| Expanded C logic tests and five Python patch-guard tests | Passed |

The food check used a roasted meal with one item remaining. With protection on, eating healed Link without
removing that last item. With protection off, eating healed Link and removed it. Temporary write-tracing code used
to locate the actual Eat path was removed before the final clean build and is not part of the archived feature.
The final stamina regression was repeated on flat ground at Riverside Stable with the downloaded save's normal
`b` sprint binding. An earlier attempt used the copied player's `x` binding and unsuitable terrain, so that attempt
was rejected as an invalid baseline rather than counted as a pass.

Equipment gameplay coverage is bow wear only. The name filter also accepts swords, spears, and shields, but their
individual wear paths have not been separately exercised. Horse stamina is not implemented. Food protection may
also affect other removals using the same methods, including dropping or selling; turn it off for required quest
transactions. Health protection has not been validated against every scripted death or boss mechanic.

## Original stamina prototype record

Validated on 2026-11-08. This record describes observed checks, not a guarantee of full-game compatibility.
No saves, screenshots, game binaries, or extracted/generated game code accompany it.

## Environment

- Windows, x86-64; NVIDIA GeForce RTX 5070 Ti Laptop GPU; approximately 32 GB system RAM.
- BOTW 1.9.0 with both DLC packs version 3.0, supplied locally.
- Upstream source base `bf6146c14c37fee8781a601e5a544b93dff6d75e`, plus the changes on this branch.
- MSYS2 UCRT64 Clang 22.1.8 and the Windows dependencies listed in `docs/BUILD-windows.md`.
- Local Python 3.14.9, Capstone 5.0.9, Unicorn 2.1.4, and lz4 4.4.5 for generation and tests.
- Game code generated locally using Python, lz4, Capstone, and Unicorn; compiled into a separate game library.

## Checks performed

| Check | Result |
| --- | --- |
| C helper: default off and original requested value preserved | Passed |
| C helper: lower stamina protected while enabled, including a request for zero | Passed |
| C helper: recovery and initialization remain possible | Passed |
| C helper: negative sentinel and NaN current value pass through | Passed |
| C helper: disabling restores the requested value | Passed |
| Python: matching build and instruction install exactly one patch | Passed |
| Python: different game build rejected without installing a patch | Passed |
| Python: changed instruction rejected without installing a patch | Passed |
| Python: existing patch conflict rejected without replacing it | Passed |
| Full local generation, native application build, and game-library build | Passed |
| Build helper, using already generated code | Passed |
| Startup and copied-save loading in a disposable data folder | Passed |
| Console reports default off and accepts live toggles | Passed |
| Gameplay sprint sequence: off, on, then off again | Passed |
| Cleanup releases scripted inputs and switches the assist off | Passed |
| Git whitespace check | Passed |

The final gameplay check used a copied Shrine of Resurrection save, a separate control endpoint, and
`tests/local_assists_gameplay.py`. It sampled stamina during scripted sprinting in each state. Both disabled
intervals consumed stamina; the enabled interval retained full stamina. The save had the game's jump-button
swap enabled, so the check used `--sprint-button x`. Normal layouts generally use `b`.

Raw telemetry and runtime logs are retained only in ignored local test folders. The original installed
executable and original save folder were not changed by this validation run.

## Repeating the checks

Use `tools/local_assists_build.ps1` as described in [LOCAL_ASSISTS.md](LOCAL_ASSISTS.md). It runs the C and Python
unit checks before building. `-SkipGenerate` is suitable only when existing generated code matches the source.

For the gameplay check, launch this build with a disposable copy of your save and
`SAVAGEWIND_CONTROL=<unused local port>`. Load the save and stand in a safe, open area. The check moves Link:

```powershell
python tests/local_assists_gameplay.py --port 17778 --sprint-button x
```

The port is an example, not a required value. Use `b` instead of `x` if sprint/jump are not swapped. The script
asserts baseline drain, no drain while enabled, and restored drain after disabling. It releases inputs and
switches the assist off in cleanup. Results go to ignored `local-test-results/` by default.

## Not established by these checks

In the initial stamina-only check, the in-game menu row was compiled and registered, but automated UI interaction
and save/reload persistence were not separately exercised. The expansion later visually confirmed all five menu
rows. There was no full playthrough, test of every movement mode, or validation on
other game builds, GPUs, operating systems, or upstream revisions. Revalidate after syncing upstream.
