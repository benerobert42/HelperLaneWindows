# Subdivided-mesh study: Source vs. strict re-triangulations on Blender subdivision levels,
# swept over MSAA, footprint (instance grid / camera distance) and timed-shader quad work.
# One run per mesh x instances x distance x quad-work mode so each is resumable;
# completed runs (metadata "Status: Complete") are skipped.
# Display-native 1920x1200 (the CLI benchmark refuses non-native sizes).
# Instances form a centered grid that the camera fits: more instances = more triangles AND smaller copies.
# Quad work: none = original lit shader (no derivatives); ddx = flat normals from ddx/ddy(worldPos);
# tex2k = implicit-LOD sample of a random mip-mapped 2048^2 texture. ddx/tex2k keep helper lanes executing.
# Dragon sources contain exact zero-area triangles; they run with --preserve-source-defects, which leaves those
# triangles and their neighborhoods untouched. Defect-free meshes run without it.
param(
    [string]$Exe = '../../../build/windows-vs2022/bin/Release/HelperLaneWindows.exe',
    [string]$Directory = 'C:/Users/oldbr/Documents',
    [string[]]$Names = @('Suzanne_s1','bunny_s1','dragon_s1'),
    [string]$Methods = 'Source,SourceControl,CDT,CDTFlip,Heap3D,HeapPlane,HeapQuadSet',
    [string[]]$Distances = @('1'),
    [int[]]$Instances = @(1),
    [string[]]$QuadWork = @('none','ddx','tex2k'),
    [string]$Msaa = '1,2,4,8',
    [int]$Steps = 8, [int]$Samples = 32, [int]$Warmup = 64, [int]$Blocks = 2,
    [int]$Width = 1920, [int]$Height = 1200, [string]$Stage = 'screen'
)
$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location -LiteralPath $sampleRoot
$exePath = (Resolve-Path $Exe).Path
$out = 'analysis/gpu/subdivided'
New-Item -ItemType Directory -Force -Path "$out/logs" | Out-Null
foreach ($name in $Names) {
  foreach ($count in $Instances) {
    foreach ($distance in $Distances) {
      foreach ($mode in $QuadWork) {
        $run = "${Stage}_${name}_d${distance}_i${count}_${mode}_lit_${Height}_q8"
        $meta = "$out/$run.metadata.txt"
        if ((Test-Path -LiteralPath $meta) -and (Select-String -LiteralPath $meta -Pattern '^Status: Complete$' -Quiet)) {
            Write-Output "Already complete: $run"; continue
        }
        foreach ($ext in 'csv','metadata.txt','raster.csv','raw.csv','png') { Remove-Item -LiteralPath "$out/$run.$ext" -ErrorAction SilentlyContinue }
        $runArgs = @('--perf', '--lit', '--lit-quad-work', $mode, '--raster-metrics', '--rotation-axes', [string]$Steps,
            '--geometries', "obj:$Directory/$name.obj", '--methods', $Methods,
            '--samples', [string]$Samples, '--warmup', [string]$Warmup, '--blocks', [string]$Blocks, '--instances', [string]$count,
            '--width', [string]$Width, '--height', [string]$Height, '--msaa', $Msaa,
            '--camera-distance-scale', $distance,
            '--queue-depth', '8', '--ordering', 'cache-overdraw', '--output', "$out/$run.csv")
        if ($name -like 'dragon*') { $runArgs += '--preserve-source-defects' }
        $start = Get-Date
        Write-Output "Starting $run at $start"
        & $exePath @runArgs *> "$out/logs/$run.log"
        $code = $LASTEXITCODE
        Write-Output ("Finished $run exit=$code after {0:N1} min" -f ((Get-Date) - $start).TotalMinutes)
        if ($code -ne 0) { Write-Output "FAILED $run (see $out/logs/$run.log)" }
      }
    }
  }
}
Write-Output 'ALL RUNS DONE'
