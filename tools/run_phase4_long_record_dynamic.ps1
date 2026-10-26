param(
    [string]$BuildDir = "build/windows-mingw-release",
    [string]$ExeName = "appSubRec.exe",
    [int]$RecordDurationSec = 60,
    [string]$OutputDir = [Environment]::GetFolderPath("MyVideos"),
    [string]$OutputFileName = "output.mp4",
    [string]$FfprobePath = "",
    [double]$MinOutputDurationSec = 45.0,
    [double]$MaxAvDurationDeltaSec = 0.5,
    [int]$MinUniqueDemuxHashes = 3,
    [int]$MinUniqueEncoderHashes = 3
)

$ErrorActionPreference = "Stop"
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Get-UniqueValuesFromLog {
    param(
        [string[]]$LogLines,
        [string]$Pattern,
        [string]$Regex
    )

    $values = @()
    foreach ($line in $LogLines) {
        if ($line -match $Pattern -and $line -match $Regex) {
            $values += $matches[1]
        }
    }
    return @($values | Sort-Object -Unique)
}

function Get-ScalarFfprobeValue {
    param(
        [string]$ProbePath,
        [string[]]$ProbeArgs
    )

    $output = & $ProbePath @ProbeArgs 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $null
    }

    $text = ($output | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($text)) {
        return $null
    }
    return $text
}

function Get-StreamDurations {
    param(
        [string]$ProbePath,
        [string]$MediaPath
    )

    $probeArgs = @(
        "-v", "error",
        "-show_entries", "stream=index,codec_type,duration",
        "-of", "csv=p=0",
        $MediaPath
    )
    $output = & $ProbePath @probeArgs 2>$null
    if ($LASTEXITCODE -ne 0) {
        return @{}
    }

    $map = @{}
    foreach ($line in $output) {
        $parts = $line -split ","
        if ($parts.Count -lt 3) {
            continue
        }
        $codecType = $parts[1].Trim()
        $durationText = $parts[2].Trim()
        $duration = 0.0
        if ([double]::TryParse($durationText, [ref]$duration)) {
            $map[$codecType] = $duration
        }
    }
    return $map
}

$buildPath = Resolve-Path $BuildDir
$exePath = Join-Path $buildPath $ExeName
$logPath = Join-Path $buildPath "SubRec.log"
$runTag = Get-Date -Format "yyyyMMdd-HHmmss"
$artifactDir = Join-Path $buildPath ("test-logs\\phase4-long-record-" + $runTag)
$outputPath = Join-Path $OutputDir $OutputFileName
$capturedOutputPath = Join-Path $artifactDir ("captured-" + $OutputFileName)
$backupOutputPath = Join-Path $artifactDir ("backup-before-run-" + $OutputFileName)

if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath"
}
if ([string]::IsNullOrWhiteSpace($FfprobePath)) {
    $ffprobeCommand = Get-Command ffprobe -ErrorAction SilentlyContinue
    if ($ffprobeCommand) { $FfprobePath = $ffprobeCommand.Source }
}
if ([string]::IsNullOrWhiteSpace($FfprobePath) -or -not (Test-Path $FfprobePath)) {
    throw "ffprobe not found: $FfprobePath"
}

New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null

$hadPreviousOutput = Test-Path $outputPath
$process = $null

try {
    if ($hadPreviousOutput) {
        Move-Item $outputPath $backupOutputPath -Force
    }

    if (Test-Path $logPath) {
        Remove-Item $logPath -Force
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exePath
    $psi.WorkingDirectory = $buildPath
    $psi.UseShellExecute = $false
    $psi.Environment["SUBREC_AUTOTEST_SCENARIO"] = "phase4_long_record_dynamic_screen"
    $psi.Environment["SUBREC_AUTOTEST_QUIT_MS"] = [string](($RecordDurationSec + 2) * 1000)

    Write-Host ""
    Write-Host "=== Phase 4 Long Record Dynamic Scene ===" -ForegroundColor Cyan
    Write-Host ("record_sec={0}, scenario={1}" -f $RecordDurationSec, $psi.Environment["SUBREC_AUTOTEST_SCENARIO"])

    $process = [System.Diagnostics.Process]::Start($psi)
    $timeoutMs = ($RecordDurationSec + 30) * 1000
    $exited = $process.WaitForExit($timeoutMs)

    if (-not $exited) {
        Write-Host "Scenario timed out, killing process" -ForegroundColor Yellow
        $process.Kill()
        $process.WaitForExit()
    }

    $artifactLogPath = Join-Path $artifactDir "phase4-long-record.log"
    $logLines = @()
    if (Test-Path $logPath) {
        Copy-Item $logPath $artifactLogPath -Force
        $logLines = Get-Content $logPath
    }

    $demuxHashes = Get-UniqueValuesFromLog -LogLines $logLines -Pattern "\[DemuxHash\]" -Regex 'hash=0x\s+"([0-9a-f]+)"'
    $encoderHashes = Get-UniqueValuesFromLog -LogLines $logLines -Pattern "\[EncoderInputMd5\]" -Regex 'md5=\s*([0-9a-f]+)'
    $previewHashes = Get-UniqueValuesFromLog -LogLines $logLines -Pattern "\[PreviewRenderHash\]" -Regex 'outputMd5=\s*([0-9a-f]+)'
    $monotonicWarnings = @($logLines | Select-String -Pattern "non monotonically increasing dts|Non-monotonous DTS").Count
    $startCount = @($logLines | Select-String -Pattern 'state "Idle" -> "Preparing"').Count
    $idleCount = @($logLines | Select-String -Pattern 'state "Stopping" -> "Idle"').Count
    $quitCount = @($logLines | Select-String -Pattern '\[AutoTest\] quit').Count

    $outputCopied = $false
    if (Test-Path $outputPath) {
        Copy-Item $outputPath $capturedOutputPath -Force
        $outputCopied = $true
    }

    $formatDuration = $null
    $streamDurations = @{}
    $audioDuration = $null
    $videoDuration = $null
    $avDurationDelta = $null
    if ($outputCopied) {
        $durationText = Get-ScalarFfprobeValue -ProbePath $FfprobePath -ProbeArgs @(
            "-v", "error",
            "-show_entries", "format=duration",
            "-of", "default=noprint_wrappers=1:nokey=1",
            $capturedOutputPath
        )
        if ($durationText) {
            $tmp = 0.0
            if ([double]::TryParse($durationText, [ref]$tmp)) {
                $formatDuration = $tmp
            }
        }

        $streamDurations = Get-StreamDurations -ProbePath $FfprobePath -MediaPath $capturedOutputPath
        if ($streamDurations.ContainsKey("audio")) {
            $audioDuration = $streamDurations["audio"]
        }
        if ($streamDurations.ContainsKey("video")) {
            $videoDuration = $streamDurations["video"]
        }
        if ($null -ne $audioDuration -and $null -ne $videoDuration) {
            $avDurationDelta = [Math]::Abs($audioDuration - $videoDuration)
        }
    }

    $checks = @(
        [pscustomobject]@{ Label = "process exited"; Passed = ($exited -and $process.ExitCode -eq 0); Detail = ("exited={0} exitCode={1}" -f $exited, $process.ExitCode) }
        [pscustomobject]@{ Label = "scheduler start once"; Passed = ($startCount -ge 1); Detail = ("actual={0}" -f $startCount) }
        [pscustomobject]@{ Label = "scheduler back idle"; Passed = ($idleCount -ge 1); Detail = ("actual={0}" -f $idleCount) }
        [pscustomobject]@{ Label = "autotest quit"; Passed = ($quitCount -ge 1); Detail = ("actual={0}" -f $quitCount) }
        [pscustomobject]@{ Label = "dynamic demux hashes"; Passed = ($demuxHashes.Count -ge $MinUniqueDemuxHashes); Detail = ("unique={0}" -f $demuxHashes.Count) }
        [pscustomobject]@{ Label = "dynamic encoder hashes"; Passed = ($encoderHashes.Count -ge $MinUniqueEncoderHashes); Detail = ("unique={0}" -f $encoderHashes.Count) }
        [pscustomobject]@{ Label = "no monotonic warnings"; Passed = ($monotonicWarnings -eq 0); Detail = ("count={0}" -f $monotonicWarnings) }
        [pscustomobject]@{ Label = "output copied"; Passed = $outputCopied; Detail = ("path={0}" -f $capturedOutputPath) }
    )

    if ($null -ne $formatDuration) {
        $checks += [pscustomobject]@{
            Label = "format duration acceptable"
            Passed = ($formatDuration -ge $MinOutputDurationSec)
            Detail = ("duration={0:N3}s" -f $formatDuration)
        }
    }

    if ($null -ne $avDurationDelta) {
        $checks += [pscustomobject]@{
            Label = "audio/video duration delta"
            Passed = ($avDurationDelta -le $MaxAvDurationDeltaSec)
            Detail = ("delta={0:N3}s audio={1:N3}s video={2:N3}s" -f $avDurationDelta, $audioDuration, $videoDuration)
        }
    }

    Write-Host ""
    Write-Host "=== Metrics ===" -ForegroundColor Cyan
    Write-Host ("demux_hashes={0}" -f ($demuxHashes -join ","))
    Write-Host ("encoder_hashes={0}" -f ($encoderHashes -join ","))
    Write-Host ("preview_render_hashes={0}" -f ($previewHashes -join ","))
    if ($null -ne $formatDuration) {
        Write-Host ("format_duration={0:N3}s" -f $formatDuration)
    }
    if ($null -ne $audioDuration) {
        Write-Host ("audio_duration={0:N3}s" -f $audioDuration)
    }
    if ($null -ne $videoDuration) {
        Write-Host ("video_duration={0:N3}s" -f $videoDuration)
    }
    if ($null -ne $avDurationDelta) {
        Write-Host ("av_duration_delta={0:N3}s" -f $avDurationDelta)
    }

    Write-Host ""
    Write-Host "=== Checks ===" -ForegroundColor Cyan
    foreach ($check in $checks) {
        $status = if ($check.Passed) { "PASS" } else { "FAIL" }
        Write-Host ("[{0}] {1}: {2}" -f $status, $check.Label, $check.Detail)
    }

    $summary = [pscustomobject]@{
        RecordDurationSec = $RecordDurationSec
        ArtifactDir = $artifactDir
        LogPath = $artifactLogPath
        OutputPath = $capturedOutputPath
        DemuxUnique = $demuxHashes.Count
        EncoderUnique = $encoderHashes.Count
        PreviewUnique = $previewHashes.Count
        FormatDuration = $formatDuration
        AudioDuration = $audioDuration
        VideoDuration = $videoDuration
        AvDurationDelta = $avDurationDelta
        ExitCode = $process.ExitCode
    }
    $summary | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $artifactDir "summary.json") -Encoding UTF8

    $failedChecks = @($checks | Where-Object { -not $_.Passed })
    if ($failedChecks.Count -gt 0) {
        exit 1
    }

    exit 0
}
finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    if ($hadPreviousOutput -and (Test-Path $backupOutputPath)) {
        if (Test-Path $outputPath) {
            Remove-Item $outputPath -Force
        }
        Move-Item $backupOutputPath $outputPath -Force
    }
}
