param([ValidateSet('screen','confirm','control','large')][string]$Stage='screen')
$ErrorActionPreference='Stop'
$names=@('SweptWing','ToothedWheel','FacetedArch','TrefoilKnot','SpiralRamp')
$methods='Source,SourceControl,CDT,CDTFlip,Greedy,LMT,GP,Heap3D,HeapPlane,HeapQuadView,Star3D'
$samples=32; $warmup=64; $blocks=2
$instances=64
if ($Stage -eq 'confirm') {
    $methods='Source,SourceControl,CDT,CDTFlip,Heap3D,HeapPlane,HeapQuadView'
    $samples=128; $warmup=256; $blocks=3
}
if ($Stage -eq 'control') {
    $methods='Source,SourceControl,Heap3D,HeapPlane'
    $samples=128; $warmup=256; $blocks=3
}
if ($Stage -eq 'large') {
    $methods='Source,SourceControl,CDT,Heap3D,HeapPlane'
    $samples=128; $warmup=256; $blocks=3; $instances=1
}
foreach ($name in $names) {
    $suffix=if ($Stage -eq 'control') { '_control' } else { '' }
    $model="obj:Data/Models/ResearchCandidates/${name}${suffix}.obj"
    $prefix="${Stage}_${name}_"
    $meta="$PSScriptRoot/${prefix}lit_1080_q8.metadata.txt"
    if ((Test-Path -LiteralPath $meta) -and (Select-String -LiteralPath $meta -Pattern '^Status: Complete$' -Quiet)) {
        Write-Output "Already complete: $Stage $name"
        continue
    }
    & "$PSScriptRoot/../orientation/run_sweep.ps1" -Models $model -Steps 8 -Samples $samples -Warmup $warmup -Blocks $blocks `
        -Methods $methods -Instances $instances -Prefix $prefix -OutputDirectory 'analysis/gpu/five_candidates'
}
