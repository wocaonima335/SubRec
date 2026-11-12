param(
    [Parameter(Mandatory)][string]$Setup,
    [string]$Payload = 'dist/SubRec'
)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:RUNNER_TEMP 'SubRec install 中文'
$logs = Join-Path $env:RUNNER_TEMP 'SubRec-installer-logs'
New-Item -ItemType Directory -Force $logs | Out-Null
if (Test-Path $dir) { throw "Smoke test requires a fresh directory: $dir" }
$payloadRoot = (Resolve-Path $Payload).Path
$setupPath = (Resolve-Path $Setup).Path

function Install-Payload([string]$Name) {
    $log = Join-Path $logs "$Name.log"
    $p = Start-Process -FilePath $setupPath -Wait -PassThru -ArgumentList @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
        '/TASKS=desktopicon', "/DIR=`"$dir`"", "/LOG=`"$log`""
    )
    if ($p.ExitCode -ne 0) { throw "Setup failed: $($p.ExitCode); $log" }
    foreach ($file in Get-ChildItem $payloadRoot -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($payloadRoot, $file.FullName)
        $installed = Join-Path $dir $relative
        if (-not (Test-Path $installed -PathType Leaf)) { throw "Not installed: $relative" }
        if ((Get-FileHash $file.FullName).Hash -ne (Get-FileHash $installed).Hash) {
            throw "Installed content differs: $relative"
        }
    }
    foreach ($file in 'NOTICE.txt', 'SubRec.ico', 'unins000.exe', 'unins000.dat') {
        if (-not (Test-Path (Join-Path $dir $file))) { throw "Missing: $file" }
    }
}

Install-Payload 'install'
$want = @(Get-ChildItem $payloadRoot -File -Recurse).Count + 4
if (@(Get-ChildItem $dir -File -Recurse).Count -ne $want) { throw 'Unexpected installed file count' }
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{04DE0717-B7D3-492D-84C6-73C1E49B54F8}_is1'
if (-not (Test-Path $uninstallKey)) { throw 'Per-user uninstall registration missing' }
$shortcuts = @(
    (Join-Path ([Environment]::GetFolderPath('Programs')) '字录 SubRec.lnk'),
    (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) '字录 SubRec.lnk')
)
foreach ($shortcut in $shortcuts) {
    if (-not (Test-Path $shortcut)) { throw "Shortcut missing: $shortcut" }
}

# Exercise cleanup and log preservation without starting a recorder on the runner.
Set-Content (Join-Path $dir 'obsolete-test.dll') 'old DLL'
New-Item -ItemType Directory -Force (Join-Path $dir 'qml/obsolete-test') | Out-Null
Set-Content (Join-Path $dir 'qml/obsolete-test/qmldir') 'old module'
Set-Content (Join-Path $dir 'SubRec.log') 'keep this log during upgrade'
Install-Payload 'upgrade'
foreach ($stale in 'obsolete-test.dll', 'qml/obsolete-test') {
    if (Test-Path (Join-Path $dir $stale)) { throw "Upgrade left $stale" }
}
if ((Get-Content (Join-Path $dir 'SubRec.log') -Raw).Trim() -ne 'keep this log during upgrade') {
    throw 'Upgrade did not preserve SubRec.log'
}

# Inno's uninstaller respawns from a temporary copy; wait for its effects as well.
$uninstall = Start-Process (Join-Path $dir 'unins000.exe') -Wait -PassThru -ArgumentList @(
    '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$logs/uninstall.log`""
)
if ($uninstall.ExitCode -ne 0) { throw "Uninstall failed: $($uninstall.ExitCode)" }
for ($i = 0; $i -lt 60 -and (Test-Path $dir); $i++) { Start-Sleep -Seconds 1 }
if (Test-Path $dir) { throw "Uninstall left files in $dir" }
if (Test-Path $uninstallKey) { throw 'Uninstall registration remains' }
foreach ($shortcut in $shortcuts) {
    if (Test-Path $shortcut) { throw "Uninstall left shortcut: $shortcut" }
}
