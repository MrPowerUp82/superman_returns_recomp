<#
Apaga arquivos gerados que podem ser recriados: logs e screenshots, caches e builds de teste avulsos.
Por padrão só MOSTRA o que apagaria (simulação). Para apagar de verdade, passe -Apply.

Grupos (sem nenhum, usa os três):
  -Logs        arquivos .log, .png e bench_* de logs\, first_run.log, os .log de port\out\build\*\logs
               e os dumps de quadro logs\vk_dump*
  -Caches      __pycache__ de tools\ e tests\, .pytest_cache, launcher\bin|obj e tests\launcher\bin|obj
  -TestBuilds  build\tests-native, build\tests-vulkan, build\vulkan-game, build\vulkan-main

Nunca toca em: .tools\, game\, artifacts\, build\vulkan-m2, build\vulkan-m3-runtime (o tradutor de shaders e o
cache do Vulkan em desenvolvimento), no executável do jogo (port\out\build\*), em logs\bench_results.csv, em
port\logs (guarda a imagem decodificada do jogo) nem nas pastas de dumps de shaders de logs\ (native_shaders,
rt_corpus*, rt_shaders*, runtime_shaders): tudo isso é ENTRADA do tools\shaders\build_corpus.ps1.

Uso:
  powershell -File tools\clean.ps1                  # simulação, todos os grupos
  powershell -File tools\clean.ps1 -Logs -Apply     # apaga só logs e screenshots
#>
param(
  [switch]$Apply,
  [switch]$Logs,
  [switch]$Caches,
  [switch]$TestBuilds
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not ($Logs -or $Caches -or $TestBuilds)) { $Logs = $Caches = $TestBuilds = $true }

# Caminhos que esta ferramenta jamais apaga (nem o que os contém).
$protected = @('.tools', 'game', 'artifacts', 'build\vulkan-m2', 'build\vulkan-m3-runtime', 'port\logs',
               'logs\native_shaders', 'logs\rt_corpus', 'logs\rt_corpus3', 'logs\rt_shaders', 'logs\rt_shaders2',
               'logs\runtime_shaders') |
  ForEach-Object { [IO.Path]::GetFullPath((Join-Path $root $_)).TrimEnd('\') }
$keepFile = [IO.Path]::GetFullPath((Join-Path $root 'logs\bench_results.csv'))

function Test-Protected([string]$path) {
  $full = [IO.Path]::GetFullPath($path).TrimEnd('\')
  if (-not $full.StartsWith("$root\", [StringComparison]::OrdinalIgnoreCase)) { return $true }
  if ($full -eq $keepFile) { return $true }
  foreach ($p in $protected) {
    if ($full -eq $p -or $full.StartsWith("$p\", [StringComparison]::OrdinalIgnoreCase) -or
        $p.StartsWith("$full\", [StringComparison]::OrdinalIgnoreCase)) { return $true }
  }
  return $false
}

function Get-PathSize([string]$path) {
  $item = Get-Item -LiteralPath $path -Force
  if (-not $item.PSIsContainer) { return $item.Length }
  $sum = (Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
  if ($sum) { return $sum } else { return 0 }
}

$targets = New-Object System.Collections.Generic.List[object]
function Add-Target([string]$group, [string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return }
  $full = (Resolve-Path -LiteralPath $path).Path
  if (Test-Protected $full) { return }
  $targets.Add([pscustomobject]@{ Group = $group; Path = $full })
}

if ($Logs) {
  # Só o que é claramente descartável; qualquer outra pasta de logs\ fica como está.
  if (Test-Path "$root\logs") {
    Get-ChildItem "$root\logs" -File -Force |
      Where-Object { $_.Extension -in '.log', '.png' -or $_.Name -like 'bench_*' } |
      ForEach-Object { Add-Target 'logs' $_.FullName }
    Get-ChildItem "$root\logs" -Directory -Force -Filter 'vk_dump*' | ForEach-Object { Add-Target 'logs' $_.FullName }
  }
  Add-Target 'logs' "$root\first_run.log"
  Get-ChildItem "$root\port\out\build" -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    if (Test-Path "$($_.FullName)\logs") {
      Get-ChildItem "$($_.FullName)\logs" -File -Filter '*.log' -Force | ForEach-Object { Add-Target 'logs' $_.FullName }
    }
  }
}
if ($Caches) {
  foreach ($base in 'tools', 'tests') {
    Get-ChildItem "$root\$base" -Recurse -Directory -Force -Filter '__pycache__' -ErrorAction SilentlyContinue |
      ForEach-Object { Add-Target 'caches' $_.FullName }
  }
  foreach ($rel in '.pytest_cache', 'launcher\bin', 'launcher\obj', 'tests\launcher\bin', 'tests\launcher\obj') {
    Add-Target 'caches' "$root\$rel"
  }
}
if ($TestBuilds) {
  foreach ($rel in 'build\tests-native', 'build\tests-vulkan', 'build\vulkan-game', 'build\vulkan-main') {
    Add-Target 'testbuilds' "$root\$rel"
  }
}

if ($Apply -and (Get-Process superman_returns -ErrorAction SilentlyContinue)) {
  throw 'O jogo está aberto (superman_returns.exe): feche-o antes de apagar.'
}

foreach ($t in $targets) { $t | Add-Member -NotePropertyName Bytes -NotePropertyValue (Get-PathSize $t.Path) }
$mb = { param($b) '{0,9:N1} MB' -f ($b / 1MB) }

if (-not $targets.Count) { Write-Output 'Nada para limpar.'; return }
$total = 0
foreach ($g in $targets | Group-Object Group) {
  $bytes = ($g.Group | Measure-Object Bytes -Sum).Sum
  $total += $bytes
  Write-Output ("[{0}] {1} itens, {2}" -f $g.Name, $g.Count, (& $mb $bytes).Trim())
  # Poucos itens: lista cada um. Muitos (logs e screenshots): só os 8 maiores.
  $list = if ($g.Count -le 12) { $g.Group } else { $g.Group | Sort-Object Bytes -Descending | Select-Object -First 8 }
  foreach ($t in $list) { Write-Output ("  {0}  {1}" -f (& $mb $t.Bytes), $t.Path.Substring($root.Length + 1)) }
  if ($g.Count -gt 12) { Write-Output ("  ... e mais {0} itens menores" -f ($g.Count - 8)) }
}
Write-Output ("Total: {0}" -f (& $mb $total).Trim())

if (-not $Apply) {
  Write-Output 'Simulação: nada foi apagado. Rode com -Apply para apagar.'
  return
}
foreach ($t in $targets) {
  if (Test-Protected $t.Path) { throw "Recusado (protegido): $($t.Path)" }
  Remove-Item -LiteralPath $t.Path -Recurse -Force
}
Write-Output ("Apagado: {0}" -f (& $mb $total).Trim())
