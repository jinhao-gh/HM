param(
    [int] $TimeoutSeconds = 240
)

$exe = Join-Path $PSScriptRoot 'test-build22\HMDAWTests_artefacts\Release\HMDAWTests.exe'

# A restricted container (the Codex/CI sandbox this project is developed in)
# only lets a child process write inside the workspace, while the tests put
# their fixtures in %TEMP%.  Give every run a private folder under work/ and
# point TEMP/TMP at it: the suite then behaves the same on a normal machine,
# in a sandbox, and when several runs happen at once.
$TempRoot = Join-Path $PSScriptRoot ("tmp-tests-" + $PID)
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
$env:TEMP = $TempRoot
$env:TMP  = $TempRoot

function Run-Exe([string] $argString, [int] $timeoutSeconds) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = $argString
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $process = [System.Diagnostics.Process]::Start($psi)

    if (-not $process.WaitForExit($timeoutSeconds * 1000)) {
        $process.Kill()
        return @{ TimedOut = $true; Exit = -1
                  Out = $process.StandardOutput.ReadToEnd()
                  Err = $process.StandardError.ReadToEnd() }
    }

    return @{ TimedOut = $false; Exit = $process.ExitCode
              Out = $process.StandardOutput.ReadToEnd()
              Err = $process.StandardError.ReadToEnd() }
}

try {
    $listing = Run-Exe "--list" 30
    $groups = ($listing.Out -split "`r?`n") | Where-Object { $_.Trim().Length -gt 0 }

    $totalFailures = 0
    $totalPasses = 0
    $badGroups = @()

    foreach ($group in $groups) {
        $result = Run-Exe ('"' + $group + '"') $TimeoutSeconds

        if ($result.TimedOut) {
            Write-Output ("  {0} : TIMEOUT after {1} s" -f $group, $TimeoutSeconds)
            $badGroups += $group
            $totalFailures++
        }
        else {
            foreach ($line in ($result.Out -split "`r?`n")) {
                if ($line -match ": \d+ passed, \d+ failed") {
                    Write-Output ("  " + $line.Trim())

                    if ($line -match ": (\d+) passed, (\d+) failed") {
                        $totalPasses += [int] $Matches[1]
                        $totalFailures += [int] $Matches[2]
                    }
                }
            }

            if ($result.Exit -ne 0) {
                if ($badGroups -notcontains $group) { $badGroups += $group }
                Write-Output ("      " + (($result.Out -split "`r?`n" | Where-Object { $_ -match "FAILED|error" } | Select-Object -First 6) -join " | "))
            }
        }
    }

    Write-Output ""
    Write-Output ("groups=" + $groups.Count + " passed=" + $totalPasses + " failed=" + $totalFailures)

    if ($totalFailures -eq 0 -and $badGroups.Count -eq 0) {
        Write-Output "ALL TESTS PASSED"
    }
    else {
        Write-Output ("TESTS FAILED: " + ($badGroups -join ", "))
    }
}
finally {
    # Scratch data only: a locked file must not hide the result above.
    try { Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction Stop } catch { }
}
