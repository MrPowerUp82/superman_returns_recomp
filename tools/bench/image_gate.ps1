<#
Gate de imagem do bench: compara o screenshot do instante em que o HUD é detectado
(logs\bench_<Nome>_start.png, salvo pelo bench.ps1) com uma referência da build boa
(artifacts\golden\start.png, local e ignorada pelo Git).

  -Mode record   grava a referência a partir do screenshot (rode numa build que se sabe boa)
  -Mode check    compara; sai com erro se reprovar

Uso: tools\bench\image_gate.ps1 -Name <nome do bench> -Mode record|check
Normalmente chamado por: tools\bench\bench_api.ps1 -Gate record|check
#>
param(
  [Parameter(Mandatory)] [string]$Name,
  [Parameter(Mandatory)] [ValidateSet('record', 'check')] [string]$Mode
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. "$PSScriptRoot\image_gate_lib.ps1"
$shot = "$root\logs\bench_${Name}_start.png"
$golden = "$root\artifacts\golden\start.png"
if (-not (Test-Path -LiteralPath $shot)) { throw "Screenshot not found: $shot (the bench must reach gameplay first)" }

if ($Mode -eq 'record') {
  New-Item -ItemType Directory -Force (Split-Path $golden) | Out-Null
  Copy-Item -LiteralPath $shot -Destination $golden -Force
  Write-Output "GATE recorded $golden"
  return
}

if (-not (Test-Path -LiteralPath $golden)) { throw "No reference at ${golden}: run once with -Gate record on a build known to be good" }
$result = Compare-GateImages -Reference $golden -Candidate $shot
$invariant = [cultureinfo]::InvariantCulture
$verdict = if ($result.Pass) { 'PASS' } else { 'FAIL' }
Write-Output ("GATE {0} {1} psnr={2} dB hist={3} {4}" -f $verdict, $Name, $result.Psnr.ToString('0.0', $invariant), $result.Histogram.ToString('0.000', $invariant), $result.Reason)
"{0},{1},{2},{3},{4}" -f (Get-Date -Format s), $Name, $result.Psnr.ToString('0.00', $invariant), $result.Histogram.ToString('0.0000', $invariant), $verdict |
  Add-Content -Encoding utf8 "$root\logs\bench_gate.csv"
if (-not $result.Pass) { throw "Image gate failed for ${Name}: $($result.Reason)" }
