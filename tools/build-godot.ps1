# Builds the Godot extension (godot/bin/overgrowth.<platform>.<target>.<arch>.dll) from the plant core and
# godot/extension/src. Needs git, Python and a C++ compiler that SCons finds (Visual Studio on Windows). The first
# run clones godot-cpp into godot/extension/godot-cpp (git-ignored) and installs SCons with pip if it is missing.
#
#   ./tools/build-godot.ps1            # -Target template_debug for a debug build
param(
    [string]$Target = 'template_release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ext = "$root\godot\extension"
$godotCpp = 'godot-4.5-stable'   # the extension's API level; it loads in Godot 4.5 and later

$python = if (Get-Command py -ErrorAction SilentlyContinue) { 'py' } else { 'python' }
$ErrorActionPreference = 'Continue'   # git, pip and SCons write progress to stderr; failures are caught via $LASTEXITCODE
if (-not (Test-Path "$ext\godot-cpp\SConstruct")) {
    git clone --depth 1 --branch $godotCpp https://github.com/godotengine/godot-cpp "$ext\godot-cpp" 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw 'Cloning godot-cpp failed' }
}
& $python -m SCons --version *> $null
if ($LASTEXITCODE -ne 0) {
    & $python -m pip install --user scons 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw 'Installing SCons failed' }
}

Push-Location $ext
try {
    & $python -m SCons platform=windows target=$Target -j $env:NUMBER_OF_PROCESSORS 2>&1 |
        Where-Object { "$_" -notmatch '^Compiling ' } | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw 'Extension build failed' }
} finally {
    Pop-Location
}
Get-ChildItem "$root\godot\bin\*.dll" | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
