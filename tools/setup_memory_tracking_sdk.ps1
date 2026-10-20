param([string]$Checkout = '', [switch]$VerifyOnly, [switch]$SkipDependencies)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$toolsRoot = [IO.Path]::GetFullPath((Join-Path $root '.tools'))
if (-not $Checkout) { $Checkout = Join-Path $toolsRoot 'rexglue-write-tracking-source' }
$Checkout = [IO.Path]::GetFullPath($Checkout)
if (-not $Checkout.StartsWith($toolsRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'SDK checkout must be a child of this workspace .tools directory'
}
$ancestor = $Checkout
while ($ancestor -and $ancestor.Length -ge $toolsRoot.Length) {
  if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'SDK checkout must not traverse a junction or symbolic link'
  }
  $ancestor = Split-Path $ancestor -Parent
}
$expected = 'f5337cdc947ff6d4c4196737e2c807a48f2a1fc2'
$patch = Join-Path $PSScriptRoot 'patches/rexglue/v0.10.0/0001-memory-write-tracking.patch'
$shared = Join-Path $PSScriptRoot 'rexglue-write-tracking'
function Invoke-SdkGit([string[]]$Arguments) {
  $result = & git.exe -c core.autocrlf=false -C $Checkout @Arguments
  if ($LASTEXITCODE -ne 0) { throw "git failed: $Arguments" }
  return ($result -join "`n")
}
function Hash([string]$Path) {
  $stream = [IO.File]::OpenRead($Path)
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
  finally { $sha.Dispose(); $stream.Dispose() }
}
if (-not (Test-Path -LiteralPath $Checkout)) {
  if ($VerifyOnly) { throw 'SDK checkout missing' }
  & git clone --quiet --depth 1 --branch v0.10.0 --filter=blob:none 'https://github.com/rexglue/rexglue-sdk.git' $Checkout
  if ($LASTEXITCODE) { throw 'SDK clone failed' }
}
if ((Invoke-SdkGit -Arguments @('rev-parse','HEAD')).Trim() -ne $expected) { throw 'SDK revision does not match pinned v0.10.0' }
$manifestPath = Join-Path $Checkout '.sr-memory-tracking.json'
$manifest = $null
if (Test-Path -LiteralPath $manifestPath) { $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json }
$sharedFiles = @(Get-ChildItem -LiteralPath $shared -File -Recurse)
$allowed = @('.sr-memory-tracking.json')
foreach ($file in $sharedFiles) {
  $allowed += 'src/system/write_tracking/' + $file.FullName.Substring($shared.Length + 1).Replace([char]92, [char]47)
}
foreach ($path in ((Invoke-SdkGit -Arguments @('ls-files','--others','--exclude-standard')) -split "`n")) {
  if ($path -and $allowed -notcontains $path) { throw "Unrecognized SDK build input: $path; nothing overwritten" }
}
$patchText = [IO.File]::ReadAllText($patch).Replace("`r`n", "`n").TrimEnd()
$diff = (Invoke-SdkGit -Arguments @('diff','HEAD','--binary','--no-ext-diff','--ignore-submodules=all')).Replace("`r`n", "`n").TrimEnd()
if ($diff -ne $patchText) {
  if ($diff) { throw 'SDK has a partial patch or unknown local changes; nothing overwritten' }
  if ($VerifyOnly) { throw 'SDK patch missing' }
  Invoke-SdkGit -Arguments @('apply','--check',$patch) | Out-Null
  Invoke-SdkGit -Arguments @('apply',$patch) | Out-Null
}
$installed = [ordered]@{}
foreach ($file in $sharedFiles) {
  $relative = $file.FullName.Substring($shared.Length + 1).Replace('\','/')
  $key = 'src/system/write_tracking/' + $relative
  $destination = Join-Path $Checkout $key
  $canonicalHash = Hash $file.FullName
  if (Test-Path -LiteralPath $destination) {
    $currentHash = Hash $destination
    if ($currentHash -ne $canonicalHash) {
      $previous = if ($manifest) { $manifest.files.PSObject.Properties[$key] } else { $null }
      if (-not $previous -or $previous.Value -ne $currentHash) { throw "Unrecognized source edit: $key; nothing overwritten" }
      if ($VerifyOnly) { throw "SDK source manifest mismatch: $key" }
    }
  } elseif ($VerifyOnly) { throw "SDK source missing: $key" }
  $installed[$key] = $canonicalHash
}
# Preflight every file before writing any shared source.
if (-not $VerifyOnly) {
  foreach ($file in $sharedFiles) {
    $key = 'src/system/write_tracking/' + $file.FullName.Substring($shared.Length + 1)
    $destination = Join-Path $Checkout $key
    New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
  }
}
foreach ($match in [regex]::Matches($patchText, '(?m)^\+\+\+ b/(.+)$')) {
  $key = $match.Groups[1].Value
  $installed[$key] = Hash (Join-Path $Checkout $key)
}
$patchHash = Hash $patch
if ($VerifyOnly) {
  if (-not $manifest -or $manifest.revision -ne $expected -or $manifest.patch_sha256 -ne $patchHash) { throw 'SDK manifest missing or outdated' }
  foreach ($key in $installed.Keys) {
    if ($manifest.files.PSObject.Properties[$key].Value -ne $installed[$key]) { throw "SDK manifest mismatch: $key" }
  }
} else {
  [ordered]@{ revision=$expected; patch_sha256=$patchHash; files=$installed } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
}
if (-not $SkipDependencies) {
  $modules = @('cli11','libmspack','FFmpeg','tomlplusplus','simde','xxHash','spdlog','fmt','utfcpp','imgui','sdl3','o1heap','inja') |
    ForEach-Object { 'thirdparty/' + $_ }
  foreach ($module in $modules) {
    $directory = Join-Path $Checkout $module
    if (Test-Path -LiteralPath (Join-Path $directory '.git')) {
      $expectedModule = ((Invoke-SdkGit -Arguments @('ls-tree','HEAD',$module)) -split '\s+')[2]
      $actualModule = (& git.exe -C $directory rev-parse HEAD) -join ''
      if ($LASTEXITCODE -or $actualModule.Trim() -ne $expectedModule) { throw "SDK dependency revision mismatch: $module" }
      & git.exe -C $directory diff --quiet HEAD --
      if ($LASTEXITCODE) { throw "SDK dependency has local changes: $module" }
    } elseif ($VerifyOnly) { throw "SDK dependency missing: $module" }
  }
  if (-not $VerifyOnly) {
    Invoke-SdkGit -Arguments (@('submodule','update','--init','--recursive','--depth','1','--jobs','4','--') + $modules) | Out-Null
  }
  $status = Invoke-SdkGit -Arguments (@('submodule','status','--recursive','--') + $modules)
  if ($status -match '(?m)^[-+U]') { throw 'SDK dependency revision or initialization mismatch' }
}
Write-Output "Memory tracking SDK verified: $Checkout ($expected)"
