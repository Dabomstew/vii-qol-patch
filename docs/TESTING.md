# Testing and reporting problems

Start with the default settings and report anything that behaves differently
from the unmodified game. Performance can vary between PCs. An earlier
verification problem during a long play session has not been reproduced or
confirmed fixed; a successful short session does not settle it.

## Before testing

1. Confirm the game executable SHA-256 is `7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9`.
2. Check that no other `dinput8.dll` proxy is installed beside the game.
3. Run `VII-Prepare-Game.exe`, select the game folder, choose **Prepare / Resume**, and wait for **Preparation complete**.
4. Leave the default profile in place for the first run: MipCache, MotionCache, and LooseFiles on; Neptasm, all gameplay controls, `Trace`, and `CaptureSources` off.
5. Keep the original PACs and both `vii-speedrun-patch\unpacked` and `vii-speedrun-patch\unpacked.dlc` when DLC is present.

## Test routes

Use a normal save, a new game, or both. A useful first pass includes loading a save, moving through a map, opening menus, using several skills in battle, viewing an ADV event, and returning to menus after battle. If you own DLC, load a save or area that uses it after preparing with the DLC installed.

For a second pass, change only one optional control at a time, restart the game, and confirm its intended behavior:

- `AutoSkipEvents=1`: eligible ADV events skip automatically.
- `BattleAutoSkip=1`: battle animation/results skip behavior is enabled.
- `EventSkipBuffer=1`: remembers an early skip press until the prompt is ready, then processes Up/Down/Enter in the order received. With AutoSkipEvents off, test a quick Yes selection, a deliberate No, reopening the dialog, and the next scene. Selecting No must not cause a delayed skip or reopen the prompt.
- `SuppressTutorials=1`: automatic Teach Me Histy tutorials are suppressed; manual help remains available.

`[Neptasm]` graphics testing is optional and starts with `[Patches] Neptasm=1`.
That setting enables only the compatibility layer: every individual graphics
control remains off until set to `1`. Change one of the following at a time,
restart the game, and record the chosen display mode, desktop resolution, and
window size:

- `FPSUnlock=1`: removes the stock frame-time cap.
- `CameraUnlock=1`: expands the camera angle range.
- `Resolution=1` with a chosen `ResolutionScale`: scales supported 1920x1080 render targets.
- `FitWindow=1`: sizes the Direct3D swap chain to the window client area.
- `WindowControl=1` with `WindowWidth` and `WindowHeight`: replaces the built-in size choices with that configured size.
- `Ultrawide=1`: expands the game's 16:9 view to the selected output size.

Test menus, ADV scenes, battle, map movement, and a return from battle after
each graphics option. The game's UI and unusual render passes can retain their
original 16:9 limitations, especially with `Ultrawide`; report clipping,
misalignment, or a black/incorrect render area rather than combining more
options to work around it. Set `[Patches] Neptasm=0` after testing.

Optional integrity checks are separate from ordinary play: set `[LooseFiles] Verify=1` to hash every loose asset open, or `[MipCache] Verify=1` to compare cache-hit texture data. Both add substantial overhead and are intended for a short reproduction, not routine testing. Restore `Verify=0` afterward and restart.

## If something goes wrong

For a single-mip comparison, keep `[Patches] MipCache=1` and change only
`[MipCache] SingleMipCache` between `0` (default bypass) and `1` (previous cache
path), restarting between runs. Repeat the same scene and keep AutoSkipEvents
and other settings identical. Report both timings and any visual difference.
For manual input problems, compare `[EventSkipBuffer] OrderedKeyboard=1` and `0`
with EventSkipBuffer enabled, and include your keyboard mappings/controller use.

If you see a visual defect, crash, loading failure, cache-disable message, or unexpected input behavior, stop the affected test and record:

- Patch release/version and the ZIP checksum if available.
- The game executable SHA-256.
- Windows version, GPU, driver version, display mode, and whether DLC is installed.
- The exact save/scene/actions that lead to the result, plus expected versus actual behavior.
- The enabled `[Patches]` values and any changed paths. Remove personal path details if you prefer.
- Relevant text from `vii-patches.log`. With `Trace=1`, include a small text excerpt or a summary of the relevant trace, not a raw asset capture.

Send the report through [GitHub Issues](https://github.com/Dabomstew/vii-qol-patch/issues).
Do **not** attach game executables, saves, PAC archives, extracted assets,
cache records or captured source buffers. Share only the relevant text from
logs and remove personal paths first.

## Useful recovery checks

Close the game before changing files. Use **Prepare / Resume** to validate and reuse completed extraction/cache work after cancellation or an interrupted preparation. It repairs managed missing or corrupt records and automatically considers the sibling DLC directory when present.

If an unknown `dinput8.dll` is detected, do not replace it. Resolve the mod conflict first. Keep verified transaction snapshots in `vii-prepare-backups` for recovery as described in [USER-GUIDE.md](USER-GUIDE.md). The original PAC archives are retained throughout.

## Known limits

- Textures the cache cannot handle use the game's normal loader. Supported
  textures missing from the cache can be added during play.
- One known texture with incomplete mip data uses the game's normal loader.
- Checks cover selected scenes and file integrity. They do not establish
  correctness throughout a full playthrough, a clean game exit, or the same
  performance on another PC. Loader logs alone do not measure physical disk reads.
- `Verify=0` does not perform per-hit byte comparison, and `LooseFiles.Verify=0` does not detect same-size edits.
