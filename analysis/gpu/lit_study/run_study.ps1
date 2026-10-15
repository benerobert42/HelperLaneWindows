$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
$exePath = (Resolve-Path "$sampleRoot/../../../build/windows-vs2022-helperlanes/bin/Release/HelperLaneWindows.exe").Path
Set-Location -LiteralPath $sampleRoot
$models = 'ellipsoid,blob,torus_skew,cylinder_skew,suzanne,bunny,teapot'
$heaps = 'Source,SourceControl,CDT,CDTFlip,Heap3D,HeapPlane,HeapScreen,HeapQuad'
$runs = @(
    @{ Name = 'single'; Instances = 1; Methods = "$heaps,Greedy,LMT,GP"; Meshopt = $false },
    @{ Name = 'grid'; Instances = 64; Methods = $heaps; Meshopt = $false },
    @{ Name = 'grid_cache'; Instances = 64; Methods = $heaps; Meshopt = $true }
)
foreach ($studyRun in $runs) {
    $runArgs = @('--perf', '--lit', '--geometries', $models, '--methods', $studyRun.Methods,
        '--samples', '64', '--warmup', '64', '--views', '4', '--blocks', '3',
        '--instances', [string]$studyRun.Instances, '--width', '1920', '--height', '1080',
        '--msaa', '1,4', '--queue-depth', '8', '--output', "analysis/gpu/lit_study/$($studyRun.Name).csv")
    if ($studyRun.Meshopt) { $runArgs += '--meshopt' }
    Write-Output "Starting $($studyRun.Name)"
    & $exePath @runArgs *> "analysis/build/lit-$($studyRun.Name).log"
    if ($LASTEXITCODE -ne 0) { throw "$($studyRun.Name) failed: exit $LASTEXITCODE" }
    Write-Output "Completed $($studyRun.Name)"
}
