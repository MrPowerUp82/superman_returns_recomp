param([string]$Emsdk = '', [string]$Out = '', [switch]$SkipEmitter)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Emsdk) { $Emsdk = Join-Path $root '.tools/emsdk' }
if (-not $Out) { $Out = Join-Path $root 'docs/wasm' }
New-Item -ItemType Directory -Force $Out | Out-Null
if (-not $SkipEmitter) {
  & python (Join-Path $PSScriptRoot 'prepare_vulkan_emitter.py')
  if ($LASTEXITCODE) { throw 'Vulkan emitter preparation failed' }
  $tree = Join-Path $root 'build/vulkan-m2/emitter-tree'
  $args = @('-std=c++20', '-O2', '-UNDEBUG', '-DFMT_HEADER_ONLY', '-DCONAN_RECOMP',
    '-I', "$tree/src/XenosRecomp", '-I', "$tree/deps/fmt/include", '-I', "$tree/deps/xxHash",
    '-include', "$PSScriptRoot/xenosrecomp/pch_corpus.h", '-fms-extensions',
    '-Wno-switch', '-Wno-null-arithmetic', '-Wno-deprecated-declarations',
    "$PSScriptRoot/xenosrecomp/corpus_main.cpp", "$tree/src/XenosRecomp/shader_recompiler.cpp",
    '-o', "$Out/hlsl.mjs", '-sEXPORT_ES6=1', '-sMODULARIZE=1', '-sINVOKE_RUN=0',
    '-sEXIT_RUNTIME=0', '-sALLOW_MEMORY_GROWTH=1', '-sSTACK_SIZE=4MB',
    '-sEXPORTED_RUNTIME_METHODS=FS,callMain', '-sENVIRONMENT=web,worker,node')
  $env:EMSDK = $Emsdk
  & (Join-Path $Emsdk 'upstream/emscripten/em++.bat') @args
  if ($LASTEXITCODE) { throw 'WebAssembly emitter build failed' }
  Copy-Item "$tree/src/XenosRecomp/shader_common.h" "$Out/shader_common.h"
}
# Generic DXC frontend: passes the production argument list verbatim, unlike
# NFSMW's fixed Vulkan 1.2 wrapper. Pin both glue and binary from one revision.
$revision = 'c3768aa53f54257ba5a14a7dea185227150e0499'
$base = "https://raw.githubusercontent.com/kaltinril/ShadowDusk/$revision/.wasm-build/dxc-wasm-out"
Invoke-WebRequest "$base/dxcompiler.js" -OutFile "$Out/dxcompiler.mjs"
Invoke-WebRequest "$base/dxcompiler.wasm" -OutFile "$Out/dxcompiler.wasm"
$expected = @{
  'dxcompiler.mjs' = 'E24C0D83545FCF198EEBF1DA5BA55E3BFEB790CEC2F942AB9E41692FBAEB5FCB'
  'dxcompiler.wasm' = 'C65696F95E5AEB9E28DE318AC74A7940143F40133F403669665C82B4F0170AB0'
}
foreach ($name in $expected.Keys) {
  if ((Get-FileHash "$Out/$name" -Algorithm SHA256).Hash -ne $expected[$name]) { throw "Unexpected compiler hash: $name" }
}
Write-Host "WebAssembly tools ready in $Out (game data not included)"
