param(
    [int]$ColorIntervalMs = 900,
    [int]$DurationMs = 70000
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$colors = @(
    [System.Drawing.Color]::Red,
    [System.Drawing.Color]::Lime,
    [System.Drawing.Color]::Blue,
    [System.Drawing.Color]::Yellow,
    [System.Drawing.Color]::Magenta,
    [System.Drawing.Color]::Cyan
)

$bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$script:colorIndex = 0

$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
$form.Location = New-Object System.Drawing.Point($bounds.Left, $bounds.Top)
$form.Size = New-Object System.Drawing.Size($bounds.Width, $bounds.Height)
$form.TopMost = $true
$form.BackColor = $colors[$script:colorIndex]
$form.ShowInTaskbar = $false
$form.Text = "SubRecPhase4Overlay"

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = $ColorIntervalMs
$timer.Add_Tick({
    $script:colorIndex = ($script:colorIndex + 1) % $colors.Count
    $form.BackColor = $colors[$script:colorIndex]
})

$closeTimer = New-Object System.Windows.Forms.Timer
$closeTimer.Interval = $DurationMs
$closeTimer.Add_Tick({
    $closeTimer.Stop()
    $form.Close()
})

$timer.Start()
$closeTimer.Start()
[void]$form.ShowDialog()
