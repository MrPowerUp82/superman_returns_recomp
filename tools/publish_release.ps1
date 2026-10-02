<#
Publishes the package made by tools\package_release.ps1:
  1. the orphan `builds` branch (one commit, force-pushed, so the history never
     grows): raw.githubusercontent.com serves it with CORS headers, which is how
     the installer page (docs/) downloads the build in the browser;
  2. a GitHub release with the same zip (gh CLI, optional).

Publishing puts a build derived from the game's code on a public repository:
run it on purpose.

  powershell -File tools\publish_release.ps1 -Confirm
#>
param(
  [switch]$Confirm,
  [switch]$NoRelease,
  [string]$Remote = 'origin'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$release = Join-Path $root 'artifacts\release'
$zip = Join-Path $release 'superman_returns_win64.zip'
if (-not (Test-Path $zip)) { throw "$zip is missing: run tools\package_release.ps1 first." }
$info = Get-Content (Join-Path $release 'version.json') -Raw | ConvertFrom-Json
if ($info.version -like '*-dirty') { throw "The package was built from uncommitted changes ($($info.version)). Commit, rebuild and package again." }
$url = (git -C $root remote get-url $Remote).Trim()
if (-not $Confirm) {
  "Would publish $($info.version) ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB) to the builds branch of $url"
  'Run again with -Confirm.'
  return
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("sr-builds-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
try {
  Copy-Item $zip $work
  Copy-Item (Join-Path $release 'superman_returns_win64.zip.sha256') $work
  Copy-Item (Join-Path $release 'version.json') $work
  "Build for the installer page (docs/). Replaced on every release; see the Releases page for the same file.`n" |
    Set-Content (Join-Path $work 'README.md')
  git -C $work init -q -b builds
  git -C $work add -A
  git -C $work -c user.name=release -c user.email=release@users.noreply.github.com commit -q -m "Build $($info.version)"
  git -C $work push -f $url builds:builds
  if ($LASTEXITCODE -ne 0) { throw 'push to the builds branch failed' }
} finally {
  Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not $NoRelease) {
  $tag = "v$($info.version)"
  gh release create $tag $zip (Join-Path $release 'superman_returns_win64.zip.sha256') `
    --title "Build $($info.version)" `
    --notes "Windows build for the installer page. The package contains no game files and no translated shaders: use the installer (GitHub Pages) with your own copy of the game."
}
"Published $($info.version)."
