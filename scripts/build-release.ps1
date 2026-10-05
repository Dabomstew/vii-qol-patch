[CmdletBinding()]
param([switch]$Tests, [string]$VisualStudioDirectory)
$ErrorActionPreference = 'Stop'
$arguments = @((Join-Path $PSScriptRoot 'release_build.py'), 'build')
if ($Tests) { $arguments += '--tests' }
if ($VisualStudioDirectory) { $arguments += @('--visual-studio-directory', $VisualStudioDirectory) }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
