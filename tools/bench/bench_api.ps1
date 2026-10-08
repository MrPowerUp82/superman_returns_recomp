<#
Roda tools\bench\bench.ps1 com os argumentos que o launcher passa para a API escolhida (janela 1280x720,
limite de 30 FPS, opções de melhoria no padrão do jogo). O resultado vai para logs\bench_results.csv.

Uso: tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <rótulo> [-Profile] [-Timeline] [-TimelineDetail] [-Gate record|check] [-Exe <caminho>]
  -Profile liga SR_VULKAN_PROFILE=1 e copia o log do jogo para logs\bench_<Name>.log.
  -Timeline liga SR_FRAME_TIMELINE, grava logs\timeline_<Name>.csv e imprime a tabela do estágio limitante
            (tools\analysis\frame_timeline_report.py), também salva em logs\timeline_<Name>.txt.
  -TimelineDetail so tem efeito junto de -Timeline: liga SR_FRAME_TIMELINE_DETAIL=1, que acrescenta os contadores
            finos do front-end da thread do jogo (frontend, fe_*, game_guest, frontend_other). Custa cerca de uma
            leitura de relogio por hook/passo cronometrado (ver docs\native-renderer-timeline.md).
  -Gate    record grava a referência de imagem (build boa); check compara e falha se a imagem divergir (tools\bench\image_gate.ps1).
  -Exe     padrão: port\out\build\win-amd64-dist\superman_returns.exe
#>
param(
  [Parameter(Mandatory)] [ValidateSet('vulkan', 'd3d12')] [string]$Api,
  [Parameter(Mandatory)] [string]$Name,
  [string]$Exe = '',
  [switch]$Profile,
  [switch]$Timeline,
  [switch]$TimelineDetail,
  [ValidateSet('', 'record', 'check')] [string]$Gate = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Exe) { $Exe = "$root\port\out\build\win-amd64-dist\superman_returns.exe" }
$gameArgs = @(
  '--sr_renderer=native', '--sr_preset=custom', "--sr_native_api=$Api", '--sr_render_scale=100',
  '--window_width=1280', '--window_height=720', '--vsync=true', '--d3d12_adapter=-1',
  '--sr_native_fps_limit=30', '--sr_native_render_scale=1', '--sr_native_anisotropic_filtering=-1',
  '--sr_native_fxaa=false', '--sr_native_shadow_quality=1', '--sr_native_msaa_samples=1',
  '--mnk_mode=true', '--mnk_mouse=true'
) -join ' '
$prevProfile = $env:SR_VULKAN_PROFILE
$prevTimeline = $env:SR_FRAME_TIMELINE
$prevTimelineDetail = $env:SR_FRAME_TIMELINE_DETAIL
$timelineCsv = "$root\logs\timeline_$Name.csv"
try {
  if ($Profile) { $env:SR_VULKAN_PROFILE = '1' }
  if ($Timeline) {
    Remove-Item $timelineCsv -ErrorAction SilentlyContinue
    Remove-Item "$root\logs\timeline_$Name.txt" -ErrorAction SilentlyContinue
    $env:SR_FRAME_TIMELINE = $timelineCsv
    if ($TimelineDetail) { $env:SR_FRAME_TIMELINE_DETAIL = '1' }
  }
  & "$PSScriptRoot\bench.ps1" -Name $Name -Exe $Exe -ExtraArgs $gameArgs -TitleTimeout 180 -WorldTimeout 200
  if ($Gate) { & "$PSScriptRoot\image_gate.ps1" -Name $Name -Mode $Gate }
} finally {
  # O jogo já terminou: restaura as variáveis de ambiente primeiro, para que nenhuma falha abaixo
  # deixe SR_FRAME_TIMELINE / SR_FRAME_TIMELINE_DETAIL / SR_VULKAN_PROFILE vazarem para a sessão do chamador nem mascare o erro do bench.
  if ($null -ne $prevTimeline) { $env:SR_FRAME_TIMELINE = $prevTimeline } else { Remove-Item env:SR_FRAME_TIMELINE -ErrorAction SilentlyContinue }
  if ($null -ne $prevTimelineDetail) { $env:SR_FRAME_TIMELINE_DETAIL = $prevTimelineDetail } else { Remove-Item env:SR_FRAME_TIMELINE_DETAIL -ErrorAction SilentlyContinue }
  if ($null -ne $prevProfile) { $env:SR_VULKAN_PROFILE = $prevProfile } else { Remove-Item env:SR_VULKAN_PROFILE -ErrorAction SilentlyContinue }
  # Keep diagnostic evidence even when gameplay validation rejects the run.
  # The next invocation clears game.log before launching the game.
  if ($Profile -and (Test-Path "$root\logs\game.log")) {
    Copy-Item "$root\logs\game.log" "$root\logs\bench_$Name.log" -Force
  }
  if ($Timeline -and -not (Test-Path $timelineCsv)) {
    Write-Warning "timeline csv ausente: $timelineCsv"
  }
  if ($Timeline -and (Test-Path $timelineCsv)) {
    # Best-effort: uma falha no relatório não pode mascarar o resultado do bench nem abortar este finally.
    try {
      # O relatório tem acentos: força UTF-8 na saída do python e na leitura pelo PowerShell (5.1 usaria a página OEM).
      $prevConsoleEnc = [Console]::OutputEncoding
      $prevPyEnc = $env:PYTHONIOENCODING
      try {
        [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
        $env:PYTHONIOENCODING = 'utf-8'
        $report = @(python "$root\tools\analysis\frame_timeline_report.py" $timelineCsv)
        if ($LASTEXITCODE -ne 0) { throw "frame_timeline_report.py terminou com codigo $LASTEXITCODE" }
      } finally {
        try { [Console]::OutputEncoding = $prevConsoleEnc } catch { }
        if ($null -ne $prevPyEnc) { $env:PYTHONIOENCODING = $prevPyEnc } else { Remove-Item env:PYTHONIOENCODING -ErrorAction SilentlyContinue }
      }
      $report | Set-Content "$root\logs\timeline_$Name.txt" -Encoding UTF8
      $report
    } catch {
      Write-Warning "Relatorio de timeline nao gerado: $($_.Exception.Message)"
    }
  }
}
