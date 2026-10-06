# Image gate of the bench (tools\bench): compares a screenshot with a reference in two layers.
#  1. Global PSNR + color-histogram distance, loose: only gross failures (black or garbled frame, other scene).
#  2. Fixed regions (HUD logo, HUD bars, character), given as fractions of the image so they do not depend on
#     the resolution. The camera heading of the level opening is not deterministic, so the background of the
#     scene changes between good runs, but the HUD and the character sit at the same screen position and keep
#     the same colors. Each region is checked by the difference of its mean color, by the difference of its
#     chromaticity (channel ratios: catches swapped RGB channels and hue shifts even in a dark scene) and,
#     for the pixel-exact HUD logo, by PSNR.
# Limits were calibrated on the good screenshots in logs\ against artifacts\golden\start.png and verified
# against negatives made from the golden (see tools\README.md and .superpowers\sdd\c3-task-3b-report.md).
# Dot-source: . "$PSScriptRoot\image_gate_lib.ps1"
Add-Type -AssemblyName System.Drawing

# Global limits: loose on purpose (the worst good screenshot, with another camera heading, has 20.3 dB / 0.169).
$script:GateMinPsnr = 17.0       # dB
$script:GateMaxHistogram = 0.40  # 0 = same histograms, 1 = disjoint

# Regions as fractions of the image (X0,Y0,X1,Y1), with the limits of each one. A limit of $null is not checked.
#   MinPsnr      dB, pixel-wise; only meaningful where the pixels do not depend on the camera (the logo)
#   MaxMeanDiff  largest absolute difference of the per-channel mean color, 0..255
#   MaxChroma    largest absolute difference of the mean chromaticity (R/(R+G+B), ...), 0..1
function New-GateRegion([string]$Name, [double[]]$Rect, $MinPsnr, $MaxMeanDiff, $MaxChroma) {
  [pscustomobject]@{ Name = $Name; X0 = $Rect[0]; Y0 = $Rect[1]; X1 = $Rect[2]; Y1 = $Rect[3]
    MinPsnr = $MinPsnr; MaxMeanDiff = $MaxMeanDiff; MaxChroma = $MaxChroma }
}
$script:GateRegions = @(
  # Superman emblem inside the HUD shield (opaque, so no background pixels), top left
  (New-GateRegion 'logo' @(0.128, 0.2278, 0.153, 0.2514) 30.0 8.0 0.03)
  # blue bar, thin strip along its length (top left)
  (New-GateRegion 'blue' @(0.19, 0.1944, 0.46, 0.2125) $null 45.0 0.07)
  # red bar, thin strip along its length (top left)
  (New-GateRegion 'red' @(0.18, 0.2292, 0.31, 0.2389) $null 45.0 0.07)
  # Superman's cape, bottom center-right
  (New-GateRegion 'char' @(0.605, 0.694, 0.676, 0.903) $null 20.0 0.05)
)

if (-not ([System.Management.Automation.PSTypeName]'SrImageGate').Type) {
  Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class SrImageGate {
  // Returns { psnr, histogram }. The bitmaps must have the same size.
  public static double[] Compare(Bitmap a, Bitmap b) {
    Rectangle rect = new Rectangle(0, 0, a.Width, a.Height);
    BitmapData da = a.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    BitmapData db = b.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    try {
      int n = a.Width * a.Height * 4;
      byte[] pa = new byte[n], pb = new byte[n];
      Marshal.Copy(da.Scan0, pa, 0, n);
      Marshal.Copy(db.Scan0, pb, 0, n);
      double sum = 0;
      long[,] ha = new long[3, 32], hb = new long[3, 32];
      for (int i = 0; i < n; i += 4) {
        for (int c = 0; c < 3; c++) {
          int va = pa[i + c], vb = pb[i + c], d = va - vb;
          sum += (double)d * d;
          ha[c, va >> 3]++; hb[c, vb >> 3]++;
        }
      }
      double pixels = (double)a.Width * a.Height;
      double mse = sum / (pixels * 3);
      double psnr = mse <= 0 ? 99 : Math.Min(99, 10 * Math.Log10(255.0 * 255.0 / mse));
      double distance = 0;
      for (int c = 0; c < 3; c++) {
        double l1 = 0;
        for (int k = 0; k < 32; k++) l1 += Math.Abs(ha[c, k] - hb[c, k]) / pixels;
        distance += l1 / 2;
      }
      return new double[] { psnr, distance / 3 };
    } finally { a.UnlockBits(da); b.UnlockBits(db); }
  }

  // Compares a rectangle given as fractions of the image. Returns { psnr, meanDiff, chroma }:
  //   meanDiff = largest |mean(b) - mean(a)| over R, G, B (0..255)
  //   chroma   = largest |chromaticity(b) - chromaticity(a)| over R, G, B, where chromaticity = channel mean / sum of the
  //              three means (1.0 when one of the two regions is pure black, so a black region always differs)
  public static double[] Region(Bitmap a, Bitmap b, double fx0, double fy0, double fx1, double fy1) {
    int x0 = (int)Math.Round(fx0 * a.Width), x1 = (int)Math.Round(fx1 * a.Width);
    int y0 = (int)Math.Round(fy0 * a.Height), y1 = (int)Math.Round(fy1 * a.Height);
    x0 = Math.Max(0, x0); y0 = Math.Max(0, y0); x1 = Math.Min(a.Width, x1); y1 = Math.Min(a.Height, y1);
    if (x1 <= x0 || y1 <= y0) throw new ArgumentException("empty region");
    Rectangle rect = new Rectangle(x0, y0, x1 - x0, y1 - y0);
    BitmapData da = a.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    BitmapData db = b.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    try {
      int rowBytes = rect.Width * 4;
      byte[] ra = new byte[rowBytes], rb = new byte[rowBytes];
      double sum = 0; double[] ma = new double[3], mb = new double[3];
      for (int y = 0; y < rect.Height; y++) {
        Marshal.Copy(IntPtr.Add(da.Scan0, y * da.Stride), ra, 0, rowBytes);
        Marshal.Copy(IntPtr.Add(db.Scan0, y * db.Stride), rb, 0, rowBytes);
        for (int i = 0; i < rowBytes; i += 4) {
          for (int c = 0; c < 3; c++) {
            int va = ra[i + c], vb = rb[i + c], d = va - vb;
            sum += (double)d * d; ma[c] += va; mb[c] += vb;
          }
        }
      }
      double pixels = (double)rect.Width * rect.Height;
      double mse = sum / (pixels * 3);
      double psnr = mse <= 0 ? 99 : Math.Min(99, 10 * Math.Log10(255.0 * 255.0 / mse));
      double meanDiff = 0, chroma = 0, sa = ma[0] + ma[1] + ma[2], sb = mb[0] + mb[1] + mb[2];
      for (int c = 0; c < 3; c++) meanDiff = Math.Max(meanDiff, Math.Abs(mb[c] - ma[c]) / pixels);
      if (sa <= 0 || sb <= 0) chroma = (sa <= 0 && sb <= 0) ? 0 : 1;
      else for (int c = 0; c < 3; c++) chroma = Math.Max(chroma, Math.Abs(mb[c] / sb - ma[c] / sa));
      return new double[] { psnr, meanDiff, chroma };
    } finally { a.UnlockBits(da); b.UnlockBits(db); }
  }
}
"@
}

# Loads a PNG without keeping the file locked.
function Read-GateBitmap([string]$Path) {
  $stream = [System.IO.File]::OpenRead($Path)
  try {
    $loaded = New-Object System.Drawing.Bitmap $stream
    try { return ,(New-Object System.Drawing.Bitmap $loaded) } finally { $loaded.Dispose() }
  } finally { $stream.Dispose() }
}

# Compares a candidate screenshot with the reference. Returns Pass, Psnr, Histogram, Reason and Regions (one
# object per region with Name, Psnr, MeanDiff, Chroma and Pass). Pass -Regions @() to check only the global metrics.
function Compare-GateImages {
  param(
    [Parameter(Mandatory)] [string]$Reference,
    [Parameter(Mandatory)] [string]$Candidate,
    [double]$MinPsnr = $script:GateMinPsnr,
    [double]$MaxHistogram = $script:GateMaxHistogram,
    [object[]]$Regions = $script:GateRegions
  )
  $a = Read-GateBitmap $Reference
  $b = Read-GateBitmap $Candidate
  try {
    if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
      return [pscustomobject]@{ Pass = $false; Psnr = 0.0; Histogram = 1.0; Regions = @()
        Reason = "different size ($($a.Width)x$($a.Height) vs $($b.Width)x$($b.Height)); record the reference again if the window size changed" }
    }
    $m = [SrImageGate]::Compare($a, $b)
    $reasons = @()
    if ($m[0] -lt $MinPsnr) { $reasons += ('PSNR {0:N1} dB < {1:N1} dB' -f $m[0], $MinPsnr) }
    if ($m[1] -gt $MaxHistogram) { $reasons += ('histogram distance {0:N3} > {1:N3}' -f $m[1], $MaxHistogram) }
    $regionResults = @()
    foreach ($r in $Regions) {
      $v = [SrImageGate]::Region($a, $b, $r.X0, $r.Y0, $r.X1, $r.Y1)
      $why = @()
      if ($null -ne $r.MinPsnr -and $v[0] -lt $r.MinPsnr) { $why += ('PSNR {0:N1} dB < {1:N1} dB' -f $v[0], $r.MinPsnr) }
      if ($null -ne $r.MaxMeanDiff -and $v[1] -gt $r.MaxMeanDiff) { $why += ('mean color diff {0:N1} > {1:N1}' -f $v[1], $r.MaxMeanDiff) }
      if ($null -ne $r.MaxChroma -and $v[2] -gt $r.MaxChroma) { $why += ('chromaticity diff {0:N3} > {1:N3}' -f $v[2], $r.MaxChroma) }
      if ($why.Count) { $reasons += ("region $($r.Name): " + ($why -join ', ')) }
      $regionResults += [pscustomobject]@{ Name = $r.Name; Psnr = $v[0]; MeanDiff = $v[1]; Chroma = $v[2]; Pass = ($why.Count -eq 0) }
    }
    return [pscustomobject]@{ Pass = ($reasons.Count -eq 0); Psnr = $m[0]; Histogram = $m[1]; Regions = $regionResults; Reason = ($reasons -join '; ') }
  } finally { $a.Dispose(); $b.Dispose() }
}

# Formats the per-region values of a result as "name:psnr/mean/chroma ..." for the console line.
function Format-GateRegions($Result) {
  $inv = [cultureinfo]::InvariantCulture
  (@($Result.Regions) | ForEach-Object {
    '{0}[psnr={1} mean={2} chroma={3}]' -f $_.Name, $_.Psnr.ToString('0.0', $inv), $_.MeanDiff.ToString('0.0', $inv), $_.Chroma.ToString('0.000', $inv)
  }) -join ' '
}

# Columns of logs\bench_gate.csv. The first five are the ones the file had before the regions existed, so old rows
# still line up with the header; the per-region columns follow (name_psnr, name_mean, name_chroma for each region).
function Get-GateCsvHeader([object[]]$Regions = $script:GateRegions) {
  $cols = @('time', 'name', 'psnr', 'hist', 'verdict')
  foreach ($r in $Regions) { $cols += "$($r.Name)_psnr", "$($r.Name)_mean", "$($r.Name)_chroma" }
  $cols -join ','
}

# Appends one row to the CSV, writing the header first when the file is new or has no header (old rows are kept).
function Add-GateCsvRow([string]$Path, [string]$Name, [string]$Verdict, $Result, [object[]]$Regions = $script:GateRegions) {
  $inv = [cultureinfo]::InvariantCulture
  $header = Get-GateCsvHeader $Regions
  if (-not (Test-Path -LiteralPath $Path) -or (Get-Item -LiteralPath $Path).Length -eq 0) {
    Set-Content -LiteralPath $Path -Value $header -Encoding UTF8
  } else {
    $lines = @(Get-Content -LiteralPath $Path)
    if (-not $lines[0].StartsWith('time,')) { Set-Content -LiteralPath $Path -Value (@($header) + $lines) -Encoding UTF8 }
  }
  $row = @((Get-Date -Format s), $Name, $Result.Psnr.ToString('0.00', $inv), $Result.Histogram.ToString('0.0000', $inv), $Verdict)
  foreach ($r in $Regions) {
    $v = @($Result.Regions) | Where-Object { $_.Name -eq $r.Name } | Select-Object -First 1
    if ($v) { $row += $v.Psnr.ToString('0.00', $inv), $v.MeanDiff.ToString('0.00', $inv), $v.Chroma.ToString('0.0000', $inv) } else { $row += '', '', '' }
  }
  Add-Content -LiteralPath $Path -Value ($row -join ',') -Encoding UTF8
}
