param(
    [double]$MaxMinutes = 40,
    [switch]$PlanarPaper,
    [switch]$PreserveSourceDefects,
    [string]$Methods = 'Source,SourceControl,CDT,CDTFlip,CDTFlip3D,Heap3D,HeapPlane',
    [string[]]$SvgFiles = @(),
    [double]$BezierDeviation = 1.0,
    [string]$OutputDirectory = "$PSScriptRoot/axis12_n100_w30",
    [string[]]$Models = @(
        'obj:Data/Models/ResearchCandidates/SweptWing.obj',
        'obj:Data/Models/ResearchCandidates/FacetedArch.obj',
        'torus_skew',
        'obj:Data/Models/ResearchCandidates/SpiralRamp.obj',
        'obj:Data/Models/ResearchCandidates/TrefoilKnot.obj',
        'suzanne',
        'obj:Data/Models/ResearchCandidates/ToothedWheel.obj',
        'cylinder_skew'
    )
)
$ErrorActionPreference = 'Stop'
if ($MaxMinutes -le 0 -or $MaxMinutes -gt 40) { throw 'MaxMinutes must be positive and at most 40.' }
$sampleRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
Set-Location -LiteralPath $sampleRoot
$exe = (Resolve-Path '../../../build/windows-vs2022-helperlanes/bin/Release/HelperLaneWindows.exe').Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$stem = "$outputRoot/study"
$meta = "$stem.metadata.txt"
if (Test-Path -LiteralPath "$stem.csv") { throw "Results already exist at $stem; preserve them before selecting a fresh output." }
if ($PlanarPaper) {
    $Models = @($SvgFiles | ForEach-Object { 'svg:' + (Resolve-Path -LiteralPath $_).Path }) + @('circle', 'ellipse')
    $argsForRun = @('--perf', '--paper-planar', '--raster-metrics',
        '--geometries', ($Models -join ','), '--bezier-deviation', $BezierDeviation.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--samples', '100', '--warmup', '30', '--blocks', '1',
        '--width', '1920', '--height', '1080', '--msaa', '1,4', '--queue-depth', '8',
        '--ordering', 'cache-overdraw', '--output', "`"$stem.csv`"")
    Write-Output "Starting $($Models.Count) planar inputs in one window; static 1x1 and 100x100 grids; 30 warmups + 100 samples."
} else {
    $argsForRun = @('--perf','--lit','--raster-metrics','--rotation-angles','45,135,225,315',
        '--geometries',($Models -join ','),'--methods',$methods,'--samples','100','--warmup','30','--blocks','1',
        '--instances','64','--width','1920','--height','1080','--msaa','1,4','--queue-depth','8',
        '--ordering','cache-overdraw','--output',"`"$stem.csv`"")
    Write-Output "Starting $($Models.Count) models in one window; 12 orientations; 30 warmups + 100 timed frames per case."
}
if ($PreserveSourceDefects) { $argsForRun += '--preserve-source-defects' }
# Temporary, per-thread power request; no persistent power-plan changes.
if (-not ('HelperLaneBenchmarkPower' -as [type])) {
    Add-Type -TypeDefinition @"
using System.Runtime.InteropServices;
public static class HelperLaneBenchmarkPower {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint SetThreadExecutionState(uint flags);
}
"@
}
if ([HelperLaneBenchmarkPower]::SetThreadExecutionState([uint32]2147483651) -eq 0) {
    throw 'Could not request temporary system/display wakefulness.'
}
Write-Output 'Temporary system/display keep-awake request active until this run ends.'
$measurement = $null
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $measurement = Start-Process -FilePath $exe -ArgumentList $argsForRun -WorkingDirectory $sampleRoot `
        -WindowStyle Normal -PassThru -RedirectStandardOutput "$stem.log" -RedirectStandardError "$stem.stderr.log"
    $measurement.Id | Set-Content -LiteralPath "$stem.pid"
    if (!$measurement.WaitForExit([int](60000 * $MaxMinutes))) {
        Stop-Process -Id $measurement.Id -Force
        $measurement.WaitForExit()
        if (Test-Path -LiteralPath $meta) {
            Add-Content -LiteralPath $meta -Value "`nStatus: Incomplete`nMessage: Stopped at the total measurement time budget."
        }
        throw "The $MaxMinutes minute budget was reached; partial records retained."
    }
    if ($measurement.ExitCode -ne 0) { throw "Measurements failed with exit $($measurement.ExitCode); see $stem.log" }
    if (!(Select-String -LiteralPath $meta -Pattern '^Status: Complete$' -Quiet)) { throw "Incomplete run: $stem" }
    Write-Output "All models finished and the window closed; elapsed $([math]::Round($timer.Elapsed.TotalMinutes, 1)) minutes."
}
finally {
    if ($measurement -and !$measurement.HasExited) {
        Stop-Process -Id $measurement.Id -Force -ErrorAction SilentlyContinue
    }
    [void][HelperLaneBenchmarkPower]::SetThreadExecutionState([uint32]2147483648)
}
