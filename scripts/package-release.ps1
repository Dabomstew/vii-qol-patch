[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
& python (Join-Path $PSScriptRoot 'release_build.py') package
if ($LASTEXITCODE -ne 0) { throw 'Release packaging failed.' }
