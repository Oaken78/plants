# Builds the plant core to WebAssembly (web/build/plants.js + plants.wasm) and runs the core tests.
# Needs the Emscripten SDK; by default it is looked for in ~/emsdk.
param(
    [switch]$Debug,
    [switch]$SkipTests,
    [string]$Emsdk = "$env:USERPROFILE\emsdk"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command em++ -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path "$Emsdk\emsdk_env.ps1")) { throw "Emscripten not found. Install it into $Emsdk (see README.md)." }
    $env:EMSDK_QUIET = '1'
    $ErrorActionPreference = 'Continue'   # emsdk_env writes progress to stderr
    & "$Emsdk\emsdk_env.ps1" *> $null
    $ErrorActionPreference = 'Stop'
}
if (-not (Get-Command em++ -ErrorAction SilentlyContinue)) { throw "em++ is not on PATH after loading $Emsdk\emsdk_env.ps1" }
$ErrorActionPreference = 'Continue'       # em++ warnings go to stderr; failures are caught via $LASTEXITCODE

$sources = Get-ChildItem "$root\core\src\*.cpp" | ForEach-Object { $_.FullName }
$common = @('-std=c++17', "-I$root\core\include", '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-Wno-unused-parameter')
$opt = if ($Debug) { @('-O1', '-g', '-sASSERTIONS=2') } else { @('-O3', '-flto') }

$exports = @(
    'plants_create', 'plants_destroy', 'plants_param_count', 'plants_param_name', 'plants_param_default',
    'plants_param_min', 'plants_param_max', 'plants_set_param', 'plants_get_param', 'plants_reset_params',
    'plants_set_host', 'plants_grow_on_host', 'plants_make_bush', 'plants_make_grass', 'plants_lod_count',
    'plants_mesh_vertex_count', 'plants_mesh_index_count', 'plants_mesh_positions', 'plants_mesh_normals',
    'plants_mesh_shade', 'plants_mesh_aux', 'plants_mesh_indices', 'plants_mesh_bounds', 'plants_stat',
    'malloc', 'free'
) | ForEach-Object { "_$_" }

New-Item -ItemType Directory -Force "$root\web\build" | Out-Null
$wasmArgs = $common + $opt + $sources + @(
    '-o', "$root\web\build\plants.js",
    '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createPlantsModule', '-sENVIRONMENT=web,worker',
    '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=64MB', '-sFILESYSTEM=0',
    "-sEXPORTED_FUNCTIONS=$($exports -join ',')",
    '-sEXPORTED_RUNTIME_METHODS=HEAPF32,HEAPU8,HEAP8,HEAPU32,UTF8ToString'
)
Write-Host "Building web/build/plants.wasm ..."
& em++ @wasmArgs 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { throw "WebAssembly build failed" }

if (-not $SkipTests) {
    New-Item -ItemType Directory -Force "$root\core\tests\build" | Out-Null
    # .cjs: the package is an ES module package, the node test runner is CommonJS.
    $testArgs = $common + @('-O2') + $sources + @("$root\core\tests\tests.cpp", '-o', "$root\core\tests\build\tests.cjs",
        '-sENVIRONMENT=node', '-sALLOW_MEMORY_GROWTH=1')
    Write-Host "Building and running core tests ..."
    & em++ @testArgs 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "Test build failed" }
    & node "$root\core\tests\build\tests.cjs"
    if ($LASTEXITCODE -ne 0) { throw "Core tests failed" }
}
Get-ChildItem "$root\web\build" | Select-Object Name, Length | Format-Table -AutoSize
