# Dot-source: . .\tools\dev_env.ps1
# Carrega o ambiente x64 do Visual Studio e o clang/cmake/ninja do projeto nesta sessão PowerShell,
# para compilar e rodar os testes de tests/native e tests/vulkan. Só mexe em variáveis de ambiente
# (as variáveis locais levam o prefixo srEnv para não colidir com as da sessão).
$srEnvRoot = Split-Path $PSScriptRoot -Parent
$srEnvVs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
if (-not $srEnvVs) { throw 'Visual Studio Build Tools not found' }
foreach ($srEnvLine in (cmd /c "`"$srEnvVs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul 2>&1 && set")) {
  if ($srEnvLine -match '^([^=]+)=(.*)$' -and $Matches[1] -notmatch '[()]') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
$env:PATH = "$srEnvRoot\.tools\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin;$srEnvVs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$srEnvVs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
