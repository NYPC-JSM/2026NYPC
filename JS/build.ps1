# Builds THIS folder's main.cpp to a per-person temp exe (NOT in the synced Drive folder).
# Output: %TEMP%\nypc_bot_<A|B>.exe   (folder name decides A/B, so this script is identical in A/ and B/)
# Usage:  powershell -ExecutionPolicy Bypass -File A\build.ps1
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$who  = Split-Path -Leaf $here            # "A" or "B"
$src  = Join-Path $here "main.cpp"
# ($out is computed below, after the ASCII-temp redirect.)

# g++ discovery. Prefer an ASCII-path toolchain (C:\mingw64) FIRST: MinGW's assembler
# (as) and linker (ld) cannot resolve a path containing non-ASCII bytes — on a machine
# whose Windows username is non-ASCII (e.g. Korean "연준서") the WinGet install lands
# under C:\Users\<한글>\... and every link fails ("cannot find crt2.o / -lstdc++"). We
# keep a plain ASCII COPY of the mingw64 tree at C:\mingw64 for that case. Then PATH,
# then the WinGet package location (works on ASCII-username machines, e.g. SM's).
$gppPath = $null
if (Test-Path "C:\mingw64\bin\g++.exe") { $gppPath = "C:\mingw64\bin\g++.exe" }
if (-not $gppPath) {
    $gpp = Get-Command g++ -ErrorAction SilentlyContinue
    if ($gpp) { $gppPath = $gpp.Source }
}
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

# Temp + output dir. gcc writes intermediate .o files under %TMP%/%TEMP%, and MinGW
# tools choke on a non-ASCII temp path just like above. The default %TEMP% is under the
# user profile (non-ASCII on this machine), so redirect TMP/TEMP + the output exe to an
# ASCII build dir when %TEMP% isn't pure ASCII. On an ASCII-username machine this is a
# no-op (keeps writing to %TEMP%, unchanged behavior). run-match.ps1 mirrors this logic.
$outDir = $env:TEMP
if ($env:TEMP -match '[^\x00-\x7F]') {
    $outDir = "C:\Temp\nypcbuild"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $env:TMP = $outDir; $env:TEMP = $outDir
}
$out = Join-Path $outDir "nypc_bot_$who.exe"

# NOTE: dropped -pipe (vs SM's build.ps1): with -pipe the cc1plus->as handoff goes over a
# pipe that failed ("error writing to -") in this sandbox; a temp .o in the ASCII $outDir
# is robust. Perf delta on one ~2100-line file is negligible.
& $gppPath -std=c++17 -O2 -Wall -static -o $out $src
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed."; exit 1 }
Write-Host "Built $out"
