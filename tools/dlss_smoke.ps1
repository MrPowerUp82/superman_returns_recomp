[CmdletBinding()]
param(
    [string]$SdkRoot = (Join-Path $PSScriptRoot '../.tools/dlss-v3.7.20'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../.tools/dlss-smoke'),
    [switch]$Evaluate
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdk = [IO.Path]::GetFullPath($SdkRoot)
$output = [IO.Path]::GetFullPath($OutputDirectory)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ x64 Build Tools are required.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
foreach ($required in @('include/nvsdk_ngx.h', 'lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib', 'lib/Windows_x86_64/rel/nvngx_dlss.dll', 'dlss-sdk-manifest.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdk $required))) { throw "Missing SDK file: $required; run tools/setup_dlss.ps1" }
}
$manifest = Get-Content -LiteralPath (Join-Path $sdk 'dlss-sdk-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.commit -ne 'a11125971f4d5f19ce17ca8cbe6f703eaa16de76') { throw 'Unexpected SDK revision' }
foreach ($file in $manifest.files) {
    if ((Get-FileHash -LiteralPath (Join-Path $sdk $file.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {
        throw "SDK SHA256 mismatch: $($file.path)"
    }
}
[void][IO.Directory]::CreateDirectory($output)
Copy-Item -LiteralPath (Join-Path $sdk 'lib/Windows_x86_64/rel/nvngx_dlss.dll') -Destination (Join-Path $output 'nvngx_dlss.dll') -Force
# Paths enter cmd through quoted environment expansions, not interpolated shell code.
$saved = @{}
foreach ($name in @('SR_DLSS_REPO', 'SR_DLSS_SDK', 'SR_DLSS_OUTPUT', 'SR_DLSS_VCVARS')) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $env:SR_DLSS_REPO = $repo; $env:SR_DLSS_SDK = $sdk
    $env:SR_DLSS_OUTPUT = $output; $env:SR_DLSS_VCVARS = $vcvars
    $command = Join-Path $output 'dlss_compile.cmd'
    @'
@echo off
call "%SR_DLSS_VCVARS%" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /MD /W4 /WX /DSR_HAS_DLSS=1 /external:I"%SR_DLSS_SDK%\include" /external:W0 "%SR_DLSS_REPO%\tests\gpu\dlss_smoke.cpp" "%SR_DLSS_REPO%\port\src\graphics\d3d12\dlss_super_resolution.cpp" /Fo"%SR_DLSS_OUTPUT%\\" /Fe"%SR_DLSS_OUTPUT%\dlss_smoke.exe" /link "%SR_DLSS_SDK%\lib\Windows_x86_64\x86_64\nvsdk_ngx_d.lib" d3d12.lib dxgi.lib dxguid.lib advapi32.lib user32.lib
exit /b %errorlevel%
'@ | Set-Content -LiteralPath $command -Encoding ASCII
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE) { throw "DLSS standalone compile failed: $LASTEXITCODE" }
    $arguments = @($output)
    if ($Evaluate) { $arguments += '--evaluate' }
    & (Join-Path $output 'dlss_smoke.exe') @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output 'dlss-smoke.log')
    if ($LASTEXITCODE) { throw "DLSS GPU capability smoke failed: $LASTEXITCODE; see $output/dlss-smoke.log" }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
