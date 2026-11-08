param([string]$DataRoot, [string]$MsysRoot = 'C:\msys64')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $DataRoot) { $DataRoot = Join-Path $repo 'local-data' }
$DataRoot = (Resolve-Path -LiteralPath $DataRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $DataRoot 'extracted\swgame.dll'))) {
    throw 'Build the local game library and prepare a separate experimental data folder first.'
}
$env:PATH = (Join-Path $MsysRoot 'ucrt64\bin') + ';' + $env:PATH
$env:SAVAGEWIND_DATA_ROOT = $DataRoot
Remove-Item Env:\SAVAGEWIND_CONTROL -ErrorAction SilentlyContinue
Remove-Item Env:\SAVAGEWIND_SAVE_DIR -ErrorAction SilentlyContinue
Start-Process -FilePath (Join-Path $repo 'build\savagewind.exe') -WorkingDirectory $DataRoot
