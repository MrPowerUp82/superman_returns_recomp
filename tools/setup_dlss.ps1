<#
Fetch the official pinned public NGX SDK for the project's authorized local work.
Preserves the license; performs no portal click, registration, or publication.
No game DLLs, interposer, installer, or machine configuration.
#>
[CmdletBinding()]
param(
    [string]$SdkRoot = (Join-Path $PSScriptRoot '../.tools/dlss-v3.7.20'),
    [string]$RuntimeDirectory
)
$ErrorActionPreference = 'Stop'
$revision = 'a11125971f4d5f19ce17ca8cbe6f703eaa16de76'
$licenseUrl = "https://github.com/NVIDIA/DLSS/blob/$revision/LICENSE.txt"
if (-not [Environment]::Is64BitProcess -or $env:OS -ne 'Windows_NT') {
    throw 'This setup stages the Windows x64 NGX SDK only.'
}
# Official Git blob IDs at the pinned commit (SHA-1 over 'blob <length>\0' + bytes).
# Also write raw SHA-256 for local audit/deployment. Never trust a moving tag/main.
$files = [ordered]@{
    'LICENSE.txt' = '8e8c157bbe2f7cfbbd25aa02b42c9f867fe71728'
    'include/nvsdk_ngx.h' = '0a7e5bb14f51be0b0e53d1f9c4a4ce97fb7948f1'
    'include/nvsdk_ngx_defs.h' = 'e86e446f8aee7c6aab2f466d0bd81ef24070b0d8'
    'include/nvsdk_ngx_helpers.h' = '79dd5a9865f02129a027a49d5f294944e712a1eb'
    'include/nvsdk_ngx_params.h' = 'd4f72f94e0fa9f8a32d5902a2626ef03b2e7c555'
    'lib/Windows_x86_64/rel/nvngx_dlss.dll' = '59c36c2d33d2268053a8bf498813025c91646bc1'
    'lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib' = '690ee22232caf0b4e5282f02c26a984e9e646329'
    'lib/Windows_x86_64/x86_64/nvsdk_ngx_d_dbg.lib' = 'b8a6eb9605f4ddf02f60e1976727c719777167f7'
    'lib/Windows_x86_64/x86_64/nvsdk_ngx_s.lib' = '0e9794b5c9108d3c368e885cdab1d45324afd566'
    'lib/Windows_x86_64/x86_64/nvsdk_ngx_s_dbg.lib' = '425ea86a8625cc3c6613d3f933963b2ef486c7c3'
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
$manifest = [ordered]@{ tag = 'v3.7.20'; commit = $revision; source = 'https://github.com/NVIDIA/DLSS'; license = $licenseUrl; files = $audit }
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
