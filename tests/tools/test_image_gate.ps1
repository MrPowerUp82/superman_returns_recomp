# Synthetic checks for the image gate (tools\bench\image_gate_lib.ps1).
# Run: powershell -NoProfile -File tests\tools\test_image_gate.ps1
Add-Type -AssemblyName System.Drawing
. "$PSScriptRoot\..\..\tools\bench\image_gate_lib.ps1"

# The synthetic checks pass explicit global limits and no regions: they test the metrics, not the calibrated defaults.
$limits = @{ MinPsnr = 18.0; MaxHistogram = 0.12; Regions = @() }
# Region checks on the synthetic scene use their own region: the orange block, with explicit limits.
$blockRegion = @([pscustomobject]@{ Name = 'block'; X0 = 0.22; Y0 = 0.25; X1 = 0.45; Y1 = 0.52; MinPsnr = $null; MaxMeanDiff = 20.0; MaxChroma = 0.05 })
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$goldenPath = Join-Path $repoRoot 'artifacts\golden\start.png'
$cameraShot = Join-Path $repoRoot 'logs\bench_c3_t3_start.png'

# Pixel transforms for the checks on the real screenshots (BGRA byte arrays).
if (-not ([System.Management.Automation.PSTypeName]'SrGateTestImage').Type) {
  Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class SrGateTestImage {
  public static byte[] Load(string path, out int w, out int h) {
    using (Bitmap b = new Bitmap(path)) {
      w = b.Width; h = b.Height;
      BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
      byte[] p = new byte[w * h * 4]; Marshal.Copy(d.Scan0, p, 0, p.Length); b.UnlockBits(d); return p;
    }
  }
  public static void Save(byte[] p, int w, int h, string path) {
    using (Bitmap b = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
      BitmapData d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
      Marshal.Copy(p, 0, d.Scan0, p.Length); b.UnlockBits(d); b.Save(path, ImageFormat.Png);
    }
  }
  // Output channel i takes input channel perm[i] (0 = R, 1 = G, 2 = B).
  public static byte[] Permute(byte[] s, int[] perm) {
    byte[] o = (byte[])s.Clone();
    for (int i = 0; i < s.Length; i += 4) { int[] v = { s[i + 2], s[i + 1], s[i] }; o[i + 2] = (byte)v[perm[0]]; o[i + 1] = (byte)v[perm[1]]; o[i] = (byte)v[perm[2]]; }
    return o;
  }
  // Blacks out a rectangle given as fractions; 0,0,1,1 is a black frame.
  public static byte[] BlackRect(byte[] s, int w, int h, double x0, double y0, double x1, double y1) {
    byte[] o = (byte[])s.Clone();
    for (int y = (int)(y0 * h); y < (int)(y1 * h); y++) for (int x = (int)(x0 * w); x < (int)(x1 * w); x++) { int i = (y * w + x) * 4; o[i] = 0; o[i + 1] = 0; o[i + 2] = 0; }
    return o;
  }
  // Rotates the hue (YIQ rotation) by the given degrees.
  public static byte[] Hue(byte[] s, double deg) {
    byte[] o = (byte[])s.Clone(); double a = deg * Math.PI / 180, c = Math.Cos(a), sn = Math.Sin(a);
    for (int i = 0; i < s.Length; i += 4) {
      double r = s[i + 2], g = s[i + 1], b = s[i];
      double y = 0.299 * r + 0.587 * g + 0.114 * b, I = 0.596 * r - 0.274 * g - 0.322 * b, Q = 0.211 * r - 0.523 * g + 0.312 * b;
      double I2 = I * c - Q * sn, Q2 = I * sn + Q * c;
      o[i + 2] = (byte)Math.Max(0, Math.Min(255, y + 0.956 * I2 + 0.621 * Q2));
      o[i + 1] = (byte)Math.Max(0, Math.Min(255, y - 0.272 * I2 - 0.647 * Q2));
      o[i] = (byte)Math.Max(0, Math.Min(255, y - 1.106 * I2 + 1.703 * Q2));
    }
    return o;
  }
}
"@
}

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
  if ($result.Pass -and @($result.Regions).Count) { $detail += ' ' + (Format-GateRegions $result) }
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

  # (h) the calibrated defaults reject the synthetic black frame (global and region limits)
  Check 'h default limits reject a black frame' $false (Compare-GateImages $reference (Join-Path $dir 'e.png'))

  # (i) a region catches what the loose global limits let through: only the orange block turns blue-ish
  $blockSwap = Convert-Scene $scene { param($x, $y, $c) if ($c.R -eq 240 -and $c.G -eq 140) { [System.Drawing.Color]::FromArgb(20, 140, 240) } else { $c } }
  $loose = @{ MinPsnr = 5.0; MaxHistogram = 0.9; Regions = $blockRegion }
  Check 'i block region: recolored block fails' $false (Compare-GateImages $reference (Save $blockSwap 'i.png') @loose) 'region block'
  # (j) the same region tolerates noise and a small shift
  Check 'j block region: noise passes' $true (Compare-GateImages $reference (Join-Path $dir 'b.png') @loose)
  Check 'k block region: 4 px shift passes' $true (Compare-GateImages $reference (Join-Path $dir 'c.png') @loose)
  # (l) background changes (camera heading) outside the region do not matter
  $bgChange = Convert-Scene $scene { param($x, $y, $c) if ($y -ge 110) { [System.Drawing.Color]::FromArgb(60, 60, 60) } else { $c } }
  Check 'l block region: background change passes' $true (Compare-GateImages $reference (Save $bgChange 'l.png') @loose)
  $scene.Dispose()

  # (m) CSV: new file gets a header; an old file without one keeps its rows and gets the header on top
  $csvNew = Join-Path $dir 'new.csv'
  $csvOld = Join-Path $dir 'old.csv'
  $res = Compare-GateImages $reference (Join-Path $dir 'b.png')
  Add-GateCsvRow $csvNew 'x' 'PASS' $res
  Set-Content $csvOld '2026-10-06T09:09:37,c3_base,36.79,0.0081,PASS' -Encoding UTF8
  Add-GateCsvRow $csvOld 'y' 'PASS' $res
  $new = @(Get-Content $csvNew); $old = @(Get-Content $csvOld)
  $cols = (Get-GateCsvHeader).Split(',').Count
  $okCsv = ($new.Count -eq 2) -and $new[0].StartsWith('time,name,psnr,hist,verdict,') -and ($new[1].Split(',').Count -eq $cols) -and
    ($old.Count -eq 3) -and ($old[0] -eq $new[0]) -and ($old[1] -eq '2026-10-06T09:09:37,c3_base,36.79,0.0081,PASS') -and ($old[2].Split(',').Count -eq $cols)
  if ($okCsv) { Write-Host 'PASS m csv header and old rows' } else { Write-Host 'FAIL m csv header and old rows'; $old | ForEach-Object { Write-Host "   $_" }; $failures++ }

  # (n..) real screenshots, with the calibrated defaults. Skipped when the local reference is absent.
  if (-not (Test-Path -LiteralPath $goldenPath)) {
    Write-Host "SKIP real screenshot checks: no reference at $goldenPath (run bench_api.ps1 -Gate record on a good build)"
  } else {
    $w = 0; $h = 0
    $px = [SrGateTestImage]::Load($goldenPath, [ref]$w, [ref]$h)
    Check 'n golden against itself passes' $true (Compare-GateImages $goldenPath $goldenPath)
    $perms = [ordered]@{ rbg = @(0, 2, 1); grb = @(1, 0, 2); gbr = @(1, 2, 0); brg = @(2, 0, 1); bgr = @(2, 1, 0) }
    foreach ($k in $perms.Keys) {
      $p = Join-Path $dir "g_perm_$k.png"; [SrGateTestImage]::Save([SrGateTestImage]::Permute($px, $perms[$k]), $w, $h, $p)
      Check "o golden with channels permuted ($k) fails" $false (Compare-GateImages $goldenPath $p)
    }
    $negatives = [ordered]@{
      'black frame'        = [SrGateTestImage]::BlackRect($px, $w, $h, 0, 0, 1, 1)
      'character blacked out' = [SrGateTestImage]::BlackRect($px, $w, $h, 0.58, 0.6, 0.72, 1.0)
      'HUD blacked out'    = [SrGateTestImage]::BlackRect($px, $w, $h, 0.09, 0.1, 0.5, 0.3)
      'hue shifted by 40 degrees' = [SrGateTestImage]::Hue($px, 40)
    }
    $i = 0
    foreach ($k in $negatives.Keys) {
      $p = Join-Path $dir "g_neg_$i.png"; $i++; [SrGateTestImage]::Save($negatives[$k], $w, $h, $p)
      Check "p golden: $k fails" $false (Compare-GateImages $goldenPath $p)
    }
    if (-not (Test-Path -LiteralPath $cameraShot)) {
      Write-Host "SKIP camera variation checks: $cameraShot is absent"
    } else {
      # a good screenshot with another camera heading: the case that failed the global-only gate
      Check 'q camera variation (bench_c3_t3) passes' $true (Compare-GateImages $goldenPath $cameraShot)
      $w2 = 0; $h2 = 0
      $px2 = [SrGateTestImage]::Load($cameraShot, [ref]$w2, [ref]$h2)
      $p = Join-Path $dir 't3_perm.png'; [SrGateTestImage]::Save([SrGateTestImage]::Permute($px2, @(2, 1, 0)), $w2, $h2, $p)
      Check 'r camera variation with channels permuted fails' $false (Compare-GateImages $goldenPath $p)
      $p = Join-Path $dir 't3_nochar.png'; [SrGateTestImage]::Save([SrGateTestImage]::BlackRect($px2, $w2, $h2, 0.58, 0.6, 0.72, 1.0), $w2, $h2, $p)
      Check 'r camera variation with character blacked out fails' $false (Compare-GateImages $goldenPath $p)
    }
    # the other good screenshots that exist locally must pass too
    foreach ($f in (Get-ChildItem (Join-Path $repoRoot 'logs') -Filter 'bench_c3_*_start.png' -ErrorAction SilentlyContinue)) {
      if ($f.FullName -eq $cameraShot) { continue }
      Check "s good screenshot $($f.Name) passes" $true (Compare-GateImages $goldenPath $f.FullName)
    }
  }
} finally {
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
}

if ($failures) { Write-Host "$failures check(s) failed"; exit 1 }
Write-Host 'all image gate checks passed'
