[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if ($env:PATCH_VISUAL_STUDIO) {
    $installation = $env:PATCH_VISUAL_STUDIO
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Install the Visual Studio C++ build tools or supply -VisualStudioDirectory.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'No Visual Studio C++ toolchain found.' }
}
if (-not (Test-Path -LiteralPath (Join-Path $installation 'VC/Auxiliary/Build/vcvarsall.bat') -PathType Leaf)) { throw 'Invalid Visual Studio directory.' }
Write-Output $installation
