# Build and verify

Install Git, Python 3.10+ and Visual Studio C++ build tools with the x86/x64
toolchains and Windows SDK on Windows. You can build and run the synthetic tests
without game files or research tools. Run from the repository root:

```powershell
./scripts/build-release.ps1 -Tests
./scripts/package-release.ps1
```

Visual Studio is discovered through vswhere. Use
`-VisualStudioDirectory '<installation-directory>'` when discovery is unavailable.
Build intermediates are ignored under `build/`; verified packages are under
`dist/<project>-<version>/`, with matching ZIP and ZIP checksum beside that folder.
Existing release outputs are never overwritten.

`VERSION` sets the release version used in native constants, Windows resources,
package names and manifests. Use a patch increment for compatible fixes, a minor
increment for features or breaking changes before 1.0, and a major increment for
breaking changes after 1.0. ABI, asset-format and cache-schema versions are
tracked separately.

Package from a clean production checkout after building with `-Tests`. The
packager checks that binaries match the source, have the expected architecture
and contain the correct embedded resources. It also checks package contents,
links and checksums. The preparer embeds its common-controls v6 activation manifest. Both build and package checks run the standalone EXE with --help from a fresh folder without sidecar files. A missing manifest or startup failure blocks the release. The manifest identifies the production commit. Packages
exclude debug symbols, intermediate build records, test fixtures and game data.

Tests requiring an original game executable run locally with explicit baseline
paths. They copy fixtures and must never launch or modify the supplied baseline.
These tests and desktop checks run separately from the synthetic CI tests.

Never push development history into production. Public releases are assembled
from reviewed exports in an isolated publishing repository. A new build can have
a different hash with another toolchain; CI success does not establish bit-for-bit
identity with a locally reviewed release binary.

The preparer builds from `native/preparer-common/` and the VII game adapter.
`native/preparer-components.json` records the shared source version and hashes.
Compile `session_test.cpp` with `preparer.cpp` using C++17 and the static runtime
to test the shared workflow with fictional games and fake launchers. Backend
integration tests use local copies of the supported game executable; they must
never launch a game or modify an installed executable.
