param(
    [string]$BuildDir = "build/windows-mingw-release",
    [string]$ExeName = "appSubRec.exe",
    [string]$ExpectedEncoder = ""
)

$ErrorActionPreference = "Stop"

$buildPath = Resolve-Path $BuildDir
$exePath = Join-Path $buildPath $ExeName
$logPath = Join-Path $buildPath "SubRec.log"
$artifactDir = Join-Path $buildPath "test-logs"

if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath"
}

New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null

$scenarios = @(
    [pscustomobject]@{
        Name = "play-stop"
        Scenario = "start_wait5_stop"
        QuitMs = 12000
        TimeoutMs = 18000
        Required = @(
            @{ Pattern = 'state "Idle" -> "Preparing"'; MinCount = 1; Label = "start once" }
            @{ Pattern = 'state "Stopping" -> "Idle"'; MinCount = 1; Label = "stop back to idle" }
            @{ Pattern = '\[VEncSelection\] winner='; MinCount = 1; Label = "encoder winner" }
            @{ Pattern = '\[AutoTest\] quit'; MinCount = 1; Label = "auto quit" }
        )
    }
    [pscustomobject]@{
        Name = "play-stop-play"
        Scenario = "start_stop_start_stop"
        QuitMs = 16000
        TimeoutMs = 24000
        Required = @(
            @{ Pattern = 'state "Idle" -> "Preparing"'; MinCount = 2; Label = "start twice" }
            @{ Pattern = 'state "Stopping" -> "Idle"'; MinCount = 2; Label = "stop twice back to idle" }
            @{ Pattern = '\[VEncSelection\] winner='; MinCount = 2; Label = "encoder winners" }
            @{ Pattern = '\[AutoTest\] quit'; MinCount = 1; Label = "auto quit" }
        )
    }
    [pscustomobject]@{
        Name = "play-pause-play"
        Scenario = "start_then_pause_resume_stop"
        QuitMs = 12000
        TimeoutMs = 18000
        Required = @(
            @{ Pattern = 'state "Idle" -> "Preparing"'; MinCount = 1; Label = "start once" }
            @{ Pattern = 'state "Running" -> "Paused"'; MinCount = 1; Label = "pause once" }
            @{ Pattern = 'state "Paused" -> "Running"'; MinCount = 1; Label = "resume once" }
            @{ Pattern = 'state "Stopping" -> "Idle"'; MinCount = 1; Label = "stop back to idle" }
            @{ Pattern = '\[VEncSelection\] winner='; MinCount = 1; Label = "encoder winner" }
            @{ Pattern = '\[AutoTest\] quit'; MinCount = 1; Label = "auto quit" }
        )
    }
    [pscustomobject]@{
        Name = "select-system-audio"
        Scenario = "select_system_then_start_stop"
        QuitMs = 5000
        TimeoutMs = 10000
        Required = @(
            @{ Pattern = '\[CaptureScheduler\] audio source set to system'; MinCount = 1; Label = "system audio selection accepted" }
            @{ Pattern = '\[CaptureSession\] start.*audioSource= system'; MinCount = 1; Label = "system audio session config" }
            @{ Pattern = '\[AutoTest\] quit'; MinCount = 1; Label = "auto quit" }
        )
    }
    [pscustomobject]@{
        Name = "select-microphone"
        Scenario = "select_microphone_then_start_stop"
        QuitMs = 5000
        TimeoutMs = 10000
        Required = @(
            @{ Pattern = '\[CaptureScheduler\] audio source set to microphone'; MinCount = 1; Label = "microphone selection accepted" }
            @{ Pattern = '\[CaptureSession\] start.*audioSource= microphone'; MinCount = 1; Label = "microphone session config" }
            @{ Pattern = '\[AutoTest\] quit'; MinCount = 1; Label = "auto quit" }
        )
    }
)

$results = @()

foreach ($item in $scenarios) {
    if (Test-Path $logPath) {
        Remove-Item $logPath -Force
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exePath
    $psi.WorkingDirectory = $buildPath
    $psi.UseShellExecute = $false
    $psi.Environment["SUBREC_AUTOTEST_SCENARIO"] = $item.Scenario
    $psi.Environment["SUBREC_AUTOTEST_QUIT_MS"] = [string]$item.QuitMs

    Write-Host ""
    Write-Host "=== Scenario: $($item.Name) [$($item.Scenario)] ===" -ForegroundColor Cyan
    $process = [System.Diagnostics.Process]::Start($psi)
    $exited = $process.WaitForExit($item.TimeoutMs)

    if (-not $exited) {
        Write-Host "Scenario timed out, killing process" -ForegroundColor Yellow
        $process.Kill()
        $process.WaitForExit()
    }

    $logLines = @()
    $artifactPath = Join-Path $artifactDir ($item.Scenario + ".log")
    if (Test-Path $logPath) {
        Copy-Item $logPath $artifactPath -Force
        $logLines = Get-Content $logPath
    }

    $checks = foreach ($rule in $item.Required) {
        $count = 0
        if ($logLines.Count -gt 0) {
            $count = (@($logLines | Select-String -Pattern $rule.Pattern)).Count
        }

        [pscustomobject]@{
            Label = $rule.Label
            Pattern = $rule.Pattern
            MinCount = $rule.MinCount
            ActualCount = $count
            Passed = ($count -ge $rule.MinCount)
        }
    }

    $encoderCheck = $null
    if (-not [string]::IsNullOrWhiteSpace($ExpectedEncoder) -and $item.Required.Pattern -contains '\[VEncSelection\] winner=') {
        $encoderPattern = "\\[VEncSelection\\] winner=" + [regex]::Escape($ExpectedEncoder) + "\\b"
        $encoderCount = if ($logLines.Count -gt 0) {
            (@($logLines | Select-String -Pattern $encoderPattern)).Count
        } else {
            0
        }
        $encoderCheck = [pscustomobject]@{
            Label = "expected encoder $ExpectedEncoder"
            Pattern = $encoderPattern
            MinCount = 1
            ActualCount = $encoderCount
            Passed = ($encoderCount -ge 1)
        }
        $checks += $encoderCheck
    }

    $passed = ($exited -and $process.ExitCode -eq 0 -and (@($checks | Where-Object { -not $_.Passed }).Count -eq 0))

    Write-Host ("Process status: exited={0} exitCode={1}" -f $exited, $process.ExitCode)
    foreach ($check in $checks) {
        $status = if ($check.Passed) { "PASS" } else { "FAIL" }
        Write-Host ("[{0}] {1}: actual {2} / expected >= {3}" -f $status, $check.Label, $check.ActualCount, $check.MinCount)
    }

    $results += [pscustomobject]@{
        Name = $item.Name
        Scenario = $item.Scenario
        Exited = $exited
        ExitCode = $process.ExitCode
        Passed = $passed
        LogPath = $artifactPath
    }
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
$results | Format-Table Name, Scenario, Exited, ExitCode, Passed, LogPath -AutoSize

if (@($results | Where-Object { -not $_.Passed }).Count -gt 0) {
    exit 1
}

exit 0

