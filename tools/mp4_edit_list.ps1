# Read MP4 timing without ffprobe. Seek over mdat, including files larger than 4 GiB.
# Dot-source this helper; it has no recording/build side effects.
function Read-Mp4Number($Reader, [long]$Offset, [int]$Width, [bool]$Signed = $false) {
    if ($Width -ne 4 -and $Width -ne 8) { throw 'Invalid MP4 integer width' }
    if ($Offset -lt 0 -or $Offset -gt $Reader.BaseStream.Length - $Width) { throw 'Truncated MP4 integer' }
    $null = $Reader.BaseStream.Seek($Offset, [System.IO.SeekOrigin]::Begin)
    $bytes = $Reader.ReadBytes($Width)
    if ($bytes.Length -ne $Width) { throw 'Truncated MP4 integer' }
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($bytes) }
    if ($Width -eq 4) {
        if ($Signed) { return [BitConverter]::ToInt32($bytes, 0) }
        return [BitConverter]::ToUInt32($bytes, 0)
    }
    if ($Signed) { return [BitConverter]::ToInt64($bytes, 0) }
    return [BitConverter]::ToUInt64($bytes, 0)
}
function Read-Mp4Type($Reader, [long]$Offset) {
    $null = $Reader.BaseStream.Seek($Offset, [System.IO.SeekOrigin]::Begin)
    $bytes = $Reader.ReadBytes(4)
    if ($bytes.Length -ne 4) { throw 'Truncated MP4 type' }
    return [Text.Encoding]::ASCII.GetString($bytes)
}
function Get-Mp4Boxes($Reader, [long]$Begin, [long]$End) {
    if ($Begin -lt 0 -or $End -gt $Reader.BaseStream.Length -or $End -lt $Begin) { throw 'Invalid MP4 bounds' }
    while ($Begin -lt $End) {
        if ($End - $Begin -lt 8) { throw 'Truncated MP4 box' }
        [ulong]$size = Read-Mp4Number $Reader $Begin 4
        $type = Read-Mp4Type $Reader ($Begin + 4)
        [long]$header = 8
        if ($size -eq 1) {
            if ($End - $Begin -lt 16) { throw 'Truncated extended MP4 box' }
            $size = Read-Mp4Number $Reader ($Begin + 8) 8
            $header = 16
        }
        if ($size -eq 0) { $size = $End - $Begin }
        if ($size -lt $header -or $size -gt [ulong]($End - $Begin)) { throw "Invalid MP4 box size: $type" }
        [pscustomobject]@{ Type = $type; Data = $Begin + $header; End = $Begin + [long]$size }
        $Begin += [long]$size
    }
}
function Get-Mp4Child($Reader, $Parent, [string]$Type) {
    $found = @(Get-Mp4Boxes $Reader $Parent.Data $Parent.End | Where-Object { $_.Type -eq $Type })
    if ($found.Count -ne 1) { throw "Expected one MP4 $Type box, found $($found.Count)" }
    return $found[0]
}
function Get-Mp4Timescale($Reader, $Box) {
    if ($Box.End - $Box.Data -lt 4) { throw 'Truncated MP4 timescale box' }
    $version = (Read-Mp4Number $Reader $Box.Data 4) -shr 24
    if ($version -gt 1) { throw 'Unsupported MP4 timescale version' }
    $offset = if ($version -eq 1) { 20 } else { 12 }
    if ($Box.End - $Box.Data -lt $offset + 4) { throw 'Truncated MP4 timescale' }
    $scale = Read-Mp4Number $Reader ($Box.Data + $offset) 4
    if ($scale -eq 0) { throw 'Zero MP4 timescale' }
    return $scale
}
function Get-Mp4EditLists([string]$Path) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        $root = [pscustomobject]@{ Data = 0; End = $reader.BaseStream.Length }
        $moov = Get-Mp4Child $reader $root 'moov'
        $movieScale = Get-Mp4Timescale $reader (Get-Mp4Child $reader $moov 'mvhd')
        foreach ($track in @(Get-Mp4Boxes $reader $moov.Data $moov.End | Where-Object { $_.Type -eq 'trak' })) {
            $mdia = Get-Mp4Child $reader $track 'mdia'
            $hdlr = Get-Mp4Child $reader $mdia 'hdlr'
            if ($hdlr.End - $hdlr.Data -lt 12) { throw 'Truncated MP4 handler' }
            $handler = Read-Mp4Type $reader ($hdlr.Data + 8)
            if ($handler -ne 'soun' -and $handler -ne 'vide') { continue }
            $scale = Get-Mp4Timescale $reader (Get-Mp4Child $reader $mdia 'mdhd')
            $edts = Get-Mp4Child $reader $track 'edts'
            $elst = Get-Mp4Child $reader $edts 'elst'
            if ($elst.End - $elst.Data -lt 8) { throw 'Truncated elst header' }
            $version = (Read-Mp4Number $reader $elst.Data 4) -shr 24
            if ($version -gt 1) { throw 'Unsupported elst version' }
            $width = if ($version -eq 1) { 8 } else { 4 }
            $stride = 2 * $width + 4
            $count = Read-Mp4Number $reader ($elst.Data + 4) 4
            if ($count -eq 0 -or [long]$count * $stride -ne $elst.End - $elst.Data - 8) { throw 'Invalid elst entry count' }
            $entries = @(
                for ($i = 0; $i -lt $count; $i++) {
                    $offset = $elst.Data + 8 + [long]$i * $stride
                    $duration = Read-Mp4Number $reader $offset $width
                    $mediaTime = Read-Mp4Number $reader ($offset + $width) $width $true
                    $rate = Read-Mp4Number $reader ($offset + 2 * $width) 4
                    if ($rate -ne 65536) { throw 'Non-unit elst playback rate' }
                    [pscustomobject]@{ Duration = $duration; MediaTime = $mediaTime }
                }
            )
            # Presentation start is the duration of leading empty edits in movie units.
            [double]$start = 0
            foreach ($entry in $entries) {
                if ($entry.MediaTime -ne -1) { break }
                $start += $entry.Duration / [double]$movieScale
            }
            [pscustomobject]@{ Handler = $handler; Timescale = $scale; MovieTimescale = $movieScale
                Version = $version; Entries = $entries; StartSeconds = $start }
        }
    } finally { $reader.Dispose() }
}
function Assert-Mp4AacPriming([string]$Path) {
    $tracks = @(Get-Mp4EditLists $Path)
    $audio = @($tracks | Where-Object { $_.Handler -eq 'soun' })
    $video = @($tracks | Where-Object { $_.Handler -eq 'vide' })
    if ($audio.Count -ne 1 -or $video.Count -ne 1) { throw 'Expected one audio and one video track' }
    $a = $audio[0]
    if ($a.Entries.Count -ne 1 -or $a.Entries[0].MediaTime -ne 1024) {
        throw "AAC elst must trim media_time=1024 with no leading empty edit; got $($a.Entries | ConvertTo-Json -Compress)"
    }
    if ($a.StartSeconds -ne 0) { throw "AAC presentation start is $($a.StartSeconds), expected 0" }
    # Video may legitimately start later. Report its offset instead of forcing it to zero.
    return "audio elst media_time=1024 timescale=$($a.Timescale) start=0s; video start=$($video[0].StartSeconds)s"
}
