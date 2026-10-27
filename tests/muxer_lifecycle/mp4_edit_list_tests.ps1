param([Parameter(Mandatory = $true)][string]$PrimingTest)
Set-StrictMode -Version 3
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../tools/mp4_edit_list.ps1')

# Use the actual FFMuxer-generated AAC/H.264 files, including negative video DTS
# and delayed video. These files are retained by the C++ test for inspection.
$lines = @(& $PrimingTest)
if ($LASTEXITCODE -ne 0) { throw 'AAC priming fixture generation failed' }
$paths = @($lines | ForEach-Object { if ($_ -match ', (.+\.mp4)$') { $Matches[1] } })
if ($paths.Count -ne 3) { throw 'Expected three generated MP4 files' }
foreach ($path in $paths) {
    Assert-Mp4AacPriming $path
    $tracks = @(Get-Mp4EditLists $path)
    $video = @($tracks | Where-Object { $_.Handler -eq 'vide' })[0]
    $expected = if ($path -like '*video-3-*') { 0.1 } else { 0 }
    if ([Math]::Abs($video.StartSeconds - $expected) -gt 0.000001) { throw 'Video offset lost in parser' }
}

# Small synthetic boxes test v0/v1 signed media_time, empty edits, large box
# headers, missing elst, and truncated data without needing a huge media file.
function U32([uint32]$Value) {
    $bytes = [BitConverter]::GetBytes($Value)
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
    return ,$bytes
}
function I64([long]$Value) {
    $bytes = [BitConverter]::GetBytes($Value)
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
    return ,$bytes
}
function Box([string]$Type, [byte[]]$Data, [bool]$Extended = $false) {
    $tag = [Text.Encoding]::ASCII.GetBytes($Type)
    if ($Extended) { return ,[byte[]]((U32 1) + $tag + (I64 ($Data.Length + 16)) + $Data) }
    return ,[byte[]]((U32 ($Data.Length + 8)) + $tag + $Data)
}
function Track([string]$Handler, [int]$Version, [long]$Media, [bool]$EmptyEdit, [string]$EditType = 'elst') {
    $flags = U32 ($Version * 16777216)
    $timeFields = if ($Version -eq 1) { [byte[]]::new(16) } else { [byte[]]::new(8) }
    $scale = if ($Handler -eq 'soun') { 48000 } else { 15360 }
    $mdhd = Box 'mdhd' ([byte[]]($flags + $timeFields + (U32 $scale)))
    $hdlr = Box 'hdlr' ([byte[]]([byte[]]::new(8) + [Text.Encoding]::ASCII.GetBytes($Handler)))
    $mdia = Box 'mdia' ([byte[]]($hdlr + $mdhd))
    $count = if ($EmptyEdit) { 2 } else { 1 }
    [byte[]]$entries = @()
    if ($EmptyEdit) {
        if ($Version -eq 1) { $entries += (I64 100) + (I64 -1) + (U32 65536) }
        else { $entries += (U32 100) + (U32 ([uint32]::MaxValue)) + (U32 65536) }
    }
    if ($Version -eq 1) { $entries += (I64 1000) + (I64 $Media) + (U32 65536) }
    else { $entries += (U32 1000) + (U32 $Media) + (U32 65536) }
    $edts = Box 'edts' (Box $EditType ([byte[]]($flags + (U32 $count) + $entries)))
    return ,(Box 'trak' ([byte[]]($edts + $mdia)))
}
function Fixture([int]$Version, [long]$Media, [bool]$AudioEmpty, [string]$EditType = 'elst') {
    $mvhd = Box 'mvhd' ([byte[]]([byte[]]::new(12) + (U32 1000)))
    $a = Track 'soun' $Version $Media $AudioEmpty $EditType
    $v = Track 'vide' $Version 0 $true
    return ,(Box 'moov' ([byte[]]($mvhd + $a + $v)) $true)
}
function MustReject([string]$Path) {
    $rejected = $false
    try { $null = Assert-Mp4AacPriming $Path } catch { $rejected = $true }
    if (-not $rejected) { throw "Incorrect edit list accepted: $Path" }
}
$dir = Join-Path ([IO.Path]::GetTempPath()) ('subrec-elst-parser-' + [Guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($dir)
foreach ($version in 0, 1) {
    $file = Join-Path $dir "v$version.mp4"
    [IO.File]::WriteAllBytes($file, (Fixture $version 1024 $false))
    Assert-Mp4AacPriming $file
    $tracks = @(Get-Mp4EditLists $file)
    if ($tracks[1].Entries[0].MediaTime -ne -1 -or $tracks[1].StartSeconds -ne 0.1) { throw 'Signed empty edit parsing failed' }
    [IO.File]::WriteAllBytes($file, (Fixture $version 0 $false))
    MustReject $file # original clamped timestamps
    [IO.File]::WriteAllBytes($file, (Fixture $version 1024 $true))
    MustReject $file # global make_zero shift disguised as leading audio silence
    [IO.File]::WriteAllBytes($file, (Fixture $version 1024 $false 'xxxx'))
    MustReject $file
    [IO.File]::WriteAllBytes($file, [byte[]](0,0,0,16,109,111,111,118))
    MustReject $file
}
Write-Output "PASS: MP4 edit-list parser (real muxer outputs, v0/v1, invalid and truncated boxes); fixtures=$dir"
