# Local-assist validation record

Validated on 2026-11-08. This record describes observed checks, not a guarantee of full-game compatibility.
No saves, screenshots, game binaries, or extracted/generated game code accompany it.

## Environment

- Windows, x86-64; NVIDIA GeForce RTX 5070 Ti Laptop GPU; approximately 32 GB system RAM.
- BOTW 1.9.0 with both DLC packs version 3.0, supplied locally.
- Upstream source base `bf6146c14c37fee8781a601e5a544b93dff6d75e`, plus the changes on this branch.
- MSYS2 UCRT64 Clang 22.1.8 and the Windows dependencies listed in `docs/BUILD-windows.md`.
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

The in-game menu row was compiled and registered, but automated UI interaction and save/reload persistence
were not separately exercised. There was no full playthrough, test of every movement mode, or validation on
other game builds, GPUs, operating systems, or upstream revisions. Revalidate after syncing upstream.
