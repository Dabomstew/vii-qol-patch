# VII QoL Patch

Version **0.2.2**.

A Windows patch for **Megadimension Neptunia VII**. Prepare Game builds a
texture cache and unpacks game assets before you play. The patch reuses that
data and offers optional story skipping, tutorial skipping, battle skipping,
FPS unlock during loads, dungeon preview hiding and graphics settings.

Supports `NeptuniaVII.exe` SHA-256
`7ff2aad55e965add3b6fc45bd0ad2158769de014d6f619dc564a50c3e0fa42d9`.
The original game executable and PAC archives stay unchanged. The patch only
activates for the supported executable.

## Installation and configuration

1. Download the release ZIP and verify its `.zip.sha256` checksum.
2. Extract it outside the game folder and close the game.
3. Run **VII-Prepare-Game.exe**, select the installation, and choose **Prepare / Resume**.
4. Wait for **Preparation complete**, then start the game through Steam.

**Install / Update** installs the patch and saves settings without preparing
assets. **Play** does the same before starting the game. Supported textures
missing from the cache can be added during play; use **Prepare / Resume** to
prepare assets ahead of time.

Prepare Game refuses an unknown existing `dinput8.dll`; resolve mod conflicts
before installing. A recognized existing patch can be updated through the same
utility. Follow [Quick Start](docs/QUICK-START.md) and the
[User Guide](docs/USER-GUIDE.md) for configuration, update, recovery and removal.

Texture caching, motion caching and unpacked assets are enabled by default.
The included LiveSplit script can start your timer when you select New Game.
Load timing is active for supported waits. FPS unlock during loads, dungeon preview hiding, story, tutorial and battle skipping, early skip input and neptasm graphics settings are off until you enable them.

Use a Windows PC that already runs the supported game, including its usual
DirectX components. Allow roughly 35 GB of extra free space for preparation,
with room for the cache to grow. Space use varies with installed content and
hardware. You can choose another drive for prepared data; updates keep your
saved folder choices.

## Compatibility and limits

Testing covers selected scenes on the supported executable, rather than a full
playthrough on every setup. Textures the cache cannot handle use the game's
normal loader. An earlier verification problem during a long play session has
not been reproduced or confirmed fixed. Optional graphics settings have only
been checked in some scenes and menus. See
[Troubleshooting and validation](docs/TESTING.md) and
[Load timing](docs/LOAD-TIMING.md) for coverage and reporting details.

## Support and source builds

Report problems through [GitHub Issues](https://github.com/Dabomstew/vii-qol-patch/issues), including
the patch version, game executable hash and a short description of the problem. Keep game
files, saves, extracted assets and private paths out of reports. Installation
does not establish speedrun-category eligibility; check your category rules.

Developers: see [Build and verify](docs/BUILD.md). No game data is distributed.
The patch source is [MIT licensed](LICENSE); see [Third-party notices](THIRD-PARTY-NOTICES.md).

## AI use

AI tools helped with the code and documentation. The testing guide describes what has been checked and the known limits.
