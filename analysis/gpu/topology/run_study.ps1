param(
    [string]$Models = 'ellipse2d,ellipsoid,blob,cylinder_skew,torus_skew,suzanne,teapot,obj:analysis/gpu/orientation/models/GraphExHeart.obj',
    [int]$Blocks = 3
)
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/../orientation/run_sweep.ps1" -Models $Models -Blocks $Blocks -Prefix 'cavity_' `
    -Methods 'Source,SourceControl,Heap3D,HeapLoose,HeapWide,Star3D,StarLoose,StarWide' `
    -OutputDirectory 'analysis/gpu/topology'
& "$PSScriptRoot/../orientation/run_sweep.ps1" -Models 'torus' -Blocks $Blocks -Prefix 'surgery_' `
    -Methods 'Source,SourceControl,Heap3D,GridAligned,GridOpposite,TopologySplit' -OutputDirectory 'analysis/gpu/topology'
& "$PSScriptRoot/../orientation/run_sweep.ps1" -Models 'torus_skew,cylinder,cylinder_skew' -Blocks $Blocks -Prefix 'grid_' `
    -Methods 'Source,SourceControl,Heap3D,GridAligned,GridOpposite' -OutputDirectory 'analysis/gpu/topology'
