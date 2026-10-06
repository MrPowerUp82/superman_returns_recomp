# Synthetic checks for the image gate (tools\bench\image_gate_lib.ps1).
# Run: powershell -NoProfile -File tests\tools\test_image_gate.ps1
Add-Type -AssemblyName System.Drawing
. "$PSScriptRoot\..\..\tools\bench\image_gate_lib.ps1"

# The checks pass explicit limits: they test the metrics, not the calibrated defaults.
$limits = @{ MinPsnr = 18.0; MaxHistogram = 0.12 }

$dir = Join-Path ([IO.Path]::GetTempPath()) ("sr-gate-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $dir | Out-Null

# 320x180 scene with different color statistics per channel, so that swapped channels are visible:
# blue sky gradient, brown ground, an orange block and a small red block.
function New-Scene([int]$Width = 320, [int]$Height = 180) {
  $bmp = New-Object System.Drawing.Bitmap $Width, $Height
  for ($y = 0; $y -lt $Height; $y++) {
    for ($x = 0; $x -lt $Width; $x++) {
      if ($y -lt 110) { $c = [System.Drawing.Color]::FromArgb(40 + [int]($y * 0.5), 90 + [int]($y * 0.8), 200 + [int]($y * 0.4)) }
      else { $c = [System.Drawing.Color]::FromArgb(120 + (($x * 3) % 20), 80 + (($x * 2) % 15), 40) }
      $bmp.SetPixel($x, $y, $c)
    }
  }
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  try {
    $orange = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(240, 140, 20))
    $red = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200, 30, 40))
    $gfx.FillRectangle($orange, 60, 40, 90, 60); $gfx.FillRectangle($red, 200, 120, 40, 30)
    $orange.Dispose(); $red.Dispose()
  } finally { $gfx.Dispose() }
  return ,$bmp
}

# Applies a per-pixel transform scriptblock { param($x,$y,$c) -> Color } to a copy of the bitmap.
function Convert-Scene($src, [scriptblock]$fn) {
  $out = New-Object System.Drawing.Bitmap $src.Width, $src.Height
  for ($y = 0; $y -lt $src.Height; $y++) { for ($x = 0; $x -lt $src.Width; $x++) { $out.SetPixel($x, $y, (& $fn $x $y $src.GetPixel($x, $y))) } }
  return ,$out
}

function Save($bmp, $name) { $path = Join-Path $dir $name; $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); return $path }
function Clamp([int]$v) { [math]::Max(0, [math]::Min(255, $v)) }

$failures = 0
function Check($name, [bool]$expectPass, $result, $reasonLike = '') {
  $ok = ($result.Pass -eq $expectPass) -and ($reasonLike -eq '' -or $result.Reason -like "*$reasonLike*")
  $detail = 'psnr={0:N1} hist={1:N3} {2}' -f $result.Psnr, $result.Histogram, $result.Reason
  if ($ok) { Write-Host "PASS $name ($detail)" } else { Write-Host "FAIL $name (expected pass=$expectPass; $detail)"; $script:failures++ }
}

try {
  $scene = New-Scene
  $reference = Save $scene.Clone() 'reference.png'

  # (a) identical image
  Check 'a identical image passes' $true (Compare-GateImages $reference (Save $scene.Clone() 'a.png') @limits)

  # (b) small noise, like the variation between two runs
  $noisy = Convert-Scene $scene { param($x, $y, $c) $n = (($x * 7 + $y * 13) % 7) - 3; [System.Drawing.Color]::FromArgb((Clamp ($c.R + $n)), (Clamp ($c.G - $n)), (Clamp ($c.B + $n))) }
  Check 'b noise of +-3 passes' $true (Compare-GateImages $reference (Save $noisy 'b.png') @limits)

  # (c) scene shifted 4 px to the right: a small camera difference
  $shifted = Convert-Scene $scene { param($x, $y, $c) $sx = [math]::Max(0, $x - 4); $scene.GetPixel($sx, $y) }
  Check 'c 4 px shift passes' $true (Compare-GateImages $reference (Save $shifted 'c.png') @limits)

  # (d) RGB channels permuted: reproduces the cycle 2 corruption
  $permuted = Convert-Scene $scene { param($x, $y, $c) [System.Drawing.Color]::FromArgb($c.B, $c.R, $c.G) }
  Check 'd permuted channels fail' $false (Compare-GateImages $reference (Save $permuted 'd.png') @limits)

  # (e) black frame
  $black = New-Object System.Drawing.Bitmap 320, 180
  $gfx = [System.Drawing.Graphics]::FromImage($black); try { $gfx.Clear([System.Drawing.Color]::Black) } finally { $gfx.Dispose() }
  Check 'e black frame fails' $false (Compare-GateImages $reference (Save $black 'e.png') @limits)

  # (f) different size
  $small = New-Scene 160 90
  Check 'f different size fails' $false (Compare-GateImages $reference (Save $small 'f.png') @limits) 'different size'

  # (g) the files are not left locked after comparing
  Remove-Item (Join-Path $dir 'a.png') -ErrorAction Stop
  Write-Host 'PASS g files are not left locked'

  # (h) the calibrated defaults are not looser than the synthetic corruption
  $defaults = Compare-GateImages $reference (Join-Path $dir 'd.png')
  if (-not $defaults.Pass) { Write-Host 'PASS h default limits reject permuted channels' }
  else { Write-Host 'FAIL h default limits accept permuted channels'; $failures++ }
  $scene.Dispose()
} finally {
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
}

if ($failures) { Write-Host "$failures check(s) failed"; exit 1 }
Write-Host 'all image gate checks passed'
