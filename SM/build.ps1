# Builds THIS folder's main.cpp to a per-person temp exe (NOT in the synced Drive folder).
# Output: %TEMP%\nypc_bot_<A|B>.exe   (folder name decides A/B, so this script is identical in A/ and B/)
# Usage:  powershell -ExecutionPolicy Bypass -File A\build.ps1
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$who  = Split-Path -Leaf $here            # "A" or "B"
$src  = Join-Path $here "main.cpp"
$out  = Join-Path $env:TEMP "nypc_bot_$who.exe"

$gpp = Get-Command g++ -ErrorAction SilentlyContinue
$gppPath = if ($gpp) { $gpp.Source } else { $null }
if (-not $gppPath) {
    # Fall back to the WinGet package location (PATH may not be refreshed yet).
    $cand = Get-ChildItem -Path "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" `
        -Recurse -Filter g++.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cand) { $gppPath = $cand.FullName }
}
if (-not $gppPath) {
    Write-Error "g++ not found. Install a C++ compiler first (see shared/BUILD_NOTES.md)."
    exit 1
}

& $gppPath -std=c++17 -O2 -Wall -pipe -static -o $out $src
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed."; exit 1 }
Write-Host "Built $out"
