# Superman HUD detection for tools\bench.ps1 (functions only, dot-source it).

# True when the Superman gameplay HUD is in the bitmap: a thin blue bar and a thin
# red bar, top left of the game image, with their vertical centers within 40 px.
#
# The bars must be THIN (<= 14 px tall). The menus also contain blue and red, but as
# large filled regions: the blue panel of the "Load a game?" dialog and the red
# Superman logo of the title screen are tens of pixels tall. Matching any blue row
# next to any red row took those menus for gameplay; requiring a thin run rejects them.
#
# Scan: top-left 50% width x 30% height, every 2nd row and 4th column. A row is a
# "blue row" with >= 20 blue samples and a "red row" with >= 8 red samples. Runs of
# consecutive scanned rows of the same kind are bars when they are <= 7 rows tall.
function Test-SupermanHud([System.Drawing.Bitmap]$Bitmap) {
  $w = [int]($Bitmap.Width * .5)
  $h = [int]($Bitmap.Height * .3)
  $maxBarRows = 7   # 7 scanned rows at step 2 = 14 px
  $blueRows = New-Object System.Collections.Generic.List[int]
  $redRows = New-Object System.Collections.Generic.List[int]
  for ($y = 0; $y -lt $h; $y += 2) {
    $blue = 0; $red = 0
    for ($x = 0; $x -lt $w; $x += 4) {
      $pixel = $Bitmap.GetPixel($x, $y)
      if ($pixel.B - $pixel.R -gt 12 -and $pixel.B - $pixel.G -gt 5 -and $pixel.R -gt 80) { $blue++ }
      elseif ($pixel.R - $pixel.B -gt 20 -and $pixel.R - $pixel.G -gt 15 -and $pixel.R -gt 80) { $red++ }
    }
    if ($blue -ge 20) { $blueRows.Add($y) }
    if ($red -ge 8) { $redRows.Add($y) }
  }
  $blueBars = @(Get-HudBarCenters $blueRows $maxBarRows)
  $redBars = @(Get-HudBarCenters $redRows $maxBarRows)
  foreach ($blueCenter in $blueBars) {
    foreach ($redCenter in $redBars) { if ([math]::Abs($blueCenter - $redCenter) -le 40) { return $true } }
  }
  return $false
}

# Vertical centers of the runs of consecutive scanned rows (step 2) that are at most
# $MaxRows rows tall. Longer runs are filled regions, not bars.
function Get-HudBarCenters($Rows, [int]$MaxRows) {
  $start = $null; $prev = $null; $count = 0
  $flush = {
    if ($null -ne $start -and $count -le $MaxRows) { ($start + $prev) / 2 }
  }
  foreach ($y in $Rows) {
    if ($null -ne $prev -and $y -eq $prev + 2) { $count++ }
    else {
      & $flush
      $start = $y; $count = 1
    }
    $prev = $y
  }
  & $flush
}
