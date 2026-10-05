# Synthetic checks for Test-SupermanHud (tools\bench\bench_hud.ps1).
# Run: powershell -NoProfile -File tests\tools\test_bench_hud.ps1
Add-Type -AssemblyName System.Drawing
. "$PSScriptRoot\..\..\tools\bench\bench_hud.ps1"

# 1000x600 bitmap: the scanned region is the top-left 500x180.
function New-Frame {
  $bmp = New-Object System.Drawing.Bitmap 1000, 600
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  try { $gfx.Clear([System.Drawing.Color]::FromArgb(25, 25, 30)) } finally { $gfx.Dispose() }
  return ,$bmp
}

function Fill($bmp, $x, $y, $w, $h, $r, $g, $b) {
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  try {
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($r, $g, $b))
    try { $gfx.FillRectangle($brush, $x, $y, $w, $h) } finally { $brush.Dispose() }
  } finally { $gfx.Dispose() }
}

function BlueBar($bmp, $y, $h = 6, $w = 420) { Fill $bmp 20 $y $w $h 90 140 230 }
function RedBar($bmp, $y, $h = 4, $w = 200) { Fill $bmp 20 $y $w $h 200 40 50 }

$failures = 0
function Check($name, [bool]$expected, $bmp) {
  try { $actual = Test-SupermanHud $bmp } catch { $actual = "error: $_" }
  finally { $bmp.Dispose() }
  if ($actual -is [bool] -and $actual -eq $expected) { Write-Host "PASS $name (=$actual)" }
  else { Write-Host "FAIL $name (expected $expected, got $actual)"; $script:failures++ }
}

# (a) gameplay-like HUD: thin blue bar with a thin red bar 20 px below
$f = New-Frame; BlueBar $f 20; RedBar $f 46
Check 'a gameplay HUD: thin blue + thin red bars' $true $f

# (b) menu-like: large filled blue panel and large red blob
$f = New-Frame; Fill $f 20 20 300 200 90 140 230; Fill $f 330 20 150 150 200 40 50
Check 'b menu: big blue panel + big red blob' $false $f

# (c) only the thin blue bar
$f = New-Frame; BlueBar $f 20
Check 'c thin blue bar only' $false $f

# (d) only the thin red bar
$f = New-Frame; RedBar $f 20
Check 'd thin red bar only' $false $f

# (e) thin bars 150 px apart
$f = New-Frame; BlueBar $f 10; RedBar $f 160
Check 'e thin bars 150 px apart' $false $f

if ($failures) { Write-Host "$failures case(s) FAILED"; exit 1 }
Write-Host 'All cases passed'
exit 0
