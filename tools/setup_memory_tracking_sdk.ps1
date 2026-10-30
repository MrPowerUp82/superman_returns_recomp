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
$patchNames = @(
  'tools/patches/rexglue/v0.10.0/0001-memory-write-tracking.patch',
  'tools/patches/rexglue/v0.10.0/0002-xma-drain-pending.patch'
)
$shared = Join-Path $PSScriptRoot 'rexglue-write-tracking'
function Invoke-SdkGit([string[]]$Arguments) {
  $result = & git.exe -c core.autocrlf=false -c diff.suppressBlankEmpty=false -c diff.interHunkContext=0 --literal-pathspecs -C $Checkout @Arguments
  if ($LASTEXITCODE -ne 0) { throw "git failed: $Arguments" }
  return ($result -join "`n")
}
function Hash([string]$Path) {
  $stream = [IO.File]::OpenRead($Path)
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
  finally { $sha.Dispose(); $stream.Dispose() }
}
function Assert-SdkPath([string]$Key) {
  if ($Key -match '(^/|\\|:|(^|/)(\.|\.\.|\.git)(/|$))') { throw "Unsafe SDK path: $Key" }
  $path = Join-Path $Checkout $Key
  while ($path.Length -gt $Checkout.Length) {
    if ((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "SDK path must not traverse a junction or symbolic link: $Key"
    }
    $path = Split-Path $path -Parent
  }
}
function Read-SdkBlob([string]$Object) {
  # Read bytes directly: Windows PowerShell native pipelines otherwise decode
  # Git's UTF-8 output using the console code page before comparison.
  if ($Object -notmatch '^(HEAD:[A-Za-z0-9_./-]+|[0-9a-f]{40})$') { throw 'Unsafe canonical blob identifier' }
  $info = New-Object Diagnostics.ProcessStartInfo
  $info.FileName = 'git.exe'; $info.WorkingDirectory = $Checkout
  $info.Arguments = 'cat-file blob ' + $Object
  $info.UseShellExecute = $false; $info.CreateNoWindow = $true
  $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
  $process = New-Object Diagnostics.Process
  $process.StartInfo = $info
  $bytes = New-Object IO.MemoryStream
  try {
    [void]$process.Start()
    $process.StandardOutput.BaseStream.CopyTo($bytes)
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode) { throw "Cannot read canonical SDK blob: $errorText" }
    return [Text.Encoding]::UTF8.GetString($bytes.ToArray()).Replace("`r`n", "`n")
  } finally { $bytes.Dispose(); $process.Dispose() }
}
function Get-CanonicalSdkContent($SelectedPatches = $patches) {
  # Build a fresh temporary index from the pinned commit. The user's real
  # index (including assume-unchanged / skip-worktree) is never modified.
  $indexPath = [IO.Path]::GetTempFileName()
  [IO.File]::Delete($indexPath)
  $savedIndex = [Environment]::GetEnvironmentVariable('GIT_INDEX_FILE','Process')
  $original = @{}; $patched = @{}
  try {
    $env:GIT_INDEX_FILE = $indexPath
    Invoke-SdkGit -Arguments @('read-tree','HEAD') | Out-Null
    foreach ($patch in $SelectedPatches) {
      Invoke-SdkGit -Arguments @('apply','--cached',$patch.path) | Out-Null
      foreach ($key in $patch.paths) {
        $entry = Invoke-SdkGit -Arguments @('ls-files','--stage','--',$key)
        if ($entry -notmatch '^100[0-7]{3} ([0-9a-f]{40}) 0\t') { throw "Missing canonical SDK blob: $key" }
        $patched[$key] = Read-SdkBlob $Matches[1]
        $original[$key] = Read-SdkBlob ('HEAD:' + $key)
      }
    }
  } finally {
    [Environment]::SetEnvironmentVariable('GIT_INDEX_FILE',$savedIndex,'Process')
    [IO.File]::Delete($indexPath)
    [IO.File]::Delete($indexPath + '.lock')
  }
  return [pscustomobject]@{ original=$original; patched=$patched }
}
function Assert-CanonicalSdkContent($Expected) {
  foreach ($key in $Expected.Keys) {
    $actual = [IO.File]::ReadAllText((Join-Path $Checkout $key)).Replace("`r`n", "`n")
    if ($actual -cne $Expected[$key]) { throw "Canonical SDK content mismatch: $key; nothing overwritten" }
  }
}
# Read the complete canonical patch set before cloning or changing the checkout.
$patches = @()
$patchHashes = [ordered]@{}
$patchedPaths = @{}
foreach ($name in $patchNames) {
  $patchPath = Join-Path $root $name
  $patchText = [IO.File]::ReadAllText($patchPath).Replace("`r`n", "`n").TrimEnd()
  $paths = @([regex]::Matches($patchText, '(?m)^\+\+\+ b/(.+)$') | ForEach-Object { $_.Groups[1].Value })
  if (-not $paths.Count -or $paths.Count -ne [regex]::Matches($patchText, '(?m)^diff --git ').Count) {
    throw "Canonical patch must modify existing SDK paths: $name"
  }
  foreach ($key in $paths) {
    Assert-SdkPath $key
    if ($patchedPaths.ContainsKey($key)) { throw "Canonical patches must have disjoint paths: $key" }
    $patchedPaths[$key] = $true
  }
  $patchHashes[$name] = Hash $patchPath
  $patches += [pscustomobject]@{ name=$name; path=$patchPath; text=$patchText; paths=$paths }
}
if (-not (Test-Path -LiteralPath $Checkout)) {
  if ($VerifyOnly) { throw 'SDK checkout missing' }
  & git clone --quiet --depth 1 --branch v0.10.0 --filter=blob:none 'https://github.com/rexglue/rexglue-sdk.git' $Checkout
  if ($LASTEXITCODE) { throw 'SDK clone failed' }
}
if ((Invoke-SdkGit -Arguments @('rev-parse','HEAD')).Trim() -ne $expected) { throw 'SDK revision does not match pinned v0.10.0' }
$manifestPath = Join-Path $Checkout '.sr-memory-tracking.json'
Assert-SdkPath '.sr-memory-tracking.json'
$manifest = $null
if (Test-Path -LiteralPath $manifestPath) { $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json }
$sharedFiles = @(Get-ChildItem -LiteralPath $shared -File -Recurse)
$allowed = @('.sr-memory-tracking.json')
foreach ($file in $sharedFiles) {
  $key = 'src/system/write_tracking/' + $file.FullName.Substring($shared.Length + 1).Replace([char]92, [char]47)
  Assert-SdkPath $key
  if ($patchedPaths.ContainsKey($key)) { throw "Shared source overlaps canonical patch: $key" }
  $allowed += $key
}
foreach ($path in ((Invoke-SdkGit -Arguments @('ls-files','--others','--exclude-standard')) -split "`n")) {
  if ($path -and $allowed -notcontains $path) { throw "Unrecognized SDK build input: $path; nothing overwritten" }
}
foreach ($key in ((Invoke-SdkGit -Arguments @('diff','HEAD','--name-only','--no-ext-diff','--ignore-submodules=all')) -split "`n")) {
  if ($key -and -not $patchedPaths.ContainsKey($key)) {
    throw "SDK has unknown local changes: $key; nothing overwritten"
  }
}
$pending = @()
$upgrades = @{}
foreach ($patch in $patches) {
  $diff = (Invoke-SdkGit -Arguments (@('diff','HEAD','--binary','--no-ext-diff','--no-textconv','--no-color','--no-renames','--src-prefix=a/','--dst-prefix=b/','--unified=3','--abbrev=7','--ignore-submodules=all','--') + $patch.paths)).Replace("`r`n", "`n").TrimEnd()
  if ($diff -cne $patch.text) {
    if ($diff) {
      $previous = Join-Path $PSScriptRoot ('patches/rexglue/v0.10.0/previous/' + (Split-Path $patch.path -Leaf))
      if (-not (Test-Path -LiteralPath $previous) -or
          $diff -cne [IO.File]::ReadAllText($previous).Replace("`r`n", "`n").TrimEnd()) {
        throw "SDK has a partial patch or unknown local changes: $($patch.name); nothing overwritten"
      }
      if ($VerifyOnly) { throw "SDK patch needs canonical upgrade: $($patch.name)" }
      $upgrades[$patch.name] = $previous
      $pending += $patch
      continue
    }
    if ($VerifyOnly) { throw "SDK patch missing: $($patch.name)" }
    Invoke-SdkGit -Arguments @('apply','--check',$patch.path) | Out-Null
    $pending += $patch
  }
}
$canonical = Get-CanonicalSdkContent
$beforeCanonical = $null
if ($upgrades.Count) {
  $beforePatches = @($patches | ForEach-Object {
    if ($upgrades.ContainsKey($_.name)) {
      [pscustomobject]@{path=$upgrades[$_.name];paths=$_.paths}
    } else { $_ }
  })
  $beforeCanonical = Get-CanonicalSdkContent $beforePatches
}
$expectedBefore = @{}
foreach ($patch in $patches) {
  foreach ($key in $patch.paths) {
    $expectedBefore[$key] = if ($upgrades.ContainsKey($patch.name)) { $beforeCanonical.patched[$key] }
      elseif ($pending -contains $patch) { $canonical.original[$key] } else { $canonical.patched[$key] }
  }
}
Assert-CanonicalSdkContent $expectedBefore
$installed = [ordered]@{}
$copyFiles = @()
foreach ($file in $sharedFiles) {
  $relative = $file.FullName.Substring($shared.Length + 1).Replace([char]92, [char]47)
  $key = 'src/system/write_tracking/' + $relative
  $destination = Join-Path $Checkout $key
  $canonicalHash = Hash $file.FullName
  $currentHash = $null
  if (Test-Path -LiteralPath $destination) {
    $currentHash = Hash $destination
    if ($currentHash -ne $canonicalHash) {
      $previous = if ($manifest) { $manifest.files.PSObject.Properties[$key] } else { $null }
      if (-not $previous -or $previous.Value -ne $currentHash) { throw "Unrecognized source edit: $key; nothing overwritten" }
      if ($VerifyOnly) { throw "SDK source manifest mismatch: $key" }
    }
  } elseif ($VerifyOnly) { throw "SDK source missing: $key" }
  $installed[$key] = $canonicalHash
  if ($currentHash -ne $canonicalHash) { $copyFiles += $file }
}
# All patches and shared sources passed preflight before any SDK input changes.
if (-not $VerifyOnly) {
  foreach ($patch in $pending) {
    if ($upgrades.ContainsKey($patch.name)) {
      Invoke-SdkGit -Arguments @('apply','--reverse',$upgrades[$patch.name]) | Out-Null
    }
    Invoke-SdkGit -Arguments @('apply',$patch.path) | Out-Null
  }
  foreach ($file in $copyFiles) {
    $key = 'src/system/write_tracking/' + $file.FullName.Substring($shared.Length + 1)
    $destination = Join-Path $Checkout $key
    New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
  }
}
foreach ($patch in $patches) {
  foreach ($key in $patch.paths) {
    $installed[$key] = Hash (Join-Path $Checkout $key)
  }
}
Assert-CanonicalSdkContent $canonical.patched
$patchHash = $patchHashes[$patchNames[0]]
if ($VerifyOnly) {
  if (-not $manifest -or $manifest.revision -ne $expected -or $manifest.patch_sha256 -ne $patchHash) { throw 'SDK manifest missing or outdated' }
  $recordedPatches = @($manifest.patches.PSObject.Properties)
  if ($recordedPatches.Count -ne $patchNames.Count) { throw 'SDK patch manifest missing or outdated' }
  for ($i = 0; $i -lt $patchNames.Count; $i++) {
    $name = $patchNames[$i]
    if ($recordedPatches[$i].Name -cne $name -or $recordedPatches[$i].Value -ne $patchHashes[$name]) {
      throw "SDK patch manifest mismatch: $name"
    }
  }
  foreach ($key in $installed.Keys) {
    if ($manifest.files.PSObject.Properties[$key].Value -ne $installed[$key]) { throw "SDK manifest mismatch: $key" }
  }
} else {
  $manifestJson = [ordered]@{ revision=$expected; patch_sha256=$patchHash; patches=$patchHashes; files=$installed } |
    ConvertTo-Json -Depth 4
  # Set-Content appends a newline. Compare the serialized content first so an
  # unchanged setup never dirties build inputs or the verification manifest.
  if (-not (Test-Path -LiteralPath $manifestPath) -or
      [IO.File]::ReadAllText($manifestPath) -cne ($manifestJson + [Environment]::NewLine)) {
    $manifestJson | Set-Content -LiteralPath $manifestPath -Encoding UTF8
  }
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
