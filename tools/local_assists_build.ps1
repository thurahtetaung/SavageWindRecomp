param(
    [Parameter(Mandatory=$true)][string]$ExeFSPath,
    [string]$PythonExe,
    [string]$MsysRoot = 'C:\msys64',
    [ValidateRange(1,64)][int]$Jobs = 16,
    [switch]$SkipGenerate
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $PythonExe) { $PythonExe = Join-Path $repo 'tools\venv\Scripts\python.exe' }
$PythonExe = (Resolve-Path -LiteralPath $PythonExe).Path
$ExeFSPath = (Resolve-Path -LiteralPath $ExeFSPath).Path
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
$previousJobs = $env:SW_LOCAL_BUILD_JOBS
Push-Location $repo
try {
    & $PythonExe 'tests/test_local_assists_patch.py'
    if ($LASTEXITCODE) { throw 'Patch-guard tests failed' }
    $env:SW_LOCAL_BUILD_JOBS = "$Jobs"
    & $bash -c 'export PATH=/ucrt64/bin:/usr/bin:$PATH; mkdir -p build; clang -std=c11 tests/local_assists_test.c runtime/local_assists.c -o build/local_assists_test.exe && build/local_assists_test.exe'
    if ($LASTEXITCODE) { throw 'Stamina logic tests failed' }
    if (-not $SkipGenerate) {
        & $PythonExe 'tools/local_assists_generate.py' $ExeFSPath
        if ($LASTEXITCODE) { throw 'Game code generation failed' }
    }
    & $bash -c 'export PATH=/ucrt64/bin:/usr/bin:$PATH; make -j "$SW_LOCAL_BUILD_JOBS" RELEASE=1'
    if ($LASTEXITCODE) { throw 'Build failed' }
    New-Item -ItemType Directory -Force 'local-data\extracted' | Out-Null
    Copy-Item -LiteralPath 'build\swgame.dll','build\swgame.dll.id' -Destination 'local-data\extracted'
    Copy-Item -LiteralPath 'third_party\fonts\FiraSans\FiraSans-Medium.ttf','third_party\fonts\FiraSans\FiraSans-SemiBold.ttf' -Destination 'build'
    Copy-Item -LiteralPath 'third_party\fonts\FiraSans\OFL.txt' -Destination 'build\FiraSans-OFL.txt'
    Copy-Item -LiteralPath 'docs\icon\icon_1024.png' -Destination 'build\icon.png'
    Write-Host 'Build and tests passed. Add your own game folders to local-data\extracted before launching.'
} finally {
    $env:SW_LOCAL_BUILD_JOBS = $previousJobs
    Pop-Location
}
