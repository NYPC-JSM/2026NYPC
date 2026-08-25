# Builds (unless -NoBuild) then runs one match: this folder's bot (LEFT) vs an opponent (RIGHT).
# Uses ../shared for tooling & opponents; builds/logs to %TEMP% (nothing written into the synced folder).
# Examples:
#   powershell -ExecutionPolicy Bypass -File A\run-match.ps1
#   powershell -ExecutionPolicy Bypass -File A\run-match.ps1 -Seed 42 -Opponent rusher
param(
    [int]$Seed = 42,
    [ValidateSet("sample","self","rusher","turtle","masser","masser_fast","garrison","fortress","forttough","idler")]
    [string]$Opponent = "sample",
    [switch]$NoBuild
)
$ErrorActionPreference = "Stop"
$here   = Split-Path -Parent $MyInvocation.MyCommand.Path
$who    = Split-Path -Leaf $here               # "A" or "B"
$root   = Split-Path -Parent $here             # NYPC/
$shared = Join-Path $root "shared"

if (-not $NoBuild) { & (Join-Path $here "build.ps1") }

# Mirror build.ps1: on a non-ASCII %TEMP% (non-ASCII Windows username) the bot exe and
# logs must live on an ASCII path, else the MinGW-built exe / tooling paths break. No-op
# on an ASCII-username machine (keeps using %TEMP%).
$outDir = $env:TEMP
if ($env:TEMP -match '[^\x00-\x7F]') {
    $outDir = "C:\Temp\nypcbuild"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $env:TMP = $outDir; $env:TEMP = $outDir
}
$bot    = Join-Path $outDir "nypc_bot_$who.exe"
$tool   = Join-Path $shared "nation-providing\testing-tool.py"
$sample = Join-Path $shared "nation-providing\sample-code.py"
$log    = Join-Path $outDir "nypc_log_$who.txt"

switch ($Opponent) {
    "self"   { $exec2 = "`"$bot`"" }
    "sample" { $exec2 = "python `"$sample`"" }
    default  { $exec2 = "python `"" + (Join-Path $shared "opponents\$Opponent.py") + "`"" }
}

Write-Host "Seed=$Seed  LEFT=$who/bot  RIGHT=$Opponent"
python "$tool" --seed $Seed -l "$log" -a "`"$bot`"" -b "$exec2"
Write-Host "Log written to $log  (paste into the web simulator to replay)"
