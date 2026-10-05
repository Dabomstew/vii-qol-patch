# VII QoL Patch

Version **0.2.0**.

A local Windows patch for **Megadimension Neptunia VII**. Prepare Game creates
persistent texture caches and optional loose assets ahead of play. The patch
also provides opt-in event, tutorial, battle, timing and graphics controls.

Supports `NeptuniaVII.exe` SHA-256
`7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9`.
Original executables and PAC archives are preserved. Unsupported executables
leave the patches inactive.

## Installation and configuration

1. Download the release ZIP and verify its `.zip.sha256` checksum.
2. Extract it outside the game folder and close the game.
3. Run **VII-Prepare-Game.exe**, select the installation, and choose **Prepare / Resume**.
4. Wait for Ready, then launch normally through Steam.

Prepare Game refuses an unknown existing `dinput8.dll`; resolve mod conflicts
before installing. A recognized existing patch can be updated through the same
utility. Follow [Quick Start](docs/QUICK-START.md) and the
[User Guide](docs/USER-GUIDE.md) for configuration, update, recovery and removal.

The default profile enables MipCache, MotionCache, prepared LooseFiles and the
read-only New Game bridge. Gameplay-changing controls and neptasm graphics
controls remain off. The release includes the LiveSplit New Game ASL script.

The native x86 patch/preparer use a static C++ runtime. Gameplay requires the
game's normal Direct3D/DirectX components, but no Python or compiler. Preparation
can require roughly 35 GB of additional storage plus headroom for a full base/DLC
installation; actual use varies. Existing explicit cache/asset paths are preserved.

## Compatibility and limits

Compatibility is limited to the supported executable and bounded routes.
Unsupported texture recipes retain original loading. The historical extended-play
verification issue remains unreproduced; short successful checks do not prove it
resolved. Optional graphics controls have partial scene/UI coverage. See
[Troubleshooting and validation](docs/TESTING.md) and
[Load timing](docs/LOAD-TIMING.md) for coverage and reporting details.

## Support and source builds

Report problems through [GitHub Issues](https://github.com/Dabomstew/vii-qol-patch/issues), including
the patch version, game identity and a sanitized diagnostic summary. Keep game
files, saves, extracted assets and private paths out of reports. Installation
does not establish speedrun-category eligibility; check your category rules.

Developers: see [Build and verify](docs/BUILD.md). No game data is distributed.
The patch source is [MIT licensed](LICENSE); see [Third-party notices](THIRD-PARTY-NOTICES.md).

## AI use

AI coding tools assisted research, implementation, and documentation under human
direction. Compatibility and correctness are assessed through the documented
tests. Coverage remains limited; passing checks do not establish compatibility
with every installation or correctness throughout an entire game.
