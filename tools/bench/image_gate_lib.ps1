# Gate de imagem do bench (tools\bench): compara dois screenshots por PSNR e pela distância entre os histogramas
# de cor. Pega corrupção grosseira (canais trocados, textura preta ou embaralhada), não diferenças de poucos pixels.
# Dot-source: . "$PSScriptRoot\image_gate_lib.ps1"
Add-Type -AssemblyName System.Drawing

# Limites calibrados com a variação natural entre execuções da build boa (veja tools\README.md).
$script:GateMinPsnr = 30.0       # dB; abaixo disso a imagem é considerada diferente demais
$script:GateMaxHistogram = 0.05  # 0 = histogramas iguais, 1 = disjuntos

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

# Compares a candidate screenshot with the reference. Returns Pass, Psnr, Histogram and Reason.
function Compare-GateImages {
  param(
    [Parameter(Mandatory)] [string]$Reference,
    [Parameter(Mandatory)] [string]$Candidate,
    [double]$MinPsnr = $script:GateMinPsnr,
    [double]$MaxHistogram = $script:GateMaxHistogram
  )
  $a = Read-GateBitmap $Reference
  $b = Read-GateBitmap $Candidate
  try {
    if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
      return [pscustomobject]@{ Pass = $false; Psnr = 0.0; Histogram = 1.0
        Reason = "different size ($($a.Width)x$($a.Height) vs $($b.Width)x$($b.Height)); record the reference again if the window size changed" }
    }
    $m = [SrImageGate]::Compare($a, $b)
    $reasons = @()
    if ($m[0] -lt $MinPsnr) { $reasons += ('PSNR {0:N1} dB < {1:N1} dB' -f $m[0], $MinPsnr) }
    if ($m[1] -gt $MaxHistogram) { $reasons += ('histogram distance {0:N3} > {1:N3}' -f $m[1], $MaxHistogram) }
    return [pscustomobject]@{ Pass = ($reasons.Count -eq 0); Psnr = $m[0]; Histogram = $m[1]; Reason = ($reasons -join '; ') }
  } finally { $a.Dispose(); $b.Dispose() }
}
