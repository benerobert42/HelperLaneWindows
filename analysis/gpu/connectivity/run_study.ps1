param(
    [string]$Models = 'ellipsoid,torus_skew,cylinder_skew,suzanne',
    [string]$Prefix = '',
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
$exePath = (Resolve-Path "$sampleRoot/../../../build/windows-vs2022-helperlanes/bin/Release/HelperLaneWindows.exe").Path
Set-Location -LiteralPath $sampleRoot
$runs = @(
    @{ Name='lit_native_1080'; Ordering='native'; Height=1080; Lit=$true },
    @{ Name='lit_cache_1080'; Ordering='cache'; Height=1080; Lit=$true },
    @{ Name='lit_cache_overdraw_1080'; Ordering='cache-overdraw'; Height=1080; Lit=$true },
    @{ Name='lit_cache_overdraw_360'; Ordering='cache-overdraw'; Height=360; Lit=$true },
    @{ Name='lit_cache_overdraw_2160'; Ordering='cache-overdraw'; Height=2160; Lit=$true },
    @{ Name='texture_implicit'; Ordering='cache-overdraw'; Height=1080; Lit=$false },
    @{ Name='texture_explicit'; Ordering='cache-overdraw'; Height=1080; Lit=$false; Explicit=$true },
    @{ Name='texture_no_depth'; Ordering='cache-overdraw'; Height=1080; Lit=$false; NoDepth=$true }
)
foreach ($studyRun in $runs) {
    if ($Only.Count -and $studyRun.Name -notin $Only) { continue }
    $runName = "$Prefix$($studyRun.Name)"
    $runArgs = @('--perf', '--raster-metrics', '--geometries', $Models,
        '--methods', 'Source,SourceControl,CDTFlip,Heap3D,HeapQuad',
        '--samples', '64', '--warmup', '64', '--views', '4', '--blocks', '3', '--instances', '64',
        '--width', [string]($studyRun.Height * 16 / 9), '--height', [string]$studyRun.Height,
        '--msaa', '1,4', '--queue-depth', '8', '--ordering', $studyRun.Ordering,
        '--output', "analysis/gpu/connectivity/$runName.csv")
    if ($studyRun.Lit) { $runArgs += '--lit' }
    if ($studyRun.Explicit) { $runArgs += '--explicit-lod' }
    if ($studyRun.NoDepth) { $runArgs += '--no-depth' }
    Write-Output "Starting $runName"
    & $exePath @runArgs *> "analysis/build/connectivity-$runName.log"
    if ($LASTEXITCODE -ne 0) { throw "$runName failed: exit $LASTEXITCODE" }
    Write-Output "Completed $runName"
}
