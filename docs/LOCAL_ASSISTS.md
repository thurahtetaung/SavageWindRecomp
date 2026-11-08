# Local gameplay assists

This branch preserves a Windows prototype of optional gameplay assists. It is a local modification
of Savage Wind, not a released upstream feature. The rest of the upstream README and license remain applicable.

## Scope and status

- Base: upstream commit `bf6146c14c37fee8781a601e5a544b93dff6d75e`.
- Tested game: BOTW 1.9.0, main build ID `cd57b23fa4bbad65803d9788c01821ee`.
- Custom version: `0.4.1-alpha.4-local-assists`.
- Adds five independent toggles, off by default, under **Options > Gameplay**.
- Stamina: prevents decreases while allowing recovery and initialization.
- Health: protects decreases at two verified player-health stores while allowing healing.
- Equipment durability: suppresses durability damage for sword, spear, bow, and shield actor names. This also
  protects enemy equipment; enemy, animal, tree, and player health are not classified as equipment.
- Arrows: skips the verified arrow-consumption routine. You still need arrows in inventory.
- Food: protects edible items in the verified inventory removal paths, including the UI Eat path.
- Does not add horse stamina, a cheat-file interpreter, or general mod loading. BOTW has no TOTK-style battery.
- Built and tested on Windows with an NVIDIA RTX 5070 Ti Laptop GPU. Other systems are not validated.
- Keep the normal installation for comparison. Use separate saves while testing; progress is not synchronized.

## What is archived

The repository contains upstream source and licensed dependencies, local source changes, build helpers, and tests.
It contains no game dumps, extracted files, generated game code, keys, firmware, personal saves, compiled game
library, or gameplay screenshots. Those remain local. The source itself is copyrighted open-source software;
its redistribution is governed by `LICENSE` and the notices accompanying dependencies in `third_party/`.
The existing credits and licenses are retained. This archive makes no claim that source code is copyright-free.

## Build from a fresh clone on Windows

1. Clone your fork and check out `local-assists`.
2. Follow [BUILD-windows.md](BUILD-windows.md). The upstream `tools/setup_windows.ps1` installs MSYS2, its UCRT64
   compiler and libraries, and a Python environment with `lz4`, `capstone`, and `unicorn`.
3. Supply your own extracted, updated 1.9.0 ExeFS. It must contain `main`, `main.npdm`, `rtld`, `sdk`, and `subsdk0`.
   The matching merged RomFS and optional DLC are needed to play. This repository supplies none of them.
4. Close any game process using this checkout's executable, then run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools/local_assists_build.ps1 -ExeFSPath 'E:\MyGame\exefs'
   ```

   For a different Python installation, pass `-PythonExe 'C:\path\python.exe'`. For a non-default MSYS2
   installation, pass `-MsysRoot 'C:\path\msys64'`. `-Jobs 16` sets build concurrency.
   A source Python environment with the three packages above is required; no frozen generator is needed.

5. The script runs the C logic tests and Python patch-guard tests, generates the game code locally, builds the
   application and game library, then installs the local library into `local-data/extracted/`.
   It also copies the upstream Fira Sans fonts, their license, and the app icon into the build folder.
6. Inside `local-data/extracted/`, copy or create directory junctions to your own `exefs`, `romfs`, and optional
   `aoc1` / `aoc2` folders. Use a real `extracted` directory, with the library directly inside it. Do not junction
   the whole installed data folder: the experimental library and settings must stay separate.
7. If continuing an existing game, copy its entire `save` folder into `local-data/save` while both games are closed.
   Retain the original. A copy is independent; later progress in one installation does not appear in the other.
8. Launch:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools/local_assists_launch.ps1
   ```

The launcher adds MSYS2's UCRT64 DLL folder only to the child process environment and uses `local-data` by default.
Its optional `-DataRoot` must identify a separate experimental data folder. Runtime dependencies remain in MSYS2;
this is a development build, not a self-contained release ZIP. Building does not require any key values in scripts.

For runtime-only changes, `-SkipGenerate` reuses locally generated code. Regenerate after changing the lifter,
patch generator, game files, or headers used by the generated code. Do not mix libraries and executables built
against different game-library IDs. If prompted to set up files again, rebuild with this source and restore the
matching `build/swgame.dll` and `.id`; an unmodified release generator does not include this assist.

## Using the assist

Open the game's Options, choose Gameplay, and toggle each assist individually. Normal menu persistence saves
`local_infinite_stamina`, `local_infinite_health`, `local_infinite_durability`, `local_infinite_arrows`, and
`local_infinite_food` as `0` or `1` in `savagewind.ini`. An absent key starts with the default, off.

The F12 console also accepts:

```text
assist stamina
assist stamina on
assist stamina off
assist health on
assist durability on
assist arrows on
assist food on
```

Console changes apply immediately; use the game menu for normal settings persistence. A normal launch exposes no
control socket. For local automation only, start with `SAVAGEWIND_CONTROL=<unused local port>` and use
`tools/swctl.py --port <port> ...` as described in [CONTROL.md](CONTROL.md).

## Implementation

`tools/local_assists.py` checks the complete build ID and every expected instruction before installing any patches.
A mismatched build, changed instruction, or overlapping patch fails generation. There are eight verified sites:
one stamina store, two health stores, arrow-consumption entry, equipment-damage load, and three food-removal sites.
The food UI hook exits through an existing mutex-unlock block before consuming the item; healing is applied by
the caller and remains functional. Original instructions run when each assist is disabled.

`runtime/local_assists.c` returns the requested bits unchanged while disabled. While enabled, it keeps the old
value only when both values are finite, the current value is positive, and the requested nonnegative value is
smaller. Initialization, increases, negative sentinels, and non-finite values pass through. It never invents a
maximum or scans arbitrary memory. An atomic flag communicates the toggle across runtime threads.

Health uses a separate integer helper. Equipment checks the actor name through the verified BaseProc layout,
instead of applying the public cheat's broad life/damage suppression to all actors. Food checks item type and
use classification, protecting edible inventory entries without classifying key items or equipment as food.

`runtime/local_assists_control.c` registers the console command; `settings.c` loads and writes the settings;
`ui/game_options.mm` adds the gameplay rows. `tools/gen.py` installs the verified patches, and the Makefile includes
the new patch module in the game-library identity. The custom version label distinguishes this build in logs.

## Validation and practical limits

On 2026-11-08, the local build passed C logic tests, full generation and compilation, startup, copied-save loading,
and sprint testing with the assist off, on, then off again. It prevented drain while enabled and normal drain
returned when disabled. See [LOCAL_ASSISTS_VALIDATION.md](LOCAL_ASSISTS_VALIDATION.md) for the recorded checks.

Tests cover the helper's behavior and patch guards. They do not establish that every game mechanic, cutscene,
temporary stamina bonus, swimming state, or climbing action is unaffected. There is no full-playthrough claim.
Only the verified paths are protected. Other game versions intentionally fail to generate.
Horse stamina remains unimplemented. Equipment tests cover bow wear; not every melee, shield, elemental, or
scripted destruction path is established by those checks. Health protection is not a guarantee against every
scripted death. Food protection may also prevent dropping, selling, or scripted removal through the same paths;
turn it off for quests or transactions that require consumption. Raw cooking ingredients are not generally protected.

Behavioral reference used during investigation: [zeldaret/botw](https://github.com/zeldaret/botw). No decompiled
implementation files are included in this fork's local changes; hooks were checked against the supplied 1.9.0 NSO.
The [Gamerjin cheat discussion](https://gbatemp.net/threads/the-legend-of-zelda-breath-of-the-wild-tid-01007ef00011e000-bid-de0b6ac4efc1dda7.673121/)
provided candidate health, stamina, arrow, and durability sites. This implementation uses verified native hooks,
not the discussion's broad durability code cave.

## Keeping the fork synchronized

Keep `main` as the upstream mirror and keep local changes on `local-assists`. For this fork:

```powershell
git remote add upstream https://github.com/Revivedx/SavageWindRecomp.git
# If upstream already exists, omit the preceding line.
git fetch upstream
git switch main
git merge --ff-only upstream/main
git push origin main
git switch local-assists
git merge main
```

GitHub's **Sync fork** button updates `main` only; it does not update the feature branch. After using it, run
`git fetch origin`, switch to `local-assists`, and merge `origin/main` instead of the local `main` above.
Resolve any conflicts explicitly, rerun the build and tests, repeat the gameplay check on copied saves, and push
`local-assists`. Do not bypass a failed build-ID or instruction guard to make an upstream update compile.
If upstream implements equivalent functionality, review it before retaining this patch.

This archive has not been submitted upstream. Upstream contributions should follow [CONTRIBUTING.md](../CONTRIBUTING.md),
including its requirement to target the upstream `test` branch.

## Rollback and troubleshooting

- Toggle each assist off to restore its original behavior without restarting.
- Use the normal game's shortcut to return to the untouched installed release and its own saves.
- An undefined `rt_local_stamina` indicates mixed source/runtime/library versions: rebuild both artifacts.
- A missing DLL indicates MSYS2 is absent or its UCRT64 bin folder is not on this process's PATH.
- A generation mismatch is a version/patch conflict, not a setting to override.
- Keep gameplay logs and screenshots local. Review any material before posting a bug report.
