# Experimental load timing and FPS control

This feature covers **battle-entry character resource waits, initial dungeon
map-resource waits before player control, ADV script preparation waits,
normal world-map resource waits on save loading and battle return, and required
synchronous dimension-title movie setup, and load-list metadata calls**. It does not yet provide whole-game
time without loads. Ordinary dungeon enemy
spawning, ADV playback, battle cameras and other unqualified
phases are not selected by this detector.

Enable observation with `[Patches] LoadTiming=1`. Also set
`[LoadTiming] FPSUnlock=1` to temporarily remove the frame limiter during the
same detected waits. Both settings default to zero and require a restart.
Keep `[Neptasm] FPSUnlock=0` to retain capped gameplay. If the global Neptasm
unlock is explicitly enabled, it keeps its global effect; this feature still
measures loads but does not attempt to override or undo that choice.

Asynchronous intervals require an original resource predicate to actually wait.
The detector excludes neither the unconditional scheduling step into the wait
state nor the subsequent camera sequence. Timing uses QPC wall time and sums
overlapping intervals once. A zero pacing interval is applied only inside the
original clock call and is restored on normal and exceptional exits. No
interpolation numerator, audio behavior or resource-readiness result is patched.

The dungeon adapter requires the original map setup predicate to report busy,
the initial map phase to remain active, and player/UI initialization to remain
incomplete. Later event waits and playable background loading remain counted.
The ADV adapter requires the original script job to remain busy in phase 3,
with the same task, scene and setup object and no cancellation. It closes at
every callback entry and retains callback execution time. Later initialization
fades, dialogue/skip input and MP resource work remain counted.
The world adapter requires the normal controller's initial wait phase, its
original readiness result, and an observed map/model or character resource-busy
result. Null objects and other world callbacks remain counted. It closes at
every task callback entry and retains callback execution time. Character setup
alone cannot trigger removal without the nested resource-busy result.
The title adapter brackets the original synchronous movie setup only when the
live title task in phase 1 depends on that exact uninitialized video. It checks
the task/window/movie identities and closes on normal or exceptional return.
First-frame delivery, movie playback, UI finalization and title timers remain
counted. This setup bracket ends before the frame clock, so it removes setup
time from the cumulative timer without accelerating playback.
The save-list adapter brackets the original synchronous existence/open/read/close
calls only during mode-0 list creation, before the selection list exists.
Scan formatting/logging, UI creation, menu navigation, save-write mode and
unrelated file access remain counted. Each bracket closes on exceptional as
well as normal return; it never extends to the frame limiter.
All reasons use the same cumulative counter.

`[LoadTiming] Trace=1` logs asynchronous/title interval boundaries for local diagnostics.
The short metadata calls do not produce per-call log output. Leave it
off for normal use. Missing owner updates, a memory-read failure, clock failure
or an active frame gap over 500 ms invalidates this experimental observer and
disables further load removal and temporary unlock for the process. Unknown
gaps must not silently become removed time. Focus regain requires fresh
resource-wait evidence. The 500 ms guard is a conservative qualification limit,
not a statement that a longer real load is impossible.

## Read-only process memory ABI

Find the unique `VIILT001` byte marker in `dinput8.dll` once per process.
The descriptor is 24 bytes:

| Offset | Type | Meaning |
| --- | --- | --- |
| `0x00` | 8 bytes | `VIILT001` |
| `0x08` | uint32 | ABI version, currently 1 |
| `0x0C` | uint32 | Descriptor size, 24 |
| `0x10` | uint32 pointer | Process-lifetime state root |
| `0x14` | uint32 | State size, 112 |

Follow the pointer at `marker+0x10`, then use these state offsets. This is a
stable process-local tree; no game heap pointer needs to be retained externally.

| State offset | Type | Meaning |
| --- | --- | --- |
| `0x00` | uint32 | Publication sequence |
| `0x08` | uint32 | Status: 0 disabled, 1 ready with partial coverage, 2 fault |
| `0x0C` | uint32 | Coverage mask: bit 0 = battle-entry character wait; bit 1 = initial dungeon map wait; bit 2 = ADV script wait; bit 3 = normal world-map resource wait; bit 4 = synchronous title movie setup; bit 5 = load-list metadata call |
| `0x10` | uint32 | Active reason mask; nonzero means a covered blocking load |
| `0x14` | uint32 | Active owner count |
| `0x18` | uint32 | Owner generation counter |
| `0x1C` | uint32 | Load-boundary sequence |
| `0x20` | uint32 | Foreground observation |
| `0x24` | uint32 | Fault: 0 none, 1 clock, 2 capacity, 3 missing owner, 4 long gap, 5 memory, 6 install |
| `0x28` | uint32 | Loading-only unlock requested |
| `0x2C` | uint32 | Global Neptasm unlock configured |
| `0x30` | uint32 | Temporary zero-interval applications |
| `0x38` | uint64 | QPC frequency |
| `0x40` | uint64 | Completed excluded ticks |
| `0x48` | uint64 | Open interval's start QPC, zero if none |
| `0x50` | uint64 | Last observer/frame QPC |
| `0x58` | uint32 | New Game sequence, zero before notification |
| `0x60` | uint64 | Last New Game QPC |
| `0x68` | uint64 | Excluded ticks at that New Game boundary |

All fields use little endian. Reserved fields are not an API. Read sequence,
then the complete payload, then sequence again. Accept only matching even
sequences, a supported descriptor, status 1 and the intended coverage mask.
Bound retries; handle read failures and stale samples explicitly. Independent
field watchers alone do not guarantee a coherent 64-bit snapshot in this x86
process. A reader must reacquire the descriptor after process restart.

For a coherent current QPC `q` in the same clock domain:

```text
excluded(q) = completedTicks + (reason != 0 ? q - openSinceQpc : 0)
```

The counter preserves completed loads shorter than one reader poll. Clip to a
fresh run anchor and account for manual timer pauses. A Boolean `isLoading`
reader is possible but quantizes boundaries and can miss short intervals.
Do not add Boolean pause subtraction on top of cumulative subtraction. A fault
invalidates the current sample; never keep a previous `true` load flag latched.
The original New Game bridge and its offsets remain unchanged.

## Validation commands

Run `native\test-load-timing.cmd`, then build the proxy. Set `VII_GAME_EXE` to
the original supported executable and run
`python native/tests/load_timing_static_test.py`. This verifies the baseline,
hook boundaries and unique descriptor without launching the game.
Use the normal Prepare Game installation regression test for opt-in defaults
and preservation of explicitly enabled options. Runtime qualification is
separate from these synthetic/static checks.
