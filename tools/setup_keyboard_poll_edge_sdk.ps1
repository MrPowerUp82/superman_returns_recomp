param([Parameter(Mandatory=$true)][string]$Checkout,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$toolsRoot=[IO.Path]::GetFullPath((Join-Path $root '.tools'))
$sdkPath=[IO.Path]::GetFullPath($Checkout)
if(-not $sdkPath.StartsWith($toolsRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Select an isolated SDK checkout inside this repository .tools directory'}
if($sdkPath -eq (Join-Path $toolsRoot 'rexglue-sdk-source')) {throw 'The original SDK checkout is preserved; select the isolated object-motion SDK checkout'}
$ancestor=$sdkPath
while($ancestor.Length -ge $toolsRoot.Length) {
  if((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'SDK checkout must not traverse a junction or symbolic link'}
  $ancestor=Split-Path $ancestor -Parent
}
function TextHash([string]$Path) {
  $bytes=[Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($Path).Replace("`r`n","`n"))
  $sha=[Security.Cryptography.SHA256]::Create()
  try {return [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','')}
  finally {$sha.Dispose()}
}
function SafeFile([string]$Relative) {
  if($Relative -match '(^/|\\|:|(^|/)(\.|\.\.|\.git)(/|$))') {throw "Unsafe SDK overlay path: $Relative"}
  $path=Join-Path $sdkPath $Relative
  while($path.Length -gt $sdkPath.Length) {
    if((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw "SDK overlay path traverses a link: $Relative"}
    $path=Split-Path $path -Parent
  }
}
$patch=Join-Path $PSScriptRoot 'patches/rexglue/v0.10.0/0004-mnk-button-poll-edges.patch'
$canonical=Get-Content -LiteralPath ($patch -replace '\.patch$','.manifest.json') -Raw | ConvertFrom-Json
$marker=Join-Path $sdkPath '.sr-keyboard-poll-edge.json'
$sha=[Security.Cryptography.SHA256]::Create()
try {$patchHash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($patch))).Replace('-','')}
finally {$sha.Dispose()}
if($patchHash -ne $canonical.patch_sha256) {throw 'Keyboard patch does not match its versioned manifest'}
$revision=(& git -C $sdkPath rev-parse HEAD) -join ''
if($LASTEXITCODE -or $revision.Trim() -ne $canonical.revision) {throw 'Keyboard SDK revision differs from pinned v0.10.0'}
SafeFile '.sr-keyboard-poll-edge.json'
$alreadyPatched=$true
$alreadyBase=$true
foreach($entry in $canonical.files.PSObject.Properties) {
  SafeFile $entry.Name
  $hash=TextHash (Join-Path $sdkPath $entry.Name)
  $alreadyPatched=$alreadyPatched -and ($hash -eq $entry.Value)
  $alreadyBase=$alreadyBase -and ($hash -eq $canonical.original_files.PSObject.Properties[$entry.Name].Value)
}
foreach($entry in $canonical.shared_files.PSObject.Properties) {
  SafeFile $entry.Name
  $relative=$entry.Name -replace '^src/system/write_tracking/',''
  if((TextHash (Join-Path $PSScriptRoot ('rexglue-write-tracking/'+$relative))) -ne $entry.Value) {throw 'Project keyboard helper differs from versioned overlay; regenerate patch manifest'}
}
if(-not $alreadyPatched) {
  if($VerifyOnly) {throw 'Keyboard SDK overlay is missing or has unknown changes'}
  if(-not $alreadyBase) {throw 'MnK sources differ from exact memory-tracking baseline; nothing overwritten'}
  & git -c core.autocrlf=false -C $sdkPath apply --check $patch
  if($LASTEXITCODE) {throw 'Keyboard SDK overlay preflight failed; nothing overwritten'}
  # All base inputs and shared files must also pass the canonical verifier.
  & (Join-Path $PSScriptRoot 'setup_memory_tracking_sdk.ps1') -Checkout $sdkPath -SkipDependencies
  if(-not $?) {throw 'Keyboard SDK baseline verification failed'}
  & git -c core.autocrlf=false -C $sdkPath apply $patch
  if($LASTEXITCODE) {throw 'Cannot apply keyboard SDK overlay'}
}
foreach($entry in $canonical.files.PSObject.Properties) {
  if((TextHash (Join-Path $sdkPath $entry.Name)) -ne $entry.Value) {throw "Keyboard SDK canonical content mismatch: $($entry.Name)"}
}
foreach($entry in $canonical.shared_files.PSObject.Properties) {
  $path=Join-Path $sdkPath $entry.Name
  if(-not (Test-Path -LiteralPath $path) -or (TextHash $path) -ne $entry.Value) {throw "Keyboard SDK helper mismatch: $($entry.Name)"}
}
$serialized=$canonical | ConvertTo-Json -Depth 4
if($VerifyOnly) {
  if(-not (Test-Path -LiteralPath $marker)) {throw 'Keyboard SDK manifest is missing'}
  $recorded=Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
  if(($recorded | ConvertTo-Json -Depth 4) -cne $serialized) {throw 'Keyboard SDK manifest is outdated'}
} else {
  if(-not (Test-Path -LiteralPath $marker) -or [IO.File]::ReadAllText($marker).TrimEnd() -cne $serialized.TrimEnd()) {
    $serialized | Set-Content -LiteralPath $marker -Encoding UTF8
  }
  # Records the final hashes and independently reconstructs base + overlay.
  & (Join-Path $PSScriptRoot 'setup_memory_tracking_sdk.ps1') -Checkout $sdkPath -SkipDependencies
  if(-not $?) {throw 'Keyboard SDK canonical chain verification failed'}
}
Write-Output 'Keyboard poll-edge SDK overlay verified; runtime keyboard validation remains required'
