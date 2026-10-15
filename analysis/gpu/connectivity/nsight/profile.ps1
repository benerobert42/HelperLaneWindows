param([string]$Model = 'torus_skew', [int]$Height = 1080, [int]$MSAA = 4, [switch]$HardwareCounters)
$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path "$PSScriptRoot/../../../..").Path
Set-Location -LiteralPath $sampleRoot
$nsys = 'C:/Program Files/NVIDIA Corporation/Nsight Systems 2026.5.1/target-windows-x64/nsys.exe'
$exe = (Resolve-Path '../../../build/windows-vs2022-helperlanes/bin/Release/HelperLaneWindows.exe').Path
$name = "${Model}_${Height}_${MSAA}x"
$counterArgs = @()
if ($HardwareCounters) {
    $name += '_metrics'
    $counterArgs = @('--gpu-metrics-devices=0', '--gpu-metrics-set=ad10x-gfxt', '--gpu-metrics-frequency=10000')
}
& $nsys profile @counterArgs --trace=dx12,dx12-annotations --dx12-gpu-workload=individual --dx12-wait-calls=true --sample=none --cpuctxsw=none --discard-environment=true "--output=analysis/gpu/connectivity/nsight/$name" --stats=true $exe --perf --lit --geometries $Model --methods Source,Heap3D --samples 128 --warmup 64 --views 1 --blocks 1 --instances 64 --width ($Height * 16 / 9) --height $Height --msaa $MSAA --queue-depth 8 --ordering cache-overdraw --output "analysis/gpu/connectivity/nsight/$name.csv" *> "analysis/build/nsight-$name.log"
exit $LASTEXITCODE
