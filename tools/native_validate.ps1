<#
Local validation of the native renderer port (docs/native-port-plan.md,
section 8). Nothing here was run by the port's author: it needs the owner's
game files, Windows and a D3D12 GPU. Every step writes to
logs\native_validate\ (ignored by Git); record the results in the plan.

Steps (run one or several, in this order the first time):
  kit      clone crazyriddler/rexglue-native-kit @136bc6c4 (sparse: tools/re,
           tools/binutils, tools/kitcfg.py) into .tools\rexglue-native-kit and
           write its kit.env for this port
  image    run the game once with SR_DUMP_IMAGE to write
           port\logs\default_image.bin, then disassemble it into
           port\logs\default_full.dis (the kit's powerpc objdump)
  sigs     tools/re/xdk_sigs.py match -> xdk_match.tsv (compare with
           docs\data\xdk_match.tsv)
  re       per hook role of game_profile.h: q.py dis/callers/callees of the
           candidate, plus pm4scan.py and a search for the shader container
           magic -> re\<ROLE>.txt. The decision stays manual (plan table 3.1)
  xex      tools\xex_libraries.py game\default.xex (XDK revision)
  capture  capture build (SR_NATIVE=CAPTURE or RENDERER): tools\bench.ps1 run
           with --sr_native_capture and the shader container dump
           -> logs\native_capture.json, logs\native_shaders\
  shaders  tools\shaders\build_corpus.ps1 on game\ + logs\native_shaders
  ab       RENDERER build: one run with --sr_renderer=native
           --sr_native_ab_mode=true (Xenos renders and presents, native renders
           offscreen) dumping both outputs at -AbSwaps; then
           tools\native_ab_compare.py
  bench    tools\bench.ps1 in xenos and in native mode; flags a native run
           that fell back to xenos
  build    build.cmd with SR_NATIVE=-Native (CAPTURE by default)

Usage:
  powershell -File tools\native_validate.ps1 -Step kit,image,sigs,re,xex
  (edit port\src\native_renderer\game_profile.h with what the reports prove)
  powershell -File tools\native_validate.ps1 -Step build -Native CAPTURE
  powershell -File tools\native_validate.ps1 -Step capture,shaders
  powershell -File tools\native_validate.ps1 -Step build -Native RENDERER
  powershell -File tools\native_validate.ps1 -Step ab,bench
#>
param(
  [Parameter(Mandatory)]
  [ValidateSet('kit', 'image', 'sigs', 're', 'xex', 'capture', 'shaders', 'ab', 'bench', 'build')]
  [string[]]$Step,
  [ValidateSet('CAPTURE', 'RENDERER')] [string]$Native = 'CAPTURE',
  [string]$AbSwaps = '300,900,1800,2700,3600,4500',
  [int]$ImageTimeout = 90
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'logs\native_validate'
$kit = Join-Path $root '.tools\rexglue-native-kit'
$kitCommit = '136bc6c4'
$portLogs = Join-Path $root 'port\logs'
$image = Join-Path $portLogs 'default_image.bin'
$dis = Join-Path $portLogs 'default_full.dis'
$register = Join-Path $root 'port\generated\default\superman_returns_register.cpp'
$profile = Join-Path $root 'port\src\native_renderer\game_profile.h'
$exe = Join-Path $root 'port\out\build\win-amd64-release\superman_returns.exe'
New-Item -ItemType Directory -Force $out, $portLogs | Out-Null

function Note($message) {
  $line = '{0:yyyy-MM-dd HH:mm:ss} {1}' -f (Get-Date), $message
  Write-Output $line
  $line | Out-File -Append -Encoding utf8 (Join-Path $out 'validate.log')
}

function Get-PythonExe() {
  $p = Get-Command python, python3 -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $p) { throw 'Python 3 not found on PATH' }
  return $p.Source
}

function Run($exe, [string[]]$arguments, [string]$log) {
  if ($log) {
    & $exe @arguments 2>&1 | Tee-Object -FilePath $log
  } else {
    & $exe @arguments
  }
  if ($LASTEXITCODE -ne 0) { throw "$exe failed ($LASTEXITCODE): $($arguments -join ' ')" }
}

# First recompiled code address, for the kit's disassembly tools (CODE_START).
function Set-CodeStart() {
  $init = Get-ChildItem (Join-Path $root 'port\generated\default') -Filter '*.h' -ErrorAction SilentlyContinue |
    Select-String -Pattern 'REX_CODE_BASE\s+(0x[0-9A-Fa-f]+)' | Select-Object -First 1
  if ($init) { $env:CODE_START = $init.Matches[0].Groups[1].Value }
  else { $env:CODE_START = '0x82000000' }
  Note "CODE_START=$env:CODE_START"
}

# {ROLE, ADDRESS, CONFIRMED} from game_profile.h.
function Get-ProfileRoles() {
  $text = Get-Content $profile -Raw
  $roles = @()
  foreach ($m in [regex]::Matches($text, '#define SR_ADDR_([A-Z0-9_]+) ([0-9A-Fa-f]{8})')) {
    $role = $m.Groups[1].Value
    $confirmed = [regex]::Match($text, "#define SR_CONFIRMED_$role (\d)").Groups[1].Value
    $roles += [pscustomobject]@{ Role = $role; Address = $m.Groups[2].Value; Confirmed = $confirmed }
  }
  return $roles
}

function Step-Kit() {
  if (-not (Test-Path (Join-Path $kit '.git'))) {
    Run git @('clone', '--filter=blob:none', '--no-checkout',
              'https://github.com/crazyriddler/rexglue-native-kit.git', $kit)
  }
  Run git @('-C', $kit, 'sparse-checkout', 'set', '--no-cone', '/tools/re/', '/tools/binutils/', '/tools/kitcfg.py')
  Run git @('-C', $kit, '-c', 'advice.detachedHead=false', 'checkout', $kitCommit)
  $portDir = (Join-Path $root 'port') -replace '\\', '/'
  @(
    '# Written by tools/native_validate.ps1 for the Superman Returns port.',
    'GAME_NAME=superman_returns',
    "PORT_DIR=$portDir",
    'TITLE_ID=454107ED',
    'GUEST_WIDTH=1280',
    'GUEST_HEIGHT=720'
  ) | Set-Content -Encoding ascii (Join-Path $kit 'kit.env')
  Run (Get-PythonExe) @('-m', 'pip', 'install', '--quiet', 'numpy', 'xxhash')
  Note "kit ready at $kit ($kitCommit)"
}

function Step-Image() {
  if (-not (Test-Path $exe)) { throw 'Build the game first (build.cmd)' }
  Get-Process superman_returns -ErrorAction SilentlyContinue | Stop-Process -Force
  Remove-Item $image -ErrorAction SilentlyContinue
  $env:SR_DUMP_IMAGE = $image
  try {
    $proc = Start-Process -FilePath $exe -PassThru -WorkingDirectory (Split-Path $exe) -ArgumentList @(
      "--game_data_root=`"$root\game`"", "--log_file=`"$root\logs\game.log`"", '--sr_skip_intro=true')
    $deadline = (Get-Date).AddSeconds($ImageTimeout)
    $size = -1
    while ((Get-Date) -lt $deadline) {
      Start-Sleep 2
      if (Test-Path $image) {
        $now = (Get-Item $image).Length
        if ($now -gt 0 -and $now -eq $size) { break }
        $size = $now
      }
    }
  } finally {
    Remove-Item Env:SR_DUMP_IMAGE -ErrorAction SilentlyContinue
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
  }
  if (-not (Test-Path $image)) { throw 'The game did not write the image (SR_DUMP_IMAGE)' }
  $objdump = Join-Path $kit 'tools\binutils\powerpc-none-elf-objdump.exe'
  if (-not (Test-Path $objdump)) { throw 'Run -Step kit first' }
  & $objdump -D -b binary -m powerpc -EB --adjust-vma=0x82000000 $image | Out-File -Encoding ascii $dis
  if ($LASTEXITCODE -ne 0) { throw 'objdump failed' }
  Remove-Item (Join-Path $portLogs 'disdb.pkl') -ErrorAction SilentlyContinue
  Note "image $((Get-Item $image).Length) bytes, disassembly $dis"
}

function Step-Sigs() {
  $tsv = Join-Path $out 'xdk_match.tsv'
  Run (Get-PythonExe) @((Join-Path $kit 'tools\re\xdk_sigs.py'), 'match', $image, $register, '-o', $tsv)
  $committed = Get-Content (Join-Path $root 'docs\data\xdk_match.tsv')
  $fresh = Get-Content $tsv
  $diff = Compare-Object $committed $fresh
  Note "xdk_sigs: $tsv; $(@($diff).Count) lines differ from docs\data\xdk_match.tsv"
}

function Step-Re() {
  Set-CodeStart
  $py = Get-PythonExe
  $re = Join-Path $kit 'tools\re'
  $dir = Join-Path $out 're'
  New-Item -ItemType Directory -Force $dir | Out-Null
  Push-Location $re
  try {
    & $py pm4scan.py 2>&1 | Out-File -Encoding utf8 (Join-Path $dir 'pm4scan.txt')
    # 0x102A = 4138: shader container magic (CreateShader candidates).
    & $py q.py grep 'lis\s+r\d+,4138' 2>&1 | Out-File -Encoding utf8 (Join-Path $dir 'container_magic.txt')
    foreach ($r in Get-ProfileRoles) {
      $file = Join-Path $dir "$($r.Role).txt"
      "role $($r.Role) candidate $($r.Address) confirmed=$($r.Confirmed)" | Out-File -Encoding utf8 $file
      foreach ($query in 'dis', 'callers', 'callees') {
        "`n==== q.py $query $($r.Address)" | Out-File -Append -Encoding utf8 $file
        & $py q.py $query $r.Address 2>&1 | Out-File -Append -Encoding utf8 $file
      }
    }
    # Setters that prove the D3DDevice offsets (plan table 3.2).
    $setters = [ordered]@{ SetIndices = '820F2C08'; SetStreamSource = '820F2A50';
      SetTexture = '82100310'; SetRenderTarget = '820F2CA0'; SetDepthStencilSurface = '820F2FD0';
      SetVertexShader = '820F4E88'; SetPixelShader = '820F5218'; SetViewportF = '82102608';
      RingAlloc = '820FC910' }
    foreach ($name in $setters.Keys) {
      & $py q.py dis $setters[$name] 2>&1 | Out-File -Encoding utf8 (Join-Path $dir "setter_$name.txt")
    }
  } finally {
    Pop-Location
  }
  Note "semantic reports in $dir; decide each role by hand (docs/native-port-plan.md 3.1/3.2)"
}

function Step-Xex() {
  Run (Get-PythonExe) @((Join-Path $root 'tools\xex_libraries.py'), (Join-Path $root 'game\default.xex')) (Join-Path $out 'xex_libraries.txt')
}

function Step-Build() {
  $env:SR_NATIVE = $Native
  try {
    & cmd.exe /c (Join-Path $root 'build.cmd')
    if ($LASTEXITCODE -ne 0) { throw "build.cmd failed with SR_NATIVE=$Native" }
  } finally {
    Remove-Item Env:SR_NATIVE -ErrorAction SilentlyContinue
  }
  Note "built with SR_NATIVE=$Native"
}

function Bench($name, $extra) {
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\bench.ps1') -Name $name -ExtraArgs $extra
  if ($LASTEXITCODE -ne 0) { throw "bench.ps1 -Name $name failed" }
  $log = Join-Path $root 'logs\game.log'
  $fallback = Select-String -Path $log -Pattern 'sr_renderer=native.*using the xenos backend' -ErrorAction SilentlyContinue
  if ($fallback) {
    Note "WARNING: run '$name' fell back to xenos: $($fallback[0].Line.Trim()) - its FPS are Xenos numbers"
  }
  Copy-Item $log (Join-Path $out "game_$name.log") -Force
}

function Step-Capture() {
  $capture = Join-Path $root 'logs\native_capture.json'
  $shaders = Join-Path $root 'logs\native_shaders'
  Bench 'native_capture' "--sr_native_capture=true --sr_native_capture_out=$capture --sr_native_dump_shader_dir=$shaders"
  if (-not (Test-Path $capture)) { throw "No ${capture}: is this a SR_NATIVE=CAPTURE build with confirmed hooks?" }
  Copy-Item $capture $out -Force
  $count = @(Get-ChildItem $shaders -Filter '*.bin' -ErrorAction SilentlyContinue).Count
  Note "capture: $capture; $count shader containers in $shaders"
}

function Step-Shaders() {
  & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\shaders\build_corpus.ps1') `
    -DumpDir (Join-Path $root 'logs\native_shaders')
  if ($LASTEXITCODE -ne 0) { throw 'build_corpus.ps1 failed' }
  Note 'shader corpus: artifacts\shaders\catalog.json / SHADER_CATALOG.md'
}

function Step-Ab() {
  $dump = Join-Path $out 'ab'
  Remove-Item $dump -Recurse -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force $dump | Out-Null
  Bench 'native_ab' ("--sr_renderer=native --sr_native_ab_mode=true --sr_native_ab_swaps=$AbSwaps " +
                     "--sr_native_dump_dir=$dump")
  Run (Get-PythonExe) @((Join-Path $root 'tools\native_ab_compare.py'), $dump, '--ppm') (Join-Path $out 'ab_psnr.txt')
}

function Step-Bench() {
  Bench 'xenos_ref' ''
  Bench 'native' '--sr_renderer=native'
  $rows = Import-Csv (Join-Path $root 'logs\bench_results.csv') | Select-Object -Last 4
  $rows | Format-Table | Out-String | Tee-Object -FilePath (Join-Path $out 'bench.txt')
  Note 'bench: last 4 rows of logs\bench_results.csv in bench.txt (meta: avg >= 30, min >= 27)'
}

foreach ($s in $Step) {
  Note "== step $s"
  switch ($s) {
    'kit' { Step-Kit }
    'image' { Step-Image }
    'sigs' { Step-Sigs }
    're' { Step-Re }
    'xex' { Step-Xex }
    'build' { Step-Build }
    'capture' { Step-Capture }
    'shaders' { Step-Shaders }
    'ab' { Step-Ab }
    'bench' { Step-Bench }
  }
}
