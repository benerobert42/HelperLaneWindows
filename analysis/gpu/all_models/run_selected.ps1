$ErrorActionPreference='Stop'
foreach ($case in (Import-Csv "$PSScriptRoot/confirmation_plan.csv")) {
    # Historical plans keep their measured input path; use the deployed source asset on a fresh checkout.
    $case.Geometry = $case.Geometry.Replace('obj:analysis/gpu/orientation/models/', 'obj:Data/Models/')
    $slug=if ($case.Geometry.StartsWith('obj:')) { [IO.Path]::GetFileNameWithoutExtension($case.Geometry.Substring(4)) } else { $case.Geometry }
    & "$PSScriptRoot/../orientation/run_sweep.ps1" -Models $case.Geometry -Steps 8 -Samples 128 -Warmup 256 -Blocks 3 `
        -Methods $case.Methods -Prefix "selected_${slug}_" -OutputDirectory 'analysis/gpu/all_models'
}
