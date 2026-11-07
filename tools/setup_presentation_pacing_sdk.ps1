param([Parameter(Mandatory=$true)][string]$Checkout,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$toolsRoot=[IO.Path]::GetFullPath((Join-Path $root '.tools'))
$sdkPath=[IO.Path]::GetFullPath($Checkout)
if(-not $sdkPath.StartsWith($toolsRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Select an isolated SDK checkout inside this repository .tools directory'}
if($sdkPath -eq (Join-Path $toolsRoot 'rexglue-sdk-source')) {throw 'The original SDK checkout is preserved; select the isolated object-motion SDK checkout'}
$ancestor=$sdkPath
while($ancestor.Length -gt $toolsRoot.Length) {
  if((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'SDK checkout must not traverse a junction or symbolic link'}
  $ancestor=Split-Path $ancestor -Parent
}
$patch=Join-Path $PSScriptRoot 'patches/rexglue/v0.10.0/0003-native-presentation-pacing.patch'
$source=Join-Path $sdkPath 'src/ui/d3d12/d3d12_presenter.cpp'
$canonical=Get-Content -LiteralPath ($patch -replace '\.patch$','.manifest.json') -Raw | ConvertFrom-Json
$marker=Join-Path $sdkPath '.sr-presentation-pacing.json'
function TextHash([string]$Path) {
  $bytes=[Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($Path).Replace("`r`n","`n"))
  $sha=[Security.Cryptography.SHA256]::Create()
  try {return [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-','')}
  finally {$sha.Dispose()}
}
function SafeFile([string]$Relative) {
  $path=Join-Path $sdkPath $Relative
  while($path.Length -gt $sdkPath.Length) {
    if((Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw "SDK overlay path traverses a link: $Relative"}
    $path=Split-Path $path -Parent
  }
}
$patchHasher=[Security.Cryptography.SHA256]::Create()
try {$actualPatchHash=[BitConverter]::ToString($patchHasher.ComputeHash([IO.File]::ReadAllBytes($patch))).Replace('-','')}
finally {$patchHasher.Dispose()}
if($actualPatchHash -ne $canonical.patch_sha256) {throw 'Pacing patch does not match its versioned manifest'}
if((TextHash (Join-Path $root 'port/src/graphics/presentation_pacing.h')) -ne $canonical.files.'include/rex/ui/presentation_pacing.h') {throw 'Project pacing helper and SDK overlay differ; regenerate versioned patch before building'}
$revision=(& git -C $sdkPath rev-parse HEAD) -join ''
if($LASTEXITCODE -or $revision.Trim() -ne $canonical.revision) {throw 'Pacing SDK revision differs from pinned v0.10.0'}
foreach($entry in $canonical.files.PSObject.Properties) {SafeFile $entry.Name}
SafeFile '.sr-presentation-pacing.json'
$alreadyPatched=(TextHash $source) -eq $canonical.files.'src/ui/d3d12/d3d12_presenter.cpp'
if(-not $alreadyPatched) {
  if($VerifyOnly) {throw 'Presentation pacing SDK patch is missing or has unknown changes'}
  if((TextHash $source) -ne $canonical.original_source_sha256) {throw 'Presenter has unknown local changes; nothing overwritten'}
  & git -c core.autocrlf=false -C $sdkPath apply --check $patch
  if($LASTEXITCODE) {throw 'SDK pacing patch preflight failed; nothing overwritten'}
  & git -c core.autocrlf=false -C $sdkPath apply $patch
  if($LASTEXITCODE) {throw 'Cannot apply SDK presentation patch'}
}
foreach($entry in $canonical.files.PSObject.Properties) {
  if((TextHash (Join-Path $sdkPath $entry.Name)) -ne $entry.Value) {throw "Pacing SDK canonical content mismatch: $($entry.Name)"}
}
$serialized=$canonical | ConvertTo-Json -Depth 4
if($VerifyOnly) {
  if(-not (Test-Path -LiteralPath $marker)) {throw 'Pacing SDK manifest is missing'}
  $recorded=Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
  if(($recorded | ConvertTo-Json -Depth 4) -cne $serialized) {throw 'Pacing SDK manifest is outdated'}
} elseif(-not (Test-Path -LiteralPath $marker) -or [IO.File]::ReadAllText($marker).TrimEnd() -cne $serialized.TrimEnd()) {
  $serialized | Set-Content -LiteralPath $marker -Encoding UTF8
}
Write-Output 'Presentation pacing SDK overlay verified; rebuild this isolated SDK if it was newly applied'
