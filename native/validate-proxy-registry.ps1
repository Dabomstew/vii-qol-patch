[CmdletBinding()]
param([string]$Proxy)
$ErrorActionPreference = 'Stop'
if (-not $Proxy) { $Proxy = "$PSScriptRoot\..\build\native-build\dinput8.dll" }
$hashes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($line in [IO.File]::ReadAllLines("$PSScriptRoot\..\build\generated\proxy-hashes.txt")) {
    if ($line.Length -eq 0 -or $line.StartsWith('#')) { continue }
    if ($line -cnotmatch '^([0-9a-f]{64}) \| (.*\S.*)$') { throw 'Invalid known-proxy registry entry' }
    if (-not $hashes.Add($Matches[1])) { throw 'Duplicate known-proxy SHA-256' }
}
if ($hashes.Count -eq 0) { throw 'Known-proxy registry is empty' }
$stream = [IO.File]::OpenRead($Proxy)
$sha = [Security.Cryptography.SHA256]::Create()
try { $proxyHash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
finally { $stream.Dispose(); $sha.Dispose() }
if (-not $hashes.Contains($proxyHash)) {
    throw "Built DLL is not registered: $proxyHash. Review this build and add its provenance to native/proxy-hashes.txt before building the preparer."
}
Write-Output "Verified bundled DLL against $($hashes.Count) known proxy hashes."
