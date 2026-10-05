# Build and verify

Use Windows, Git, Python 3.10+ and Visual Studio C++ build tools with the x86/x64
toolchains and Windows SDK. No research repository, game data, Frida, or Ghidra
is required to build or run the synthetic tests. Run from the repository root:

```powershell
./scripts/build-release.ps1 -Tests
./scripts/package-release.ps1
```

Visual Studio is discovered through vswhere. Use
`-VisualStudioDirectory '<installation-directory>'` when discovery is unavailable.
Build intermediates are ignored under `build/`; verified packages are under
`dist/<project>-<version>/`, with matching ZIP and ZIP checksum beside that folder.
Existing release outputs are never overwritten.

`VERSION` is the release version authority. Generated native constants, Windows
version resources, package names and manifests use it. Projects advance
independently: patch for compatible fixes, minor for features or pre-1.0 breaking
changes, major for breaking changes after 1.0. Earlier deliveries are prerelease
history. ABI, asset-format and cache-schema versions are independent contracts.

Package only a clean production commit after building with `-Tests`. Packaging
checks all tracked source hashes, artifact identities, PE architectures, embedded
resources where applicable, archive membership, bytes, links and checksums.
Only the current committed production source revision appears in the manifest.
PDBs, build identities, synthetic fixtures and game data are not distributed.

Tests requiring an original game executable run locally with explicit baseline
paths. They copy fixtures and must never launch or modify the supplied baseline.
Game-backed and desktop checks are separate from the synthetic CI suite.

Never push development history into production. Public releases are assembled
from reviewed exports in an isolated publishing repository. A new build can have
a different hash with another toolchain; CI success does not establish bit-for-bit
identity with a locally reviewed release binary.
