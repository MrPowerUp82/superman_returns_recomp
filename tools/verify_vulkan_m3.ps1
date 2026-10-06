param(
    [string]$Python = 'python',
    [string]$Dotnet = '',
    [string]$NativeBuild = 'build/tests-native',
    [string]$VulkanBuild = 'build/tests-vulkan',
    [string]$TextureBuild = 'build/vulkan-main',
    [switch]$SkipGpu,
    [switch]$SkipCorpus
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
function Invoke-Check([string]$Executable, [string[]]$CheckArgs) {
    Write-Host "Checking $Executable $CheckArgs"
    & $Executable @CheckArgs
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
Push-Location $taskRoot
try {
    $pythonPath = (Get-Command $Python -ErrorAction Stop).Source
    if (-not $Dotnet) {
        $Dotnet = if (Test-Path -LiteralPath '.tools/dotnet-launcher/dotnet.exe' -PathType Leaf) { '.tools/dotnet-launcher/dotnet.exe' } else { 'dotnet' }
    }
    $nativeTests = Join-Path $NativeBuild 'sr_native_tests.exe'
    $vulkanTests = Join-Path $VulkanBuild 'sr_vulkan_tests.exe'
    $textureTests = Join-Path $TextureBuild 'sr_guest_texture_tests.exe'
    foreach ($testPath in @($nativeTests, $vulkanTests, $textureTests)) {
        if (-not (Test-Path -LiteralPath $testPath -PathType Leaf)) { throw "Build required test target: $testPath" }
    }
    Invoke-Check $nativeTests @()
    Invoke-Check $vulkanTests @()
    Invoke-Check $textureTests @()
    Invoke-Check $pythonPath @('-m', 'unittest', 'discover', 'tests/shaders')
    Invoke-Check $Dotnet @('run', '--project', 'tests/launcher/LauncherChecks.csproj', '--configuration', 'Release')
    $processTests = Join-Path $VulkanBuild 'sr_vulkan_shader_process_test.exe'
    Invoke-Check $processTests @($pythonPath)
    if ($SkipCorpus) { Write-Warning 'Shader corpus validation explicitly skipped.' }
    else {
        if (-not (Get-ChildItem artifacts/shaders/raw -Filter '*.bin' -File -ErrorAction SilentlyContinue)) {
            throw 'Empty or missing local shader corpus is not a pass.'
        }
        Invoke-Check $pythonPath @('tools/shaders/validate_vulkan_corpus.py')
    }
    if ($SkipGpu) { Write-Warning 'Hardware GPU fixtures explicitly skipped.' }
    else {
        Invoke-Check (Join-Path $VulkanBuild 'sr_vulkan_contract_test.exe') @('--production-bindings')
        $gpuTests = Join-Path $VulkanBuild 'sr_vulkan_resources_test.exe'
        Invoke-Check $gpuTests @()
        foreach ($fixture in @('targets', 'pipeline-cache', 'game-record', 'game-record-merged', 'resolve-copy', 'resolve-record', 'depth-resolve', 'edram-alias', 'alias-defaults', 'resolve-sample', 'game-frame', 'game-frame-async', 'immediate', 'stacked-resolve', 'composition', 'hold-lifetime')) {
            Invoke-Check $gpuTests @("--$fixture")
        }
    }
    Write-Host 'Requested automated checks passed. Game scene parity and window lifecycle still require recorded game QA.'
} finally { Pop-Location }
