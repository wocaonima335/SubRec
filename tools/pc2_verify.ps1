<#
.SYNOPSIS
  PC2 verification workflow for SubRec: build the app and all tests with a given
  Qt/MinGW kit and FFmpeg, run ctest, check ddagrab with ffmpeg.exe, then record N seconds of
  screen (ddagrab) + a chosen DirectShow microphone with the app's autotest mode.

.DESCRIPTION
  Status: the user confirms this branch's full test suite passed on PC2.
  This script covers build/tests, device probes, recordings and MP4 readback;
  the test-suite result alone does not attest to every script stage or optional mode.
  Check the summary.txt from the actual invocation for its end-to-end result.

  Nothing is installed, deleted or deployed. Every build goes to -BuildRoot (created if
  missing, never cleaned). Recordings and logs go to -BuildRoot\pc2-run-<timestamp>.
  Exit code: 0 = every step passed, 1 = at least one step failed (see summary.txt).

  The app's autotest environment (main.cpp, scenario "record_seconds"):
    SUBREC_AUTOTEST_SCENARIO=record_seconds   start, record, stop, check the file, exit
    SUBREC_AUTOTEST_RECORD_MS=5000            recording length after the capture is Running
    SUBREC_AUTOTEST_MIC_DEVICE=<name|id>      microphone (exact name/id, or unique substring)
    SUBREC_AUTOTEST_OUTPUT=<dir>\<file>.mp4   output file
    SUBREC_CAPTURE_BACKEND=ddagrab|dshow|auto screen backend (overrides the settings page)
    SUBREC_DDAGRAB_MODE=auto|gpu|cpu          ddagrab frames: D3D11 passthrough or system memory
    SUBREC_DDAGRAB_OUTPUT=<n>                 DXGI output index (default 0)
    SUBREC_FFMPEG_LOG=info|verbose|debug      FFmpeg messages into SubRec.log
  App exit codes: 0 ok, 2 bad MIC_DEVICE/OUTPUT, 3 never Running, 4 no/empty file, 5 stop timeout, 7 captureError.

.EXAMPLE
  powershell -NoProfile -File tools\pc2_verify.ps1 `
    -QtPrefix E:\Qt\6.8.3\mingw_64 -MinGWBin E:\Qt\Tools\mingw1310_64\bin `
    -FfmpegRoot E:\ffmpeg-subrec-gpl-win64 -MicDevice '<microphone name>'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtPrefix,     # folder with bin\qmake.exe, lib\cmake\Qt6
    [Parameter(Mandatory = $true)][string]$MinGWBin,     # folder with g++.exe (MinGW 13.1 of the Qt kit)
    [Parameter(Mandatory = $true)][string]$FfmpegRoot,   # bin\ include\ lib\ (build-ffmpeg.sh output)
    [string]$MicDevice = '',                             # empty: record without selecting a microphone
    [string]$SourceDir = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildRoot = '',                             # default: <SourceDir>\build\pc2
    [string]$OutputDir = '',                             # default: run directory (use D:\SubRec_test\out on PC3)
    [string]$ArtifactRoot = '',                          # optional root for logs/results (use D:\SubRec_test\logs)
    [string]$CMakeBin = '',                              # folder with cmake.exe/ctest.exe if not on PATH
    [string]$NinjaBin = '',                              # folder with ninja.exe if not on PATH
    [int]$RecordMs = 5000,
    [string[]]$DdagrabModes = @('auto', 'cpu'),          # one recording per mode
    [switch]$WithDshowRecording,                         # also record once with SUBREC_CAPTURE_BACKEND=dshow
    [switch]$WithProbe,                                  # build tests\native_capture_probe and list devices
    [switch]$SkipBuild                                   # reuse existing builds
)

Set-StrictMode -Version 3
. (Join-Path $PSScriptRoot 'mp4_edit_list.ps1')
# Not 'Stop': Windows PowerShell 5.1 turns redirected stderr of native tools (ffmpeg, cmake)
# into error records, which 'Stop' would make fatal. Failures are checked via exit codes.
$ErrorActionPreference = 'Continue'

if (-not $BuildRoot) { $BuildRoot = Join-Path $SourceDir 'build\pc2' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$artifactRoot = if ($ArtifactRoot) { $ArtifactRoot } else { $BuildRoot }
$runDir = Join-Path $artifactRoot "pc2-run-$stamp"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
if (-not $OutputDir) { $OutputDir = $runDir }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$Step, [bool]$Ok, [string]$Detail) {
    $results.Add([pscustomobject]@{ Step = $Step; Result = $(if ($Ok) { 'PASS' } else { 'FAIL' }); Detail = $Detail })
    $color = if ($Ok) { 'Green' } else { 'Red' }
    Write-Host ("[{0}] {1} {2}" -f $(if ($Ok) { 'PASS' } else { 'FAIL' }), $Step, $Detail) -ForegroundColor $color
}

# Runs a native command, tees its output to a log, returns the exit code.
function Invoke-Logged([string]$Name, [string]$Exe, [string[]]$Arguments) {
    $log = Join-Path $runDir "$Name.log"
    Write-Host "==> $Name : $Exe $($Arguments -join ' ')"
    & $Exe @Arguments *>&1 | Tee-Object -FilePath $log | Out-Host
    return $LASTEXITCODE
}

# --- toolchain on PATH (this process only) ---------------------------------------------------
foreach ($p in @($QtPrefix, $MinGWBin, $FfmpegRoot)) {
    if (-not (Test-Path $p)) { throw "missing path: $p" }
}
$pathParts = @((Join-Path $QtPrefix 'bin'), $MinGWBin, (Join-Path $FfmpegRoot 'bin'))
if ($CMakeBin) { $pathParts += $CMakeBin }
if ($NinjaBin) { $pathParts += $NinjaBin }
$env:PATH = ($pathParts -join ';') + ';' + $env:PATH
foreach ($tool in 'cmake', 'ctest', 'ninja', 'g++') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found (use -CMakeBin/-NinjaBin/-MinGWBin)" }
}
$ffmpegExe = Join-Path $FfmpegRoot 'bin\ffmpeg.exe'
$commonCMake = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtPrefix",
                 "-DFFMPEG_ROOT=$FfmpegRoot", "-DCMAKE_C_COMPILER=$(Join-Path $MinGWBin 'gcc.exe')",
                 "-DCMAKE_CXX_COMPILER=$(Join-Path $MinGWBin 'g++.exe')")
"source=$SourceDir`nqt=$QtPrefix`nmingw=$MinGWBin`nffmpeg=$FfmpegRoot`nmic=$MicDevice" |
    Out-File (Join-Path $runDir 'inputs.txt') -Encoding utf8
& g++ --version | Select-Object -First 1 | Out-File (Join-Path $runDir 'inputs.txt') -Append -Encoding utf8

# --- 1. main project (appSubRec + in-tree ctests: capture_lifecycle, audio_filter_dual) --------
$mainBuild = Join-Path $BuildRoot 'main'
if (-not $SkipBuild) {
    $rc = Invoke-Logged 'main-configure' 'cmake' (@('-S', $SourceDir, '-B', $mainBuild) + $commonCMake)
    if ($rc -eq 0) { $rc = Invoke-Logged 'main-build' 'cmake' @('--build', $mainBuild) }
    Add-Result 'main build' ($rc -eq 0) "rc=$rc"
}
$rc = Invoke-Logged 'main-ctest' 'ctest' @('--test-dir', $mainBuild, '--output-on-failure')
Add-Result 'main ctest (capture_lifecycle, audio_filter_dual)' ($rc -eq 0) "rc=$rc"

# --- 2. standalone test projects -------------------------------------------------------------
foreach ($t in 'input_startup', 'media_conversion', 'muxer_lifecycle', 'wasapi_loopback', 'audio_timeline_mix') {
    $dir = Join-Path $BuildRoot $t
    if (-not $SkipBuild) {
        $rc = Invoke-Logged "$t-configure" 'cmake' (@('-S', (Join-Path $SourceDir "tests\$t"), '-B', $dir) + $commonCMake)
        if ($rc -eq 0) { $rc = Invoke-Logged "$t-build" 'cmake' @('--build', $dir) }
        if ($rc -ne 0) { Add-Result "$t build" $false "rc=$rc"; continue }
    }
    $rc = Invoke-Logged "$t-ctest" 'ctest' @('--test-dir', $dir, '--output-on-failure')
    Add-Result "$t ctest" ($rc -eq 0) "rc=$rc"
}
if ($WithProbe) {
    $dir = Join-Path $BuildRoot 'native_capture_probe'
    $rc = Invoke-Logged 'probe-configure' 'cmake' (@('-S', (Join-Path $SourceDir 'tests\native_capture_probe'), '-B', $dir) + $commonCMake)
    if ($rc -eq 0) { $rc = Invoke-Logged 'probe-build' 'cmake' @('--build', $dir) }
    if ($rc -eq 0) {
        $probe = Join-Path $dir 'native_capture_probe.exe'
        $rc1 = Invoke-Logged 'probe-wasapi-list' $probe @('wasapi', '--list')
        $rc2 = Invoke-Logged 'probe-dxgi-list' $probe @('dxgi', '--list')
        Add-Result 'native_capture_probe --list' (($rc1 -eq 0) -and ($rc2 -eq 0)) "wasapi=$rc1 dxgi=$rc2"
    } else { Add-Result 'native_capture_probe build' $false "rc=$rc" }
}

# --- 3. FFmpeg: ddagrab present and working (no SubRec code involved) -----------------------
$filters = (& $ffmpegExe -hide_banner -filters 2>&1) -join "`n"
$missing = @(@('ddagrab', 'hwdownload', 'hwupload', 'hwmap', 'scale_d3d11') | Where-Object { $filters -notmatch "(?m)\s$_\s" })
Add-Result 'ffmpeg.exe -filters' ($missing.Count -eq 0) $(if ($missing) { "missing: $($missing -join ',')" } else { 'ddagrab hwdownload hwupload hwmap scale_d3d11' })
$cpuClip = Join-Path $runDir 'ffmpeg-ddagrab-cpu-x264.mp4'
$rc = Invoke-Logged 'ffmpeg-ddagrab-cpu' $ffmpegExe @('-hide_banner', '-y', '-filter_complex', 'ddagrab=framerate=30,hwdownload,format=bgra', '-t', '3', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', $cpuClip)
Add-Result 'ffmpeg ddagrab -> hwdownload -> libx264' ($rc -eq 0) "rc=$rc file=$cpuClip"
$gpuClip = Join-Path $runDir 'ffmpeg-ddagrab-gpu-nvenc.mp4'
$rc = Invoke-Logged 'ffmpeg-ddagrab-gpu' $ffmpegExe @('-hide_banner', '-y', '-filter_complex', 'ddagrab=framerate=30', '-t', '3', '-c:v', 'h264_nvenc', $gpuClip)
Add-Result 'ffmpeg ddagrab (D3D11) -> h264_nvenc' ($rc -eq 0) "rc=$rc file=$gpuClip"
# dshow device names (exit code is non-zero by design of -list_devices).
Invoke-Logged 'ffmpeg-dshow-devices' $ffmpegExe @('-hide_banner', '-list_devices', 'true', '-f', 'dshow', '-i', 'dummy') | Out-Null

# --- 4. SubRec autotest recordings --------------------------------------------------------------
$app = Join-Path $mainBuild 'appSubRec.exe'
$appLog = Join-Path $mainBuild 'SubRec.log'   # main.cpp: <applicationDirPath>\SubRec.log (appended)

function Invoke-Recording([string]$Name, [hashtable]$Environment) {
    $out = Join-Path $runDir "$Name.mp4"
    if ($OutputDir) { $out = Join-Path $OutputDir "$Name.mp4" }
    $vars = @{
        SUBREC_AUTOTEST_SCENARIO  = 'record_seconds'
        SUBREC_AUTOTEST_RECORD_MS = "$RecordMs"
        SUBREC_AUTOTEST_OUTPUT    = $out
        SUBREC_FFMPEG_LOG         = 'info'
    }
    if ($MicDevice) { $vars['SUBREC_AUTOTEST_MIC_DEVICE'] = $MicDevice }
    foreach ($k in $Environment.Keys) { $vars[$k] = $Environment[$k] }
    $logStart = if (Test-Path $appLog) { (Get-Item $appLog).Length } else { 0 }
    $saved = @{}
    foreach ($k in $vars.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $vars[$k]) }
    try {
        Write-Host "==> recording $Name ($(($vars.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' '))"
        # Process.Start instead of Start-Process: reliable ExitCode and a hard timeout.
        $psi = New-Object System.Diagnostics.ProcessStartInfo $app
        $psi.WorkingDirectory = $mainBuild
        $psi.UseShellExecute = $false   # inherit this process's environment (the vars above)
        $proc = [System.Diagnostics.Process]::Start($psi)
        if (-not $proc.WaitForExit($RecordMs + 90000)) {
            $proc.Kill()
            $proc.WaitForExit()
            Write-Host "recording $Name timed out; process killed" -ForegroundColor Red
            $code = -1
        } else {
            $code = $proc.ExitCode
        }
    } finally {
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
    }
    # Copy this run's part of SubRec.log (UTF-8).
    $runLog = Join-Path $runDir "$Name-SubRec.log"
    if (Test-Path $appLog) {
        $fs = [System.IO.File]::Open($appLog, 'Open', 'Read', 'ReadWrite')
        try {
            $null = $fs.Seek($logStart, 'Begin')
            $reader = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
            [System.IO.File]::WriteAllText($runLog, $reader.ReadToEnd(), [System.Text.Encoding]::UTF8)
        } finally { $fs.Dispose() }
    }
    $summary = ''
    if (Test-Path $runLog) {
        $summary = (Select-String -Path $runLog -Pattern '\[AutoTest\] result=|\[AutoTest\] exit code|\[CaptureError\]|\[VEncSelection\] winner=|\[Ddagrab\] opened|\[initVideo\] (ddagrab|falling back)' |
                    ForEach-Object { $_.Line.Trim() }) -join ' | '
    }
    $captureErrors = if (Test-Path $runLog) { @(Select-String -Path $runLog -Pattern '\[CaptureError\]' | ForEach-Object { $_.Line.Trim() }) } else { @() }
    Add-Result "record $Name (app exit code)" (($code -eq 0) -and $captureErrors.Count -eq 0) "exit=$code captureError=$($captureErrors -join ' | ') $summary"
    if ($Name -like 'rec-wasapi-*') {
        $nativeStarted = (Test-Path $runLog) -and [bool](Select-String -Path $runLog -SimpleMatch '[WASAPI] started')
        Add-Result "WASAPI source $Name" $nativeStarted 'Requires native source startup; a DirectShow fallback does not pass.'
        if ($Name -eq 'rec-wasapi-mix') {
            $mixed = (Test-Path $runLog) -and [bool](Select-String -Path $runLog -Pattern '\[AudioMix\] finished system_frames= [1-9][0-9]* mic_frames= [1-9][0-9]*')
            Add-Result 'mix consumed both sources' $mixed 'Requires non-zero system and microphone frame counts.'
        }
    }

    # Read the file back without decoding (needs the mov demuxer of the test components).
    if (Test-Path $out) {
        $probeLog = Join-Path $runDir "$Name-readback.log"
        $text = (& $ffmpegExe -hide_banner -i $out -c copy -f null - 2>&1) -join "`n"
        $rc = $LASTEXITCODE
        $text | Out-File $probeLog -Encoding utf8
        $hasVideo = $text -match 'Video: h264'
        $hasAudio = $text -match 'Audio: aac'
        $duration = if ($text -match 'Duration: (\d\d:\d\d:\d\d\.\d\d)') { $Matches[1] } else { '?' }
        $durationOk = $duration -ne '?' -and [Math]::Abs(([TimeSpan]::Parse($duration)).TotalSeconds - $RecordMs / 1000.0) -lt 1.5
        Add-Result "readback $Name" (($rc -eq 0) -and $hasVideo -and $hasAudio -and $durationOk) "rc=$rc h264=$hasVideo aac=$hasAudio duration=$duration bytes=$((Get-Item $out).Length)"
        # No ffprobe in the minimal build: inspect this audio track's edit list directly.
        try {
            $timing = Assert-Mp4AacPriming $out
            $timing | Out-File (Join-Path $runDir "$Name-edit-list.log") -Encoding utf8
            Add-Result "AAC priming $Name" $true $timing
        } catch {
            Add-Result "AAC priming $Name" $false $_.Exception.Message
        }
    } else {
        Add-Result "readback $Name" $false "no file $out"
    }
}

if (Test-Path $app) {
    # Generated tone uses SoundPlayer on the default render endpoint; no FFmpeg build changes.
    $tone = Join-Path $runDir 'wasapi-tone.wav'
    $writer = New-Object System.IO.BinaryWriter([System.IO.File]::Create($tone))
    try {
        $rate = 48000; $count = $rate * 2; $bytes = $count * 2
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF')); $writer.Write([int](36 + $bytes))
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $writer.Write([int]16)
        $writer.Write([int16]1); $writer.Write([int16]1); $writer.Write([int]$rate)
        $writer.Write([int]($rate * 2)); $writer.Write([int16]2); $writer.Write([int16]16)
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data')); $writer.Write([int]$bytes)
        for ($i = 0; $i -lt $count; $i++) { $writer.Write([int16](8000 * [Math]::Sin(2 * [Math]::PI * 440 * $i / $rate))) }
    } finally { $writer.Dispose() }
    $player = New-Object System.Media.SoundPlayer $tone
    $systemVars = @{ SUBREC_SYSTEM_AUDIO_BACKEND = 'wasapi'; SUBREC_AUTOTEST_MIC_DEVICE = ''; SUBREC_AUTOTEST_SYSTEM_AUDIO = '1'; SUBREC_AUTOTEST_RECORD_MS = '5000' }
    $savedRecordMs = $RecordMs
    $RecordMs = 5000
    try {
        $player.PlayLooping()
        Invoke-Recording 'rec-wasapi-tone' $systemVars
        if ($MicDevice) {
            $mixVars = $systemVars.Clone()
            $mixVars['SUBREC_AUTOTEST_MIC_DEVICE'] = $MicDevice
            Invoke-Recording 'rec-wasapi-mix' $mixVars
        }
        $player.Stop()
        Write-Host 'Idle test: stop other applications playing audio to exercise no-packet silence filling.'
        Invoke-Recording 'rec-wasapi-idle' $systemVars
    } finally { $player.Stop(); $player.Dispose(); $RecordMs = $savedRecordMs }
    foreach ($mode in $DdagrabModes) {
        Invoke-Recording "rec-ddagrab-$mode" @{ SUBREC_CAPTURE_BACKEND = 'ddagrab'; SUBREC_DDAGRAB_MODE = $mode }
    }
    if ($WithDshowRecording) {
        Invoke-Recording 'rec-dshow' @{ SUBREC_CAPTURE_BACKEND = 'dshow' }
    }
} else {
    Add-Result 'recordings' $false "app not built: $app"
}

# --- summary ------------------------------------------------------------------------------------
$summaryFile = Join-Path $runDir 'summary.txt'
$results | Format-Table -AutoSize -Wrap | Out-String -Width 400 | Tee-Object -FilePath $summaryFile | Out-Host
Write-Host "Logs and recordings: $runDir"
if ($results | Where-Object { $_.Result -eq 'FAIL' }) { exit 1 }
exit 0
