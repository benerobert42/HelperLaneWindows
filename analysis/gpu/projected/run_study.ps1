param(
    [string]$Models = 'ellipsoid,cylinder_skew,torus_skew,suzanne',
    [string]$Prefix = 'projected_full_',
    [int]$Blocks = 3,
    [string]$Methods = 'Source,SourceControl,Heap3D,HeapQuadSet,HeapQuadView,HeapQuadWarm,HeapQuadWarmLag'
)
# Reuse the existing runner and 1080p protocol; no interactive adaptation.
& "$PSScriptRoot/../orientation/run_sweep.ps1" -Models $Models -Blocks $Blocks -Prefix $Prefix `
    -Methods $Methods `
    -OutputDirectory 'analysis/gpu/projected'
