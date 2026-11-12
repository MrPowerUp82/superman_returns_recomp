<#
Fetch the official pinned public NGX SDK for the project's authorized local work.
Preserves the license; performs no portal click, registration, or publication.
No game DLLs, interposer, installer, or machine configuration.
#>
[CmdletBinding()]
param(
    [string]$SdkRoot = (Join-Path $PSScriptRoot '../.tools/dlss-v310.9.1'),
    [string]$RuntimeDirectory
)
$ErrorActionPreference = 'Stop'
$revision = '374959484e79a640feaba44c93ac8cfb0a03f5b5'  # tag v310.9.1 (DLSS 4 transformer presets J/K/L/M)
$licenseUrl = "https://github.com/NVIDIA/DLSS/blob/$revision/LICENSE.txt"
if (-not [Environment]::Is64BitProcess -or $env:OS -ne 'Windows_NT') {
    throw 'This setup stages the Windows x64 NGX SDK only.'
}
# Official Git blob IDs at the pinned commit (SHA-1 over 'blob <length>\0' + bytes).
# Also write raw SHA-256 for local audit/deployment. Never trust a moving tag/main.
$files = [ordered]@{
    'LICENSE.txt' = '80756566d773cdac55179c5ae16c4495df7f73b5'
    'include/nvsdk_ngx.h' = 'ae6b49174049eed69cc377b2fffd3c901651f764'
    'include/nvsdk_ngx_defs.h' = '807fa2475e90214f6846b9b4840be92e7bd66b5b'
    'include/nvsdk_ngx_helpers.h' = 'e6a0f2581e1586240821afa7523a65a75654e847'
    'include/nvsdk_ngx_helpers_d3d.h' = '664f99d95d3f8375304dc558ec601a66635ade7b'
    'include/nvsdk_ngx_helpers_cuda.h' = '251ba9d5fcf0800bbc3fbb5e8870c61739c6d98a'
    'include/nvsdk_ngx_loader.h' = '13756a449673327b08dfc9e6715937636de4ca48'
    'include/nvsdk_ngx_params.h' = '014d9f4adec600ea3677947b7f98258da9f9c7fc'
    'lib/Windows_x86_64/rel/nvngx_dlss.dll' = '875981984f56e71b4fde6605484ef767dadf9910'
    'lib/Windows_x86_64/x64/nvsdk_ngx_d.lib' = '6ddf9dbcb4961d2948982177ab7db73fce7a4d40'
    'lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib' = '04c664553652ead566f8b6df858b639acb341cf4'
    'lib/Windows_x86_64/x64/nvsdk_ngx_s.lib' = 'fcd9adc26d111e7013be555d27a4cf60e5753eb5'
    'lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib' = '3f73215a02142f3db9cc337d5e6868f02601f976'
}

function Get-GitBlobHash([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $prefix = [Text.Encoding]::ASCII.GetBytes("blob $($bytes.Length)`0")
    $sha = [Security.Cryptography.SHA1]::Create()
    try {
        [void]$sha.TransformBlock($prefix, 0, $prefix.Length, $prefix, 0)
        [void]$sha.TransformFinalBlock($bytes, 0, $bytes.Length)
        return ([BitConverter]::ToString($sha.Hash)).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

$root = [IO.Path]::GetFullPath($SdkRoot)
# Never overwrite a pre-existing installation or an arbitrary user's directory.
if (Test-Path -LiteralPath $root) { throw "Destination already exists: $root" }
[void][IO.Directory]::CreateDirectory($root)
$audit = @()
foreach ($entry in $files.GetEnumerator()) {
    $destination = Join-Path $root $entry.Key
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/NVIDIA/DLSS/$revision/$($entry.Key)" -OutFile $destination
    $actual = Get-GitBlobHash $destination
    if ($actual -ne $entry.Value) { throw "Official blob hash mismatch: $($entry.Key). Staging incomplete; do not use $root" }
    if ($entry.Key.EndsWith('.dll')) {
        $signature = Get-AuthenticodeSignature -LiteralPath $destination
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'NVIDIA Corporation') {
            throw "NVIDIA Authenticode validation failed: $destination"
        }
    }
    $audit += [ordered]@{ path = $entry.Key; gitBlobSha1 = $actual; sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$manifest = [ordered]@{ tag = 'v310.9.1'; commit = $revision; source = 'https://github.com/NVIDIA/DLSS'; license = $licenseUrl; files = $audit }
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'dlss-sdk-manifest.json') -Encoding UTF8
if ($RuntimeDirectory) {
    $runtime = [IO.Path]::GetFullPath($RuntimeDirectory)
    if (-not (Test-Path -LiteralPath $runtime -PathType Container)) { throw "Runtime directory must already exist: $runtime" }
    $target = Join-Path $runtime 'nvngx_dlss.dll'
    if (Test-Path -LiteralPath $target) { throw "Runtime DLL already exists; no overwrite: $target" }
    Copy-Item -LiteralPath (Join-Path $root 'lib/Windows_x86_64/rel/nvngx_dlss.dll') -Destination $target
    Write-Output "Staged official release runtime: $target"
}
Write-Output "Pinned SDK staged at $root; audit: dlss-sdk-manifest.json. No CMake/build/game execution performed."
