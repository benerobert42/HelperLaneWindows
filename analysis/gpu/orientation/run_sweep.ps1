param(
    [string]$Models = 'ellipse2d,ellipsoid,blob,sphere_dense,jittered,cylinder,cylinder_skew,torus,torus_skew,saddle_branch,clustered,planar,folded,skew2,skew_fold,suzanne,bunny,teapot,obj:Data/Models/GraphExHeart.obj,obj:Data/Models/GraphExTangleCube.obj',
    [int]$Steps = 8,
    [int[]]$Heights = @(1080),
    [int]$Samples = 128,
    [int]$Warmup = 256,
    [int]$Blocks = 3,
    [int]$Instances = 64,
    [int]$QueueDepth = 8,
    [string]$Methods = 'Source,SourceControl,CDTFlip,Heap3D',
    [string]$Prefix = 'study_',
    [string]$OutputDirectory = 'analysis/gpu/orientation'
)
$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location -LiteralPath $sampleRoot
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path analysis/build | Out-Null
$exe = (Resolve-Path '../../../build/windows-vs2022-helperlanes/bin/Release/HelperLaneWindows.exe').Path
foreach ($height in $Heights) {
    $name = "${Prefix}lit_${height}_q${QueueDepth}"
    $runArgs = @('--perf', '--lit', '--raster-metrics', '--rotation-axes', [string]$Steps,
        '--geometries', $Models, '--methods', $Methods, '--samples', [string]$Samples,
        '--warmup', [string]$Warmup, '--blocks', [string]$Blocks, '--instances', [string]$Instances,
        '--width', [string]($height * 16 / 9), '--height', [string]$height, '--msaa', '1,4',
        '--queue-depth', [string]$QueueDepth, '--ordering', 'cache-overdraw',
        '--output', "$OutputDirectory/$name.csv")
    Write-Output "Starting $name : $Steps rotations per axis, models $Models"
    & $exe @runArgs *> "analysis/build/orientation-$name.log"
    if ($LASTEXITCODE -ne 0) { throw "$name failed with exit $LASTEXITCODE" }
    Write-Output "Completed $name"
}
