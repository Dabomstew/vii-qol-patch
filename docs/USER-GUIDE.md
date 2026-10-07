# User guide

For the first setup, run Prepare Game and choose **Prepare / Resume**. To update
the patch or save settings without preparing assets again, use **Install / Update**.

## First installation

Extract the release ZIP to a folder outside the game installation. Do not copy its files into the game folder by hand.

Close Megadimension Neptunia VII and any tool that might keep its files open. Open the game folder containing `NeptuniaVII.exe`, then check whether it already contains `dinput8.dll`.

If `dinput8.dll` is already present, it may belong to another mod. Do not overwrite
it by hand. Prepare Game checks its hash against a list of known VII patch builds,
including manually installed copies. It leaves unknown or modified DLLs untouched.
If you installed a newer VII patch, try its matching preparer. Resolve conflicts
with other mods before installing. The supported game executable has SHA-256:

```
7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9
```

You can check it in PowerShell from the game folder:

```powershell
Get-FileHash .\NeptuniaVII.exe -Algorithm SHA256
```

Run `VII-Prepare-Game.exe`. It may find a Steam installation automatically; otherwise choose **Game folder...** and select the folder containing `NeptuniaVII.exe` (selecting `CONTENTS` also works).

The preparer checks the supported executable when selecting a folder, and again
before changing installed files. Unsupported installations are refused.

The default data locations are relative to that game folder:

```
vii-speedrun-patch\cache
vii-speedrun-patch\unpacked
vii-speedrun-patch\unpacked.dlc
```

Choose **Cache folder...** or **Unpacked assets...** to use another location, including another drive. Existing paths are loaded into the window. Cache and unpacked assets must use separate, non-overlapping folders; neither may overlap original game data or installer backups. Selecting a new location does not move existing data.

Select **Prepare / Resume**. The tool extracts the game PACs, automatically includes a sibling `DLC` folder when present, prepares supported textures, then installs its matching proxy and updates `vii-patches.ini`. It keeps original PACs intact. DLC output is automatically derived by appending `.dlc` to the unpacked-data location; there is no separate DLC path to set.

Wait for **Preparation complete**, then launch through Steam or your normal shortcut.
**Play** installs the patch, saves the displayed settings and starts the game without
preparing assets first. Supported textures missing from the cache can be added
during play. Opening the utility alone does not start preparation or the game.

Allow roughly 35 GB or more of free space, with room for the cache to grow.
One full base-game and DLC installation used about 21 GB for extracted assets
and 11.9 GB for the texture cache. Your space use may differ.

## Cancel, resume, and repair

**Cancel** stops at a safe boundary. Completed extracted files and prepared cache records are retained, checked on the next run, and reused when valid. Choose **Prepare / Resume** again to continue; it also repairs missing, corrupt, or interrupted managed records.

Closing the window during preparation asks it to stop, then waits until it can
close safely. Completed work remains available for the next run.

If preparation stops with an error, read the message, keep the game closed, correct the reported condition (for example, free space or a proxy conflict), and resume. Do not delete original PACs or the managed unpacked folders to make an error disappear.

The preparer checks the installed DLL before extraction or texture generation.
Updates keep verified before/after copies under `vii-prepare-backups` and record
an in-progress update in `vii-prepare-pending.ini`. If the utility is interrupted
during installation, close the game and choose **Recover interrupted update**, or run:

```powershell
.\VII-Prepare-Game.exe --source 'C:\path\to\Megadimension Neptunia VII' --recover
```

Recovery verifies its snapshot and current files before restoring the prior
installation. A completed update with only pending cleanup is kept. If you edited
files after the interruption, or a backup has changed, recovery stops and reports
the affected path; preserve those files and backups for review. Do not remove the
pending record to bypass this check. Recovery is repeatable if it is interrupted.

Linked Steam-library parents are supported, but the selected game folder itself
and installer-owned files/folders must not be symbolic links or junctions.
Cache output cannot overlap original `CONTENTS`, `DLC`, or installer backups.
Game folder names containing a semicolon are currently unsupported by the VII
installer. Another preparer operating on the same installation is refused.

Run Prepare / Resume again after installing or removing DLC, repairing the game, moving the unpacked data, or changing graphics drivers. A changed GPU, driver, D3DX runtime, or recipe uses a different cache namespace; older records are retained but may no longer be selected.

## Install or update without preparing

Choose **Install / Update** to install the bundled patch and save the displayed
settings. This does not scan textures, extract archives, or launch the game.
Repeating an unchanged update keeps the previous update backup.
Use **Prepare / Resume** when you want to extract assets and prebuild textures.

The window loads the prepared loading options, **Unlock FPS during loads**, and
five optional gameplay choices from the existing INI. Prepared loading
improvements default on; FPS unlock and gameplay changes default off. Explicit
existing choices, including disabled loading options, are retained.
**Reload settings** reads the current file. If you have unsaved changes, choose
**Discard changes** to reload or **Stay** to keep editing. The same prompt appears
when changing the game folder, closing or starting maintenance. If the INI changes while
the window is open, reload it before applying your choices. Rollback, uninstall
and recovery use the saved installation and leave unsaved choices unapplied.
Closing during installation waits for it to finish safely and does not launch
the game afterward.

The window scales with your display. You can select, copy and edit folder paths;
enter full paths for cache and asset folders, such as `C:\Games\VII-cache`.

The CLI supports the same settings and maintenance backend:

```powershell
.\VII-Prepare-Game.exe --source 'C:\Games\Megadimension Neptunia VII' --install
.\VII-Prepare-Game.exe --source 'C:\Games\Megadimension Neptunia VII' --install --set EventSkipBuffer=1
.\VII-Prepare-Game.exe --source 'C:\Games\Megadimension Neptunia VII' --settings
```

`--enable-only` remains an alias for `--install`. `--output CACHE` and
`--assets FOLDER` choose locations; `--set Key=0|1` accepts the nine displayed
feature keys. `--settings` prints saved paths, flags and the INI SHA-256 as JSON.
Scripts can pass that hash to `--if-config-hash HASH` when applying changes to
avoid overwriting settings changed since they were read (an empty hash means no INI existed). `--gui --source GAME`
opens the window without automatically starting work. Plain `--source GAME`
still prepares and resumes.

## Configuration

`vii-patches.ini` is beside `NeptuniaVII.exe`. Close and restart the game after every edit. Relative paths are resolved from the game folder; absolute Windows paths are also supported.

Missing keys are filled from the bundled template. Advanced values, unknown keys and existing comments are retained through the Windows INI API. Windows ANSI and UTF-16 LE profiles are supported; non-ASCII selected paths may convert ANSI to UTF-16 LE. UTF-8 BOM and UTF-16 BE files are refused before installation; save them as UTF-16 LE and reload.

Load timing is always active for supported waits, including blocked story-event
resource queues. Prepare Game moves saved load-only FPS and dungeon preview
choices to `UnlockFPSDuringLoads` and `SuppressDungeonPreviews` and removes the
old timing and queue toggles. Explicit values under the new names take priority.

The release defaults are:

| Setting | Default | Effect |
| --- | ---: | --- |
| `[Patches] MipCache` | `1` | Reuses supported generated textures. |
| `[MipCache] SingleMipCache` | `0` | Uses the original loader for the known single-mip texture path, avoiding redundant cache work. Set `1` to compare the previous caching behavior; full-mip caching stays enabled. |
| `[MipCache] BudgetMiB` / `MaxEntries` | `256` / `512` | Bounds the in-process texture cache. |
| `[MipCache] DiskMode` | `2` | Read and write persistent cache records. |
| `[MipCache] DiskDirectory` | `vii-speedrun-patch\cache` | Persistent cache location. |
| `[Patches] MotionCache` | `1` | Enables the replay motion cache. |
| `[MotionCache] Mode` / `BudgetMiB` | `Replay` / `16` | Replays cached motion data within a 16 MiB budget. |
| `[Patches] LooseFiles` | `1` | Loads checked unpacked assets; uses the game's normal loader if they cannot be used. |
| `[LooseFiles] Directory` | `vii-speedrun-patch\unpacked` | Base extraction location; DLC is the derived `.dlc` folder. |
| `[LooseFiles] Verify` | `0` | Checks expected size; `1` additionally hashes every opened loose file and is slower. |
| `[Patches] NewGameDetector` | `1` | Lets `vii-new-game.asl` detect New Game and start the LiveSplit timer without writing game memory. |
| `[Patches] UnlockFPSDuringLoads` | `0` | Removes the frame cap during detected loads, including blocked story-event resource queues. Available in Prepare Game. Keep global Neptasm FPSUnlock off for capped gameplay. |
| `[LoadTiming] Trace` | `0` | Logs boundaries from the always-active load timing bridge. See [load timing](LOAD-TIMING.md) for partial coverage. |
| `[Patches] SuppressDungeonPreviews` | `0` | Hides optional dungeon preview movies and images in the world-map information window. Available in Prepare Game. |
| `[Patches] AutoSkipEvents` | `0` | Automatically skips eligible ADV events when enabled. |
| `[Patches] BattleAutoSkip` | `0` | Enables battle animation/results skipping when enabled. |
| `[Patches] EventSkipBuffer` | `0` | Buffers an early event skip input and preserves observed keyboard action order in its confirmation dialog when enabled. |
| `[EventSkipBuffer] OrderedKeyboard` | `1` | Extends EventSkipBuffer with ordered Up/Down/Enter capture. Set `0` for early-skip buffering alone. Has no effect unless `[Patches] EventSkipBuffer=1`. |
| `[EventSkipBuffer] UpKey` / `DownKey` / `ConfirmKey` | `38` / `40` / `13` | Decimal Windows virtual-key codes for Up arrow, Down arrow and Enter. Change only to match the corresponding actions remapped in the game; use distinct codes from 1 to 254. |
| `[Patches] SuppressTutorials` | `0` | Suppresses automatic Teach Me Histy tutorials when enabled. |
| `[Patches] Neptasm` | `0` | Enables the compatibility layer for neptasm's graphics controls; each control below remains off until explicitly enabled. |
| `[Neptasm] FPSUnlock` / `CameraUnlock` | `0` / `0` | Removes the stock frame-time cap and expands the camera angle range. |
| `[Neptasm] Resolution` / `ResolutionScale` | `0` / `1.0` | Scales supported 1920x1080 render targets by the selected multiplier. |
| `[Neptasm] FitWindow` | `0` | Uses the window client area for the Direct3D swap-chain size. |
| `[Neptasm] WindowControl` / `WindowWidth` / `WindowHeight` | `0` / `1920` / `1080` | Replaces the ten built-in window-size choices with one configured size. |
| `[Neptasm] Ultrawide` | `0` | Expands the game's 16:9 view to the selected output size; some menus and effects may still assume 16:9. |
| `[MipCache] Trace` / `CaptureSources` | `0` / `0` | Opt-in diagnostics; source capture is off by default. |

An explicit custom `DiskDirectory` or `LooseFiles.Directory` is preserved when Prepare Game updates an existing managed installation. Keep the base unpacked folder and its derived `.dlc` folder together when moving them. Do not edit extracted files: invalid files use the original loader, and `Verify=0` cannot detect same-size edits.

### LiveSplit New Game start

The package includes `vii-new-game.asl`. Add it as a Scriptable Auto Splitter
component in LiveSplit to start the timer when the title-screen New Game
transition is entered. It signature-scans the installed proxy's `dinput8.dll`
module once, then reads the proxy-published sequence counter and transition
state. The script does not write memory or patch the game. Keep
`[Patches] NewGameDetector=1` and restart the game after changing it.

To disable an optimization while retaining the installation, set its corresponding `[Patches]` value to `0` and restart. For example, set `MipCache=0`, `MotionCache=0`, or `LooseFiles=0`. The gameplay controls are independent and must be explicitly set to `1` before they alter play.

The single-mip improvement is active by default, including when updating an older
configuration that omits `SingleMipCache`. It does not require preparing the cache
again or deleting existing records. An explicit `SingleMipCache=1` is preserved
during updates; change it to `0` to use the improvement.

With `AutoSkipEvents=0` and `EventSkipBuffer=1`, ordered keyboard capture extends
the existing manual skip option. It samples while the event-skip dialog is active
and replays at most one action per dialog update. Selecting No, leaving the dialog,
changing scene or losing focus discards pending actions. Escape keeps its normal
behavior. Controller-only input uses the existing game polling path. Remapped keys
and mixed keyboard/controller use need separate testing; very short pulses can
still escape sampling. Ambiguous simultaneous samples, expired actions or a full
queue fall back to ordinary input for that dialog.

### Optional neptasm graphics controls

The neptasm compatibility layer is an optional graphics experiment, not part
of the prepared-cache profile. Leave `[Patches] Neptasm=0` for ordinary play.
Setting it to `1` makes the `[Neptasm]` controls available, but does not enable
`FPSUnlock`, `CameraUnlock`, `Resolution`, `FitWindow`, `WindowControl`, or
`Ultrawide` by itself. Enable and test one control at a time, then restart the
game after every edit.

For example, to use a custom window size, enable both the layer and the window
control:

```ini
[Patches]
Neptasm=1

[Neptasm]
WindowControl=1
WindowWidth=2560
WindowHeight=1440
```

`ResolutionScale` only applies when `Resolution=1`; `WindowWidth` and
`WindowHeight` only apply when `WindowControl=1`. `Ultrawide=1` expands the
game's 16:9 view, but some menus and effects can still
use the original layout assumptions. If the display is clipped, misaligned,
black, or otherwise incorrect, set `[Patches] Neptasm=0`, restart, and report
the exact display configuration rather than layering multiple graphics
options together. Suggested testing steps are in
[TESTING.md](TESTING.md).

## Diagnostics and feedback

Normal play does not create texture tracing or source captures. To help investigate a problem, set `[MipCache] Trace=1`, reproduce the issue once, then set it back to `0` and restart. `CaptureSources=1` saves unique input buffers and should remain off unless a maintainer explicitly asks for a local reproduction.

Do not send game executables, saves, PACs, extracted assets, cache records or
captured source buffers. Report the patch version, game SHA-256, Windows/GPU/driver
details, enabled settings and steps to reproduce the problem. Describe what you
expected and what happened, with relevant text from `vii-patches.log` or a trace
summary. See [TESTING.md](TESTING.md) for the reporting checklist.

## Updating, rollback, and uninstall

To update, close the game and extract the newer release to a separate folder.
Run its `VII-Prepare-Game.exe`, check the displayed settings and choose
**Install / Update**. Use **Prepare / Resume** if assets need preparing again.
Do not replace `vii-patches.ini` with a fresh template: Prepare Game keeps your
settings and backs up files before updating them. It accepts known older VII
patch DLLs and leaves unknown DLLs untouched.

Before each managed install or update, the tool creates a unique transaction folder under `vii-prepare-backups`. `snapshot.ini` records exact file targets and before/after hashes; `before-*` and `after-*` files retain the corresponding bytes. `committed.txt` identifies a completed transaction. State records the backup and cache paths; it does not authorize replacing a DLL or deleting a configuration file. Legacy timestamped backups retain their original format.

**Rollback last update** restores the prior DLL from the last verified
installation snapshot. It restores the previous settings only when the current
settings still match what that update installed. Edited or deleted settings are
retained as they are. Rolling back a fresh install removes its verified DLL and
keeps its settings. Rollback stops if the DLL has since been replaced, even by
another recognized VII build, or a required backup is missing or changed.

**Uninstall patch** removes a recognized VII DLL, including a manual installation
with missing/stale state. It keeps settings, prepared assets, caches, logs and
backups. Neither action changes the game executable. Unknown DLLs are untouched.
Both actions use the same interruption/recovery safeguards as installation.

Equivalent commands (close the game first):

```powershell
.\VII-Prepare-Game.exe --source 'C:\path\to\Megadimension Neptunia VII' --rollback
.\VII-Prepare-Game.exe --source 'C:\path\to\Megadimension Neptunia VII' --uninstall
```

Legacy backups remain available for manual review; the new rollback action does
not treat old unverified snapshots as verified transactions. If rollback cannot
verify an earlier snapshot, it stops rather than guessing which files to replace.

Cache, unpacked data, logs, and backups are not removed by rollback. Keep them for a future reinstall, or remove only the `vii-speedrun-patch` data folders after the plugin has been disabled or removed and you no longer need them. Original PAC files remain part of the game installation.
