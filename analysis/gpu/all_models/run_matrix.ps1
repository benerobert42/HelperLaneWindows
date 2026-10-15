param([ValidateSet('screen','confirm','special')][string]$Stage = 'screen')
$ErrorActionPreference = 'Stop'
$models = @('sphere','sphere_dense','jittered','ellipsoid','ellipsoid_small','blob','blob_small',
    'saddle_branch','clustered','planar','jittered_planar','folded','skew1','skew2','skew3','skew_fold','skew_saddle',
    'torus','torus_skew','cylinder','cylinder_skew','suzanne','bunny','teapot','ellipse2d',
    'obj:Data/Models/GraphExHeart.obj','obj:Data/Models/GraphExTangleCube.obj')
$methods = 'Source,SourceControl,CDT,CDTFlip,CDTFlip3D,Greedy,LMT,LMT3D,GP,Heap3D,HeapPlane,HeapScreen,HeapQuad,HeapQuadSet,HeapQuadView,HeapQuadLag,HeapQuadWarm,HeapQuadWarmLag,HeapLoose,HeapWide,Star3D,StarLoose,StarWide'
$samples=32; $warmup=64; $blocks=2
if ($Stage -eq 'confirm') {
    $methods='Source,SourceControl,CDT,Heap3D,Star3D'
    $samples=128; $warmup=256; $blocks=3
}
if ($Stage -eq 'special') {
    $models=@('torus','torus_skew','cylinder','cylinder_skew')
    $methods='Source,SourceControl,Heap3D,GridAligned,GridOpposite'
    $samples=128; $warmup=256; $blocks=3
}
$sampleRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location -LiteralPath $sampleRoot
New-Item -ItemType Directory -Force -Path analysis/gpu/all_models | Out-Null
foreach ($model in $models) {
    $slug=if ($model.StartsWith('obj:')) { [IO.Path]::GetFileNameWithoutExtension($model.Substring(4)) } else { $model }
    $prefix="${Stage}_${slug}_"
    $result="analysis/gpu/all_models/${prefix}lit_1080_q8.metadata.txt"
    if ((Test-Path -LiteralPath $result) -and (Select-String -LiteralPath $result -Pattern '^Status: Complete$' -Quiet)) {
        Write-Output "Already complete: $Stage $model"
        continue
    }
    $selected=if ($Stage -eq 'special' -and $model -eq 'torus') { "$methods,TopologySplit" } else { $methods }
    & "$PSScriptRoot/../orientation/run_sweep.ps1" -Models $model -Steps 8 -Samples $samples -Warmup $warmup -Blocks $blocks `
        -Methods $selected -Prefix $prefix -OutputDirectory 'analysis/gpu/all_models'
}
