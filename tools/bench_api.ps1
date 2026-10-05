<#
Roda tools\bench.ps1 com os argumentos que o launcher passa para a API escolhida (janela 1280x720,
limite de 30 FPS, opções de melhoria no padrão do jogo). O resultado vai para logs\bench_results.csv.

Uso: tools\bench_api.ps1 -Api vulkan|d3d12 -Name <rótulo> [-Profile] [-Exe <caminho>]
  -Profile liga SR_VULKAN_PROFILE=1 e copia o log do jogo para logs\bench_<Name>.log.
  -Exe     padrão: port\out\build\win-amd64-dist\superman_returns.exe
#>
param(
  [Parameter(Mandatory)] [ValidateSet('vulkan', 'd3d12')] [string]$Api,
  [Parameter(Mandatory)] [string]$Name,
  [string]$Exe = '',
  [switch]$Profile
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = "$root\port\out\build\win-amd64-dist\superman_returns.exe" }
$gameArgs = @(
  '--sr_renderer=native', '--sr_preset=custom', "--sr_native_api=$Api", '--sr_render_scale=100',
  '--window_width=1280', '--window_height=720', '--vsync=true', '--d3d12_adapter=-1',
  '--sr_native_fps_limit=30', '--sr_native_render_scale=1', '--sr_native_anisotropic_filtering=-1',
  '--sr_native_fxaa=false', '--sr_native_shadow_quality=1', '--sr_native_msaa_samples=1',
  '--mnk_mode=true', '--mnk_mouse=true'
) -join ' '
if ($Profile) { $env:SR_VULKAN_PROFILE = '1' }
& "$PSScriptRoot\bench.ps1" -Name $Name -Exe $Exe -ExtraArgs $gameArgs -TitleTimeout 180 -WorldTimeout 200
if ($Profile) { Copy-Item "$root\logs\game.log" "$root\logs\bench_$Name.log" -Force }
