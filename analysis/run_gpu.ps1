param(
    [switch]$Quick,
    [uint32]$Instances = 1,
    [string]$Output = 'analysis/gpu/latest.csv',
    [string]$Geometries = 'sphere,sphere_dense,jittered,planar,jittered_planar,folded',
    [string]$Methods = 'Source,CDT,CDTFlip,Greedy,LMT,GP,Heap3D,HeapPlane',
    [uint32]$Samples = 40,
    [uint32]$Warmup = 20,
    [uint32]$Views = 8,
    [uint32]$Blocks = 3,
    [string]$MSAA = '1,4',
    [switch]$NoDepth,
    [switch]$Meshopt
)
$ErrorActionPreference = 'Stop'
$falcorRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$exe = Join-Path $falcorRoot 'build\windows-vs2022-helperlanes\bin\Release\HelperLaneWindows.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Build HelperLaneWindows and FalcorPython first: $exe" }
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $env:FALCOR_DEVMODE = '1'
    $benchmarkArgs = @('--perf', '--output', $Output, '--instances', $Instances,
        '--geometries', $Geometries, '--methods', $Methods, '--samples', $Samples,
        '--warmup', $Warmup, '--views', $Views, '--blocks', $Blocks, '--msaa', $MSAA)
    if ($Quick) { $benchmarkArgs += '--quick' }
    if ($NoDepth) { $benchmarkArgs += '--no-depth' }
    if ($Meshopt) { $benchmarkArgs += '--meshopt' }
    & $exe @benchmarkArgs
    if ($LASTEXITCODE -ne 0) { throw 'GPU benchmark failed.' }
    & (Join-Path $falcorRoot 'tools\.packman\python\python.exe') (Join-Path $PSScriptRoot 'summarize_gpu.py') $Output
    if ($LASTEXITCODE -ne 0) { throw 'GPU analysis failed.' }
} finally { Pop-Location }
