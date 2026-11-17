# Runs the offscreen self test with the test VST3 instrument, so the hosting path
# is exercised end to end. The release script does the same thing.
param(
    [string] $Out = (Join-Path $PSScriptRoot 'selftest-vst'),
    [string] $Exe = (Join-Path $PSScriptRoot 'app-build5\NiDaw_artefacts\Release\HM.exe'),
    [string] $Plugin = (Join-Path $PSScriptRoot 'app-build5\NiDawTestInstrument_artefacts\Release\VST3\HMTestInstrument.vst3')
    ,[string] $Effect = (Join-Path $PSScriptRoot 'app-build5\NiDawTestEffect_artefacts\Release\VST3\HMTestEffect.vst3')
)

# Restricted containers only allow writes inside the workspace, and the self
# test writes its plugin scan results and scratch projects to TEMP.  A private
# folder per run also keeps stale fixtures from a previous run out of the result
# (a shared, dirty temp folder once made the wavetable check see three tables).
$TempRoot = Join-Path $PSScriptRoot ("tmp-selftest-" + $PID)
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
$env:TEMP = $TempRoot
$env:TMP  = $TempRoot

$exitCode = 1

try {
    $process = Start-Process -FilePath $Exe -ArgumentList "--selftest", $Out, "--vst3", $Plugin, "--vst3fx", $Effect -Wait -PassThru
    Write-Output ("EXIT=" + $process.ExitCode)
    $exitCode = $process.ExitCode
}
finally {
    try { Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction Stop } catch { }
}

exit $exitCode
