# Resume um log do Vulkan gravado com SR_VULKAN_PROFILE=1: as 3 últimas médias por 120 quadros do
# Vulkan profile (gravação) e as 3 últimas linhas da captura de texturas (thread do jogo).
# Uso: tools\bench\vulkan_profile_summary.ps1 -Log logs\bench_<Name>.log
param([Parameter(Mandatory)] [string]$Log)
'--- Vulkan profile (ms/frame, média de 120 quadros)'
Select-String -Path $Log -Pattern 'Vulkan profile \(ms/frame' | Select-Object -Last 3 |
  ForEach-Object { ($_.Line -replace '^.*over 120\): ', '') -replace ' \| draws=.*$', '' }
'--- native Vulkan capture (thread do jogo)'
Select-String -Path $Log -Pattern 'native Vulkan capture frame' | Select-Object -Last 3 |
  ForEach-Object { $_.Line -replace '^.*native Vulkan capture ', '' }
