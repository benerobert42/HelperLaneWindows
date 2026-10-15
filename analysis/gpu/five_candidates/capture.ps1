$ErrorActionPreference='Stop'
$sampleRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location -LiteralPath $sampleRoot
foreach ($name in @('SweptWing','ToothedWheel','FacetedArch','TrefoilKnot','SpiralRamp')) {
    & ../../../build/windows-vs2022-helperlanes/bin/Release/HelperLaneWindows.exe --perf --lit --views 1 `
        --geometries "obj:Data/Models/ResearchCandidates/$name.obj" --methods Source `
        --samples 4 --warmup 4 --blocks 1 --instances 1 --width 1920 --height 1080 --msaa 1 `
        --queue-depth 8 --ordering cache-overdraw --output "analysis/gpu/five_candidates/captures/$name.csv" `
        *> "analysis/build/capture-$name.log"
    if ($LASTEXITCODE -ne 0) { throw "Capture failed: $name" }
    Write-Output "Captured $name"
}
