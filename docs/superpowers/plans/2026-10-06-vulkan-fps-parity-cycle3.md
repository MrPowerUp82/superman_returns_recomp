# Vulkan FPS Parity, Ciclo 3 — Plano de Implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: use superpowers:subagent-driven-development (recomendada) ou superpowers:executing-plans para executar este plano tarefa por tarefa. Os passos usam caixas de seleção (`- [ ]`).

**Objetivo:** levar o renderer nativo Vulkan a média ≥ 29 FPS (parado e andando, duas execuções seguidas) na Intel UHD deste notebook, sem mudar a imagem e sem relaxar a verificação exata de texturas.

**Arquitetura:** primeiro um gate automático de imagem (a rede de segurança que faltou no ciclo 2); depois uma dieta de CPU por draw na gravação (constantes escritas uma vez na arena, posse de recursos por submissão sem repetição, filtro de comandos redundantes); por fim o hash das texturas mais rápido (XXH3 com AVX2) e, só se faltar, em paralelo. Cada tarefa é um commit próprio, medido contra o custo do **seu** estágio.

**Stack:** C++20 (clang), Vulkan 1.3 carregado dinamicamente, CMake/Ninja, PowerShell 5.1 (`System.Drawing`), harness de testes próprio (`SR_TEST`/`SR_CHECK`).

Spec: `docs/superpowers/specs/2026-10-06-vulkan-fps-parity-cycle3-design.md`.

## Restrições globais

- **Imagem idêntica.** Só otimização interna de CPU. Nenhum atraso em texturas dinâmicas.
- **Texturas exatas a cada quadro**: sem `texture_watch_`, sem backoff, sem vigilância de páginas. O hash de cada textura nova no quadro continua sendo o XXH3 com seed encadeado `0xcbf29ce484222325` (o `content_hash` guardado continua válido).
- **O renderer nativo nunca cai sozinho para o Xenos.** Não mexer na seleção de backend.
- **D3D12 não pode regredir.** Código compartilhado (`native_renderer.cpp`) só muda no caminho `!texture_watch_`.
- **Não ligar `texture_watch_` no Vulkan** (o ciclo 2 fechou o jogo com `0xC0000005` por isso).
- **Nunca tirar a posse de um recurso sem substituto**: todo recurso que um draw grava passa por `Hold` antes de os comandos desse draw serem gravados (o ciclo 2 corrompeu a imagem por isso).
- Meta final: média ≥ 29 FPS parado e andando, em duas execuções seguidas do `tools\bench\bench_api.ps1`. Se depois da Tarefa 5 ainda faltar, **parar e perguntar** antes de qualquer gravação paralela.
- O bench toma o teclado e o mouse: **não tocar neles durante uma execução** (~3 min).
- Nada é enviado ao remoto (`git push`) sem o usuário pedir.
- Mensagens de commit terminam com `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Repositório com CRLF (`core.autocrlf=true`): não normalizar finais de linha de arquivos existentes.

## Estrutura de arquivos

| Arquivo | Ação | Responsabilidade |
| --- | --- | --- |
| `tools/bench/image_gate_lib.ps1` | criar | PSNR + distância de histograma entre dois PNGs (`Compare-GateImages`) |
| `tools/bench/image_gate.ps1` | criar | `-Mode record|check` sobre `logs\bench_<Nome>_start.png` e `artifacts\golden\start.png` |
| `tests/tools/test_image_gate.ps1` | criar | testes sintéticos do gate |
| `tools/bench/bench.ps1`, `bench_api.ps1` | modificar | salva `_start.png` na detecção do HUD; `-Gate record|check` |
| `port/src/graphics/vulkan/descriptors.h/.cpp` | modificar | `DrawBindings` sem constantes; `BuildBindings` escreve o bloco de 12 KB no lugar |
| `port/src/graphics/vulkan/resources.h/.cpp` | modificar | `TransientSlice`, `MapTransient`, `FlushTransient`; `SubmissionResources::Hold` e listas reaproveitadas |
| `port/src/graphics/vulkan/descriptor_sets.h/.cpp` | modificar | `Prepare` com a fatia; cache com chave em array fixo, posse por `Hold`, `DescriptorDraw` por valor |
| `port/src/graphics/vulkan/state_shadow.h` | criar | `StateShadow`: último valor emitido de cada comando |
| `port/src/graphics/vulkan/game_renderer.h/.cpp` | modificar | usa as fatias, arrays no stack e o `StateShadow` |
| `port/src/graphics/vulkan/composition.h/.cpp`, `immediate.cpp` | modificar | escrevem gamma/opções na fatia; `DescriptorDraw` por valor |
| `port/src/native_renderer/xxh3_avx2.h/.cpp` | criar | XXH3 compilado com AVX2 (único arquivo com `-mavx2`) |
| `port/src/native_renderer/cpu_features.h` | criar | `Avx2Available()` (sem AVX2 no código) |
| `port/src/native_renderer/job_pool.h` | criar | `JobPool`: helpers com spin curto e sono |
| `port/src/native_renderer/texture_hash_batch.h` | criar | `HashTextureJobs` (serial ou no pool, mesmo valor) |
| `port/src/native_renderer/native_renderer.cpp/.h` | modificar | despacho do hash, pré-passe em `CaptureTextures`, cvar `sr_native_hash_threads` |
| `port/CMakeLists.txt`, `tests/native/CMakeLists.txt`, `tests/vulkan/CMakeLists.txt` | modificar | novos fontes e `-mavx2` só no `xxh3_avx2.cpp` |
| `tests/vulkan/test_descriptors.cpp`, `test_resources.cpp`, `test_state_shadow.cpp`, `resources_integration.cpp`, `shader_contract_integration.cpp` | modificar/criar | testes unitários e fixtures de GPU |
| `tests/native/test_xxh3_avx2.cpp`, `xxh3_reference.cpp`, `test_job_pool.cpp`, `test_texture_hash_batch.cpp` | criar | testes do hash |
| `tools/verify_vulkan_m3.ps1`, `tools/README.md`, `docs/vulkan-m3.md`, `README.md` | modificar | fixture novo, documentação, resultados |

## Comandos padrão (referidos pelas tarefas)

**Testes unitários nativos**

```powershell
. .\tools\dev_env.ps1
cmake -S tests/native -B build/tests-native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build/tests-native
.\build\tests-native\sr_native_tests.exe
```

Esperado: nenhuma linha `FAIL` e a última linha `N tests, 0 failed checks`.

**Testes do Vulkan (unitários + contrato + fixtures de GPU)** — "suíte de regressão Vulkan":

```powershell
powershell -File tools\build_vulkan.ps1
.\build\tests-vulkan\sr_vulkan_tests.exe
.\build\tests-vulkan\sr_vulkan_contract_test.exe --production-bindings
$gpu = '.\build\tests-vulkan\sr_vulkan_resources_test.exe'
& $gpu; if ($LASTEXITCODE) { throw 'sr_vulkan_resources_test falhou' }
foreach ($f in 'targets','pipeline-cache','game-record','game-record-merged','resolve-copy','resolve-record','depth-resolve','edram-alias','alias-defaults','resolve-sample','game-frame','game-frame-async','immediate','stacked-resolve','composition') {
  & $gpu "--$f"; if ($LASTEXITCODE) { throw "fixture $f falhou" }
}
```

Esperado: nenhuma exceção; cada fixture imprime a linha de sucesso e sai com 0. (A Tarefa 2 acrescenta o fixture `hold-lifetime` à lista.) As validation layers não estão instaladas: `validation_errors=0` não prova validação, os fixtures com checagem de pixels e o gate de imagem são a rede.

**Recompilar o jogo e medir** — "passo de medição" (`<tag>` é o rótulo da tarefa):

```powershell
cmd /c "set SR_BUILD_LAUNCHER=OFF&& build.cmd"
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
powershell -File tools\bench\bench_api.ps1 -Api vulkan -Name c3_<tag> -Profile -Gate check -Exe $exe
powershell -File tools\bench\vulkan_profile_summary.ps1 -Log logs\bench_c3_<tag>.log
Select-String -Path logs\bench_results.csv -Pattern 'c3_<tag>'
Select-String -Path logs\bench_c3_<tag>.log -Pattern 'Vulkan profile \(ms/frame' | Select-Object -Last 3
```

A linha `Vulkan profile` traz `draws=N`; compare **µs por draw** (fase em ms ÷ draws × 1000), porque a cena varia entre execuções. Registre as linhas no relatório da tarefa. Não toque no teclado nem no mouse durante o bench.

---

## Tarefa 0: Gate de imagem e linha de base

**Arquivos:**
- Criar: `tools/bench/image_gate_lib.ps1`, `tools/bench/image_gate.ps1`, `tests/tools/test_image_gate.ps1`
- Modificar: `tools/bench/bench.ps1` (função `Save-GameRegion` e captura na detecção do HUD), `tools/bench/bench_api.ps1` (`-Gate`), `tools/README.md`

**Interfaces:**
- Produz: `Compare-GateImages -Reference <png> -Candidate <png> [-MinPsnr] [-MaxHistogram]` → objeto `{Pass, Psnr, Histogram, Reason}`; `image_gate.ps1 -Name <n> -Mode record|check`; `bench_api.ps1 -Gate record|check`; arquivo `logs\bench_<Nome>_start.png` e `artifacts\golden\start.png`.

- [ ] **Passo 1: criar o branch**

```powershell
git switch -c perf/vulkan-fps-parity-cycle3
```

- [ ] **Passo 2: escrever o teste sintético (vai falhar: a biblioteca ainda não existe)**

Criar `tests/tools/test_image_gate.ps1`:

```powershell
# Synthetic checks for the image gate (tools\bench\image_gate_lib.ps1).
# Run: powershell -NoProfile -File tests\tools\test_image_gate.ps1
Add-Type -AssemblyName System.Drawing
. "$PSScriptRoot\..\..\tools\bench\image_gate_lib.ps1"

# The checks pass explicit limits: they test the metrics, not the calibrated defaults.
$limits = @{ MinPsnr = 18.0; MaxHistogram = 0.12 }

$dir = Join-Path ([IO.Path]::GetTempPath()) ("sr-gate-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $dir | Out-Null

# 320x180 scene with different color statistics per channel, so that swapped channels are visible:
# blue sky gradient, brown ground, an orange block and a small red block.
function New-Scene([int]$Width = 320, [int]$Height = 180) {
  $bmp = New-Object System.Drawing.Bitmap $Width, $Height
  for ($y = 0; $y -lt $Height; $y++) {
    for ($x = 0; $x -lt $Width; $x++) {
      if ($y -lt 110) { $c = [System.Drawing.Color]::FromArgb(40 + [int]($y * 0.5), 90 + [int]($y * 0.8), 200 + [int]($y * 0.4)) }
      else { $c = [System.Drawing.Color]::FromArgb(120 + (($x * 3) % 20), 80 + (($x * 2) % 15), 40) }
      $bmp.SetPixel($x, $y, $c)
    }
  }
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  try {
    $orange = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(240, 140, 20))
    $red = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200, 30, 40))
    $gfx.FillRectangle($orange, 60, 40, 90, 60); $gfx.FillRectangle($red, 200, 120, 40, 30)
    $orange.Dispose(); $red.Dispose()
  } finally { $gfx.Dispose() }
  return ,$bmp
}

# Applies a per-pixel transform scriptblock { param($x,$y,$c) -> Color } to a copy of the bitmap.
function Convert-Scene($src, [scriptblock]$fn) {
  $out = New-Object System.Drawing.Bitmap $src.Width, $src.Height
  for ($y = 0; $y -lt $src.Height; $y++) { for ($x = 0; $x -lt $src.Width; $x++) { $out.SetPixel($x, $y, (& $fn $x $y $src.GetPixel($x, $y))) } }
  return ,$out
}

function Save($bmp, $name) { $path = Join-Path $dir $name; $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); return $path }
function Clamp([int]$v) { [math]::Max(0, [math]::Min(255, $v)) }

$failures = 0
function Check($name, [bool]$expectPass, $result, $reasonLike = '') {
  $ok = ($result.Pass -eq $expectPass) -and ($reasonLike -eq '' -or $result.Reason -like "*$reasonLike*")
  $detail = 'psnr={0:N1} hist={1:N3} {2}' -f $result.Psnr, $result.Histogram, $result.Reason
  if ($ok) { Write-Host "PASS $name ($detail)" } else { Write-Host "FAIL $name (expected pass=$expectPass; $detail)"; $script:failures++ }
}

try {
  $scene = New-Scene
  $reference = Save $scene.Clone() 'reference.png'

  # (a) identical image
  Check 'a identical image passes' $true (Compare-GateImages $reference (Save $scene.Clone() 'a.png') @limits)

  # (b) small noise, like the variation between two runs
  $noisy = Convert-Scene $scene { param($x, $y, $c) $n = (($x * 7 + $y * 13) % 7) - 3; [System.Drawing.Color]::FromArgb((Clamp ($c.R + $n)), (Clamp ($c.G - $n)), (Clamp ($c.B + $n))) }
  Check 'b noise of +-3 passes' $true (Compare-GateImages $reference (Save $noisy 'b.png') @limits)

  # (c) scene shifted 4 px to the right: a small camera difference
  $shifted = Convert-Scene $scene { param($x, $y, $c) $sx = [math]::Max(0, $x - 4); $scene.GetPixel($sx, $y) }
  Check 'c 4 px shift passes' $true (Compare-GateImages $reference (Save $shifted 'c.png') @limits)

  # (d) RGB channels permuted: reproduces the cycle 2 corruption
  $permuted = Convert-Scene $scene { param($x, $y, $c) [System.Drawing.Color]::FromArgb($c.B, $c.R, $c.G) }
  Check 'd permuted channels fail' $false (Compare-GateImages $reference (Save $permuted 'd.png') @limits)

  # (e) black frame
  $black = New-Object System.Drawing.Bitmap 320, 180
  $gfx = [System.Drawing.Graphics]::FromImage($black); try { $gfx.Clear([System.Drawing.Color]::Black) } finally { $gfx.Dispose() }
  Check 'e black frame fails' $false (Compare-GateImages $reference (Save $black 'e.png') @limits)

  # (f) different size
  $small = New-Scene 160 90
  Check 'f different size fails' $false (Compare-GateImages $reference (Save $small 'f.png') @limits) 'different size'

  # (g) the files are not left locked after comparing
  Remove-Item (Join-Path $dir 'a.png') -ErrorAction Stop
  Write-Host 'PASS g files are not left locked'

  # (h) the calibrated defaults are not looser than the synthetic corruption
  $defaults = Compare-GateImages $reference (Join-Path $dir 'd.png')
  if (-not $defaults.Pass) { Write-Host 'PASS h default limits reject permuted channels' }
  else { Write-Host 'FAIL h default limits accept permuted channels'; $failures++ }
  $scene.Dispose()
} finally {
  Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
}

if ($failures) { Write-Host "$failures check(s) failed"; exit 1 }
Write-Host 'all image gate checks passed'
```

- [ ] **Passo 3: rodar e confirmar que falha**

Run: `powershell -NoProfile -File tests\tools\test_image_gate.ps1`
Esperado: erro porque `tools\bench\image_gate_lib.ps1` não existe.

- [ ] **Passo 4: criar a biblioteca**

Criar `tools/bench/image_gate_lib.ps1`:

```powershell
# Gate de imagem do bench (tools\bench): compara dois screenshots por PSNR e pela distância entre os histogramas
# de cor. Pega corrupção grosseira (canais trocados, textura preta ou embaralhada), não diferenças de poucos pixels.
# Dot-source: . "$PSScriptRoot\image_gate_lib.ps1"
Add-Type -AssemblyName System.Drawing

# Limites calibrados com a variação natural entre execuções da build boa (veja tools\README.md).
$script:GateMinPsnr = 18.0       # dB; abaixo disso a imagem é considerada diferente demais
$script:GateMaxHistogram = 0.12  # 0 = histogramas iguais, 1 = disjuntos

if (-not ([System.Management.Automation.PSTypeName]'SrImageGate').Type) {
  Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class SrImageGate {
  // Returns { psnr, histogram }. The bitmaps must have the same size.
  public static double[] Compare(Bitmap a, Bitmap b) {
    Rectangle rect = new Rectangle(0, 0, a.Width, a.Height);
    BitmapData da = a.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    BitmapData db = b.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    try {
      int n = a.Width * a.Height * 4;
      byte[] pa = new byte[n], pb = new byte[n];
      Marshal.Copy(da.Scan0, pa, 0, n);
      Marshal.Copy(db.Scan0, pb, 0, n);
      double sum = 0;
      long[,] ha = new long[3, 32], hb = new long[3, 32];
      for (int i = 0; i < n; i += 4) {
        for (int c = 0; c < 3; c++) {
          int va = pa[i + c], vb = pb[i + c], d = va - vb;
          sum += (double)d * d;
          ha[c, va >> 3]++; hb[c, vb >> 3]++;
        }
      }
      double pixels = (double)a.Width * a.Height;
      double mse = sum / (pixels * 3);
      double psnr = mse <= 0 ? 99 : Math.Min(99, 10 * Math.Log10(255.0 * 255.0 / mse));
      double distance = 0;
      for (int c = 0; c < 3; c++) {
        double l1 = 0;
        for (int k = 0; k < 32; k++) l1 += Math.Abs(ha[c, k] - hb[c, k]) / pixels;
        distance += l1 / 2;
      }
      return new double[] { psnr, distance / 3 };
    } finally { a.UnlockBits(da); b.UnlockBits(db); }
  }
}
"@
}

# Loads a PNG without keeping the file locked.
function Read-GateBitmap([string]$Path) {
  $stream = [System.IO.File]::OpenRead($Path)
  try {
    $loaded = New-Object System.Drawing.Bitmap $stream
    try { return ,(New-Object System.Drawing.Bitmap $loaded) } finally { $loaded.Dispose() }
  } finally { $stream.Dispose() }
}

# Compares a candidate screenshot with the reference. Returns Pass, Psnr, Histogram and Reason.
function Compare-GateImages {
  param(
    [Parameter(Mandatory)] [string]$Reference,
    [Parameter(Mandatory)] [string]$Candidate,
    [double]$MinPsnr = $script:GateMinPsnr,
    [double]$MaxHistogram = $script:GateMaxHistogram
  )
  $a = Read-GateBitmap $Reference
  $b = Read-GateBitmap $Candidate
  try {
    if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
      return [pscustomobject]@{ Pass = $false; Psnr = 0.0; Histogram = 1.0
        Reason = "different size ($($a.Width)x$($a.Height) vs $($b.Width)x$($b.Height)); record the reference again if the window size changed" }
    }
    $m = [SrImageGate]::Compare($a, $b)
    $reasons = @()
    if ($m[0] -lt $MinPsnr) { $reasons += ('PSNR {0:N1} dB < {1:N1} dB' -f $m[0], $MinPsnr) }
    if ($m[1] -gt $MaxHistogram) { $reasons += ('histogram distance {0:N3} > {1:N3}' -f $m[1], $MaxHistogram) }
    return [pscustomobject]@{ Pass = ($reasons.Count -eq 0); Psnr = $m[0]; Histogram = $m[1]; Reason = ($reasons -join '; ') }
  } finally { $a.Dispose(); $b.Dispose() }
}
```

- [ ] **Passo 5: rodar o teste (Windows PowerShell 5.1) e confirmar que passa**

Run: `powershell -NoProfile -File tests\tools\test_image_gate.ps1`
Esperado (valores aproximados, vistos ao validar o código): `PASS a`…`PASS h` e `all image gate checks passed`; a permutação de canais dá PSNR ~7,9 dB e histograma ~0,85, o quadro preto PSNR ~5,5 dB, o deslocamento de 4 px PSNR ~24,6 dB.

- [ ] **Passo 6: criar `tools/bench/image_gate.ps1`**

```powershell
<#
Gate de imagem do bench: compara o screenshot do instante em que o HUD é detectado
(logs\bench_<Nome>_start.png, salvo pelo bench.ps1) com uma referência da build boa
(artifacts\golden\start.png, local e ignorada pelo Git).

  -Mode record   grava a referência a partir do screenshot (rode numa build que se sabe boa)
  -Mode check    compara; sai com erro se reprovar

Uso: tools\bench\image_gate.ps1 -Name <nome do bench> -Mode record|check
Normalmente chamado por: tools\bench\bench_api.ps1 -Gate record|check
#>
param(
  [Parameter(Mandatory)] [string]$Name,
  [Parameter(Mandatory)] [ValidateSet('record', 'check')] [string]$Mode
)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. "$PSScriptRoot\image_gate_lib.ps1"
$shot = "$root\logs\bench_${Name}_start.png"
$golden = "$root\artifacts\golden\start.png"
if (-not (Test-Path -LiteralPath $shot)) { throw "Screenshot not found: $shot (the bench must reach gameplay first)" }

if ($Mode -eq 'record') {
  New-Item -ItemType Directory -Force (Split-Path $golden) | Out-Null
  Copy-Item -LiteralPath $shot -Destination $golden -Force
  Write-Output "GATE recorded $golden"
  return
}

if (-not (Test-Path -LiteralPath $golden)) { throw "No reference at ${golden}: run once with -Gate record on a build known to be good" }
$result = Compare-GateImages -Reference $golden -Candidate $shot
$invariant = [cultureinfo]::InvariantCulture
$verdict = if ($result.Pass) { 'PASS' } else { 'FAIL' }
Write-Output ("GATE {0} {1} psnr={2} dB hist={3} {4}" -f $verdict, $Name, $result.Psnr.ToString('0.0', $invariant), $result.Histogram.ToString('0.000', $invariant), $result.Reason)
"{0},{1},{2},{3},{4}" -f (Get-Date -Format s), $Name, $result.Psnr.ToString('0.00', $invariant), $result.Histogram.ToString('0.0000', $invariant), $verdict |
  Add-Content -Encoding utf8 "$root\logs\bench_gate.csv"
if (-not $result.Pass) { throw "Image gate failed for ${Name}: $($result.Reason)" }
```

- [ ] **Passo 7: `bench.ps1` salva o screenshot na detecção do HUD**

Em `tools/bench/bench.ps1`, logo depois da função `Has-Gameplay-Frame` (termina antes do comentário `# Throws (leaving a screenshot) unless the HUD is seen`), inserir:

```powershell
# Saves the game window's client area as PNG (the image gate compares it with a reference).
function Save-GameRegion($proc, $path) {
  $region = Game-Region $proc
  $bmp = New-Object System.Drawing.Bitmap $region.Width, $region.Height
  try {
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    try { $gfx.CopyFromScreen($region.Location, [System.Drawing.Point]::Empty, $region.Size) }
    finally { $gfx.Dispose() }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  } finally { $bmp.Dispose() }
}

```

E, na linha `Step "visible frame detected"`, trocar:

```powershell
  Step "visible frame detected"
  Start-Sleep 5
```

por:

```powershell
  Step "visible frame detected"
  # Initial camera and pose: the one scene that does not vary between runs (the image gate compares it).
  Save-GameRegion $proc "$root\logs\bench_${Name}_start.png"
  Start-Sleep 5
```

- [ ] **Passo 8: `bench_api.ps1 -Gate`**

Em `tools/bench/bench_api.ps1`: no bloco `param(`, acrescentar depois de `[switch]$Profile`:

```powershell
  [switch]$Profile,
  [ValidateSet('', 'record', 'check')] [string]$Gate = ''
```

(trocando a linha `  [switch]$Profile` pela de cima, com a vírgula). No cabeçalho de ajuda, acrescentar a linha `  -Gate    record grava a referência de imagem (build boa); check compara e falha se a imagem divergir (tools\bench\image_gate.ps1).` e trocar a linha de uso por `Uso: tools\bench\bench_api.ps1 -Api vulkan|d3d12 -Name <rótulo> [-Profile] [-Gate record|check] [-Exe <caminho>]`. Dentro do `try`, depois do `if ($Profile) { Copy-Item ... }`, acrescentar:

```powershell
  if ($Gate) { & "$PSScriptRoot\image_gate.ps1" -Name $Name -Mode $Gate }
```

- [ ] **Passo 9: documentar em `tools/README.md`**

Na tabela de `bench/`, acrescentar depois da linha do `bench_hud.ps1`:

```markdown
| `image_gate.ps1`, `image_gate_lib.ps1` | Gate de imagem: compara o screenshot do instante em que o HUD é detectado (`logs/bench_<Nome>_start.png`) com a referência local `artifacts/golden/start.png` por PSNR e distância de histograma. `bench_api.ps1 -Gate record` grava a referência numa build boa; `-Gate check` compara. Pega corrupção grosseira, não diferenças de poucos pixels. Teste em `tests/tools/test_image_gate.ps1`. |
```

- [ ] **Passo 10: commit do gate (sem calibração ainda)**

```powershell
git add tools/bench/image_gate_lib.ps1 tools/bench/image_gate.ps1 tests/tools/test_image_gate.ps1 tools/bench/bench.ps1 tools/bench/bench_api.ps1 tools/README.md
git commit -m "feat(bench): image gate that compares the screenshot at HUD detection with a golden reference

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Passo 11: recompilar o jogo na HEAD (build boa) e gravar a referência**

Siga o passo de medição (recompilar), depois:

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
powershell -File tools\bench\bench_api.ps1 -Api vulkan -Name c3_gate_ref -Profile -Gate record -Exe $exe
```

Esperado: o bench termina, `GATE recorded ...\artifacts\golden\start.png`. Abra `artifacts\golden\start.png` e confirme que é a cena inicial do jogo com o HUD (duas barras finas azul e vermelha no canto) e imagem correta. Se a imagem não for gameplay, repita.

Se o jogo não abrir em Vulkan com `win-amd64-release`, use `win-amd64-dist` (o padrão do `bench_api.ps1`) e use o mesmo executável em **todas** as tarefas.

- [ ] **Passo 12: calibrar o limite com a variação natural**

```powershell
powershell -File tools\bench\bench_api.ps1 -Api vulkan -Name c3_base -Profile -Gate check -Exe $exe
powershell -File tools\bench\bench_api.ps1 -Api vulkan -Name c3_base2 -Profile -Gate check -Exe $exe
```

Anote as duas linhas `GATE PASS/FAIL ... psnr=... hist=...` (também em `logs\bench_gate.csv`). O padrão atual (18 dB, 0,12) pode reprovar a build boa. Regra de calibração:

- `GateMinPsnr` = menor PSNR observado − 6 dB (arredonde para baixo), mas nunca abaixo de 15.
- `GateMaxHistogram` = maior distância observada × 2 + 0,03 (arredonde para 2 casas).

Se o PSNR entre duas execuções boas ficar abaixo de 20 dB, o gate não distingue ruído de corrupção: **pare e reporte** (a cena do instante do HUD não é estável o suficiente).

Edite as duas variáveis no topo de `tools/bench/image_gate_lib.ps1`, rode `powershell -NoProfile -File tests\tools\test_image_gate.ps1` (o caso `h` precisa continuar passando: os limites calibrados rejeitam a permutação de canais), e confirme `-Gate check` aprovando as duas execuções.

- [ ] **Passo 13: linha de base de custo e commit da calibração**

Registre no relatório o `Vulkan profile` e a captura de `c3_base` e `c3_base2` (µs por draw de `bindings`, `descriptors`, `commands`; `textures_ms`, `hash_ms`) e os FPS do `bench_results.csv`. Essa é a linha de base de comparação das tarefas seguintes.

```powershell
git add tools/bench/image_gate_lib.ps1
git commit -m "chore(bench): calibrate the image gate limits against two good runs

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Tarefa 1: Constantes sem cópias e bindings sem alocação

Hoje cada draw copia 12 KB quatro vezes (`BuildBindings`, a cópia de `RemapTextureBindings`, o `move` dela, o `UploadTransient`). Depois: uma cópia, direto na arena.

**Arquivos:**
- Modificar: `port/src/graphics/vulkan/descriptors.h`, `descriptors.cpp`, `resources.h`, `resources.cpp`, `descriptor_sets.h`, `descriptor_sets.cpp`, `game_renderer.cpp`, `composition.cpp`, `immediate.cpp`
- Testes: `tests/vulkan/test_descriptors.cpp`, `tests/vulkan/test_resources.cpp`, `tests/vulkan/shader_contract_integration.cpp`

**Interfaces:**
- Produz: `struct TransientSlice {std::shared_ptr<BufferResource> chunk;VkDeviceSize offset=0,size=0;std::byte* data=nullptr;}`; `bool ResourceStore::MapTransient(VkDeviceSize size,TransientSlice&,Error&)`; `bool ResourceStore::FlushTransient(const TransientSlice&,Error&)`; `bool BuildBindings(const guest::DrawPacket&,std::byte* block,DrawBindings&,Error&)` (`block` tem `sizeof(guest::ConstantSnapshot)` = 12288 bytes); `inline constexpr size_t kSharedConstantsOffset`; `DescriptorStore::Prepare(const DrawBindings&,const std::array<std::array<uint32_t,6>,32>& fetch,ResourceStore&,uint64_t serial,const TransientSlice& constants,Error&)` → `std::shared_ptr<DescriptorDraw>` (a Tarefa 2 muda o retorno).
- `DrawBindings` perde o campo `constants`.

- [ ] **Passo 1: testes que falham — `tests/vulkan/test_descriptors.cpp`**

Substituir as linhas 1–40 (do início do arquivo até o fim do teste `vertex_descriptor_remap_preserves_ushort2_and_stream_offset`) por:

```cpp
#include "descriptors.h"
#include "test_main.h"
#include <cstring>
using namespace superman_returns::graphics::vulkan;
namespace guest=superman_returns::graphics::guest;
namespace {
// The 12 KiB constant block of one draw (vs, ps, shared), as MapTransient hands it out.
struct Block {
  alignas(16) std::array<std::byte,sizeof(guest::ConstantSnapshot)> bytes{};
  std::byte* data() {return bytes.data();}
  const std::byte* shared() const {return bytes.data()+kSharedConstantsOffset;}
};
}
SR_TEST(descriptor_arrays_accept_32_slots_and_reject_33) {
  std::vector<TextureBindingRequest> requests;
  for(uint32_t i=0;i<32;++i) requests.push_back({i,1500+i,TextureDimension::k2D,0});
  DrawBindings bindings;Error e;
  SR_CHECK(RemapTextureBindings(requests,bindings,e));
  SR_CHECK_EQ(bindings.textures[0][31],1531u);
  requests.push_back({32,1600,TextureDimension::k2D,0});
  SR_CHECK(!RemapTextureBindings(requests,bindings,e));
}
SR_TEST(descriptors_remap_global_ids_keep_flags_and_dimension_dummies) {
  DrawBindings b;Error e;
  std::vector<TextureBindingRequest> requests{{7,1500,TextureDimension::k3D,0x80000000u},
                                             {9,1501,TextureDimension::kCube,0}};
  SR_CHECK(RemapTextureBindings(requests,b,e));
  SR_CHECK_EQ(b.texture_indices[7],0x80000000u);
  SR_CHECK_EQ(b.texture_indices[9],0u);
  SR_CHECK_EQ(b.textures[1][0],1500u);
  SR_CHECK_EQ(b.textures[2][0],1501u);
  SR_CHECK_EQ(b.textures[0][0],DummyTexture(TextureDimension::k2D));
  SR_CHECK_EQ(b.textures[1][31],DummyTexture(TextureDimension::k3D));
  requests.push_back(requests[0]);
  SR_CHECK(!RemapTextureBindings(requests,b,e));
}
SR_TEST(remap_failure_leaves_the_bindings_untouched) {
  DrawBindings b;Error e;
  std::vector<TextureBindingRequest> good{{3,1500,TextureDimension::k2D,0}};
  SR_CHECK(RemapTextureBindings(good,b,e));
  const DrawBindings before=b;
  std::vector<TextureBindingRequest> bad{{4,1600,TextureDimension::k2D,0},{4,1601,TextureDimension::k2D,0}};  // duplicate slot
  SR_CHECK(!RemapTextureBindings(bad,b,e));
  SR_CHECK(std::memcmp(&before,&b,sizeof(DrawBindings))==0);
  std::vector<TextureBindingRequest> zero{{5,0,TextureDimension::k2D,0}};  // no resource
  SR_CHECK(!RemapTextureBindings(zero,b,e));
  SR_CHECK(std::memcmp(&before,&b,sizeof(DrawBindings))==0);
}
SR_TEST(vertex_descriptor_remap_preserves_ushort2_and_stream_offset) {
  guest::DrawPacket p;
  p.vertex_fetch[48]={7,14,4,0x2C2259};
  guest::VertexStream stream{};stream.stream=7;stream.offset=12;stream.size=16;stream.stride=4;stream.update.plan.key=1500;
  p.streams.push_back(stream);
  Block block;DrawBindings b;Error e;
  SR_CHECK(BuildBindings(p,block.data(),b,e));
  SR_CHECK_EQ(b.vertex_buffers[0],1500u);
  guest::VertexFetchMeta meta{};std::memcpy(&meta,block.shared()+512+48*16,16);
  SR_CHECK_EQ(meta.buffer,0u);SR_CHECK_EQ(meta.offset,14u);SR_CHECK_EQ(meta.type,0x2C2259u);
  SR_CHECK_EQ(b.vertex_buffers[31],DummyBuffer);
}
SR_TEST(build_bindings_writes_vs_ps_and_patches_shared_in_the_block) {
  guest::DrawPacket p;
  p.constants.vs[3]=0x11111111;p.constants.ps[5]=0x22222222;p.constants.shared[300]=0x7e;  // 300 is outside the patched ranges
  p.texture_fetch[7][0]=2;p.texture_fetch[7][5]=1<<9;p.texture_fetch[7][1]=0x1006;
  Block block;DrawBindings b;Error e;
  SR_CHECK(BuildBindings(p,block.data(),b,e));
  uint32_t word=0;
  std::memcpy(&word,block.data()+3*4,4);SR_CHECK_EQ(word,0x11111111u);
  std::memcpy(&word,block.data()+4096+5*4,4);SR_CHECK_EQ(word,0x22222222u);
  SR_CHECK_EQ(uint32_t(block.shared()[300]),0x7eu);
  std::memcpy(&word,block.shared()+7*4,4);SR_CHECK_EQ(word,b.texture_indices[7]);   // texture index of slot 7
  std::memcpy(&word,block.shared()+128+7*4,4);SR_CHECK_EQ(word,7u);                // sampler index of slot 7
  SR_CHECK_EQ(b.sampler_indices[7],7u);
}
SR_TEST(build_bindings_rejects_duplicate_and_missing_streams) {
  Block block;DrawBindings b;Error e;
  guest::VertexStream stream{};stream.stream=2;stream.size=16;stream.stride=4;stream.update.plan.key=1500;
  guest::DrawPacket duplicate;duplicate.streams={stream,stream};
  SR_CHECK(!BuildBindings(duplicate,block.data(),b,e));
  guest::DrawPacket no_key;stream.update.plan.key=0;no_key.streams={stream};
  SR_CHECK(!BuildBindings(no_key,block.data(),b,e));
  guest::DrawPacket missing;stream.update.plan.key=1500;missing.streams={stream};
  missing.vertex_fetch[0]={9,0,4,0x2C2259};   // references stream 9, which the draw does not have
  SR_CHECK(!BuildBindings(missing,block.data(),b,e));
  guest::DrawPacket out_of_range;out_of_range.vertex_fetch[0]={40,0,4,0x2C2259};
  SR_CHECK(!BuildBindings(out_of_range,block.data(),b,e));
}
```

Manter sem mudanças o teste `sampler_plan_preserves_fetch_filters_and_address_modes` e o `resource_id_bases_do_not_collide`. Substituir o teste `draw_bindings_gamma_matches_reference_rgb_sign_rule` (hoje linhas 63–69) pela versão adaptada:

```cpp
SR_TEST(draw_bindings_gamma_matches_reference_rgb_sign_rule) {
  guest::DrawPacket packet;packet.texture_fetch[0][0]=2|(3<<2);packet.texture_fetch[0][5]=1<<9;
  Block block;DrawBindings bindings;Error e;SR_CHECK(BuildBindings(packet,block.data(),bindings,e));
  SR_CHECK_EQ(bindings.texture_indices[0]&0x80000000u,0u);
  packet.texture_fetch[0][0]|=(3<<4)|(3<<6);
  SR_CHECK(BuildBindings(packet,block.data(),bindings,e));SR_CHECK_EQ(bindings.texture_indices[0]&0x80000000u,0x80000000u);
}
```

O arquivo final tem 9 testes: `descriptor_arrays_...`, `descriptors_remap_...`, `remap_failure_...`, `vertex_descriptor_remap_...`, `build_bindings_writes_...`, `build_bindings_rejects_...`, `sampler_plan_...`, `draw_bindings_gamma_...` e `resource_id_bases_...`.

- [ ] **Passo 2: teste que falha — `tests/vulkan/test_resources.cpp`**

Acrescentar ao fim do arquivo:

```cpp
SR_TEST(map_transient_hands_out_mapped_slices_of_one_chunk_and_flushes_non_coherent_memory) {
  allocations.clear();copies=flushes=destroyed=0;
  Context c(ResourceFake());c.device=reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;  // not coherent
  c.properties.limits.maxStorageBufferRange=4096;c.properties.limits.nonCoherentAtomSize=64;c.properties.limits.minStorageBufferOffsetAlignment=16;
  {
    ResourceStore store(c);Error e;TransientSlice a,b;
    SR_CHECK(!store.MapTransient(16,a,e));                       // no recording submission yet
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),1,e));
    SR_CHECK(!store.MapTransient(0,a,e));SR_CHECK(!store.MapTransient(5000,a,e));  // empty and over maxStorageBufferRange
    SR_CHECK(store.MapTransient(16,a,e));SR_CHECK(store.MapTransient(16,b,e));
    SR_CHECK(a.data!=nullptr);SR_CHECK(a.chunk==b.chunk);
    SR_CHECK_EQ(a.offset,0u);SR_CHECK_EQ(b.offset,64u);          // aligned to the non-coherent atom size
    SR_CHECK_EQ(size_t(b.data-a.data),size_t(64));
    std::memset(a.data,0x5a,16);                                  // the slice is the mapped memory itself
    SR_CHECK_EQ(allocations[a.chunk->memory][0],0x5au);
    SR_CHECK(store.FlushTransient(a,e));SR_CHECK_EQ(flushes,1);   // FakeFlush checks offset 0, size 64
    store.Retire(1);
  }
  SR_CHECK(allocations.empty());
}
SR_TEST(map_transient_skips_the_flush_on_coherent_memory) {
  allocations.clear();copies=flushes=destroyed=0;
  Context c(ResourceFake());c.device=reinterpret_cast<VkDevice>(1);
  c.memory.memoryTypeCount=1;c.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  c.properties.limits.maxStorageBufferRange=4096;c.properties.limits.nonCoherentAtomSize=64;
  {
    ResourceStore store(c);Error e;TransientSlice a;
    SR_CHECK(store.BeginSubmission(reinterpret_cast<VkCommandBuffer>(1),1,e));
    SR_CHECK(store.MapTransient(16,a,e));SR_CHECK(store.FlushTransient(a,e));SR_CHECK_EQ(flushes,0);
    store.Retire(1);
  }
  SR_CHECK(allocations.empty());
}
```

- [ ] **Passo 3: rodar e confirmar a falha**

Run: `powershell -File tools\build_vulkan.ps1`
Esperado: erro de compilação (`BuildBindings` com assinatura nova, `kSharedConstantsOffset`, `TransientSlice`, `MapTransient` inexistentes).

- [ ] **Passo 4: `descriptors.h`**

Substituir o arquivo inteiro por:

```cpp
#pragma once
#include "../guest/render_packet.h"
#include "loader.h"
#include <cstddef>
#include <span>
namespace superman_returns::graphics::vulkan {
enum class TextureDimension:uint32_t {k2D,k3D,kCube};
constexpr guest::ResourceId DummyTexture(TextureDimension d) {return UINT64_MAX-uint32_t(d);}
inline constexpr guest::ResourceId DummyBuffer=UINT64_MAX-3;
inline constexpr guest::ResourceId InlineBufferBase=uint64_t(1)<<63;
inline constexpr guest::ResourceId ExpandedIndexBufferBase=(uint64_t(1)<<63)|(uint64_t(1)<<62);
inline constexpr guest::ResourceId DescriptorConstantBufferBase=(uint64_t(1)<<63)|(uint64_t(1)<<61);
// The three constant blocks of a draw are bound as one contiguous 12 KiB range.
static_assert(sizeof(guest::ConstantSnapshot)==3*4096 && offsetof(guest::ConstantSnapshot,vs)==0 &&
              offsetof(guest::ConstantSnapshot,ps)==4096 && offsetof(guest::ConstantSnapshot,shared)==8192);
inline constexpr size_t kSharedConstantsOffset=offsetof(guest::ConstantSnapshot,shared);
struct TextureBindingRequest {
  uint32_t slot;
  guest::ResourceId resource;
  TextureDimension dimension;
  uint32_t flags;
};
// Resource identities and indices of one draw. The constants live in the draw's block (see BuildBindings).
struct DrawBindings {
  std::array<std::array<guest::ResourceId,32>,3> textures{};
  std::array<guest::ResourceId,32> vertex_buffers{};
  std::array<uint32_t,32> texture_indices{},sampler_indices{};
};
guest::ResourceId TextureResourceId(std::span<const uint32_t,6>);
// Validates every request first: on failure `bindings` is left untouched.
bool RemapTextureBindings(std::span<const TextureBindingRequest>,DrawBindings&,Error&);
// Fills `bindings` and writes the draw's constants into `block` (sizeof(guest::ConstantSnapshot) bytes, e.g. a
// ResourceStore::MapTransient slice): vs and ps as captured, shared patched with the texture and sampler
// indices and the vertex fetch metadata. Callers may patch the shared part further afterwards.
bool BuildBindings(const guest::DrawPacket&,std::byte* block,DrawBindings& bindings,Error&);
bool PlanSampler(std::span<const uint32_t,6>,const VkPhysicalDeviceFeatures&,
                 const VkPhysicalDeviceLimits&,bool mirror_clamp,
                 VkSamplerCreateInfo&,Error&);
} // namespace superman_returns::graphics::vulkan
```

- [ ] **Passo 5: `descriptors.cpp`**

Trocar o `#include <map>` por `#include <array>` (o `<algorithm>` e o `<cstring>` ficam) e substituir as funções `RemapTextureBindings` e `BuildBindings` (da linha `bool RemapTextureBindings(` até o fim de `BuildBindings`, antes de `bool PlanSampler(`) por:

```cpp
bool RemapTextureBindings(std::span<const TextureBindingRequest> requests,DrawBindings& out,Error& error) {
  if(requests.size()>32) {error={"Descriptor remap",VK_ERROR_FEATURE_NOT_PRESENT,"More than 32 texture slots"};return false;}
  std::array<bool,32> used{};
  for(const auto& r:requests) {  // validate everything before touching `out`
    auto dim=uint32_t(r.dimension);
    if(r.slot>=32 || dim>=3 || used[r.slot] || !r.resource || r.resource>=DummyBuffer) {
      error={"Descriptor remap",VK_ERROR_INITIALIZATION_FAILED,"Invalid or duplicate texture slot/resource/dimension"};return false;
    }
    used[r.slot]=true;
  }
  for(uint32_t dim=0;dim<3;++dim) out.textures[dim].fill(DummyTexture(TextureDimension(dim)));
  out.texture_indices.fill(31);
  std::array<uint32_t,3> count{};
  for(const auto& r:requests) {
    auto dim=uint32_t(r.dimension);
    uint32_t index=count[dim]++;
    out.textures[dim][index]=r.resource;
    out.texture_indices[r.slot]=(r.flags&~0x7fffu)|index;
  }
  error={};return true;
}
bool BuildBindings(const guest::DrawPacket& draw,std::byte* block,DrawBindings& out,Error& error) {
  out=DrawBindings{};
  out.vertex_buffers.fill(DummyBuffer);
  std::array<TextureBindingRequest,32> requests;size_t request_count=0;
  for(uint32_t slot=0;slot<32;++slot) {
    const auto& fetch=draw.texture_fetch[slot];
    if((fetch[0]&3)!=2) continue;
    uint32_t dim=(fetch[5]>>9)&3;
    if(dim==0) dim=1; // SDK prepares 1D as a one-row 2D image.
    uint32_t gamma=0;
    if(((fetch[0]>>2)&3)==3 && ((fetch[0]>>4)&3)==3 && ((fetch[0]>>6)&3)==3) gamma=0x80000000u;
    requests[request_count++]={slot,TextureResourceId(fetch),TextureDimension(dim-1),gamma};
  }
  if(!RemapTextureBindings(std::span<const TextureBindingRequest>(requests.data(),request_count),out,error)) return false;
  std::array<uint8_t,32> stream_index;stream_index.fill(0xff);uint32_t streams=0;
  if(draw.inline_vertices) {
    out.vertex_buffers[0]=InlineBufferBase|draw.command_serial;
  } else {
    for(const auto& stream:draw.streams) {
      if(stream.stream>=32 || !stream.update.plan.key || stream_index[stream.stream]!=0xff || streams>=32) {
        error={"Vertex remap",VK_ERROR_INITIALIZATION_FAILED,"Invalid or duplicate vertex stream"};return false;
      }
      stream_index[stream.stream]=uint8_t(streams);out.vertex_buffers[streams++]=stream.update.plan.key;
    }
  }
  std::memcpy(block,&draw.constants,sizeof(guest::ConstantSnapshot));
  std::byte* shared=block+kSharedConstantsOffset;
  for(uint32_t i=0;i<draw.vertex_fetch.size();++i) {
    auto meta=draw.vertex_fetch[i];
    if(meta.type) {
      if(draw.inline_vertices) meta.buffer=0;
      else {
        if(meta.buffer>=32 || stream_index[meta.buffer]==0xff) {error={"Vertex remap",VK_ERROR_INITIALIZATION_FAILED,"Vertex metadata references missing stream"};return false;}
        meta.buffer=stream_index[meta.buffer];
      }
    } else {meta={31,0,0,0};}
    std::memcpy(shared+512+i*16,&meta,16);
  }
  for(uint32_t slot=0;slot<32;++slot) {
    out.sampler_indices[slot]=slot;
    std::memcpy(shared+slot*4,&out.texture_indices[slot],4);
    std::memcpy(shared+128+slot*4,&out.sampler_indices[slot],4);
  }
  error={};return true;
}
```

- [ ] **Passo 6: `resources.h` — `TransientSlice` e a API nova**

Depois da definição de `struct BufferResource {...};` (termina em `~BufferResource();\n};`), acrescentar:

```cpp
// A host-visible slice of the transient arena that the caller fills in place (no copy). The arena chunk is
// kept by the submission (Place keeps it once per serial), so the slice needs no ownership of its own.
struct TransientSlice {
  std::shared_ptr<BufferResource> chunk;  // handle, memory and coherency come from the chunk
  VkDeviceSize offset=0,size=0;
  std::byte* data=nullptr;
};
```

Na classe `ResourceStore`, depois da declaração de `UploadTransient` (e seu comentário), acrescentar:

```cpp
  // UploadTransient without the copy: write `size` bytes at slice.data, then FlushTransient. Valid until
  // this submission retires.
  bool MapTransient(VkDeviceSize size,TransientSlice&,Error&);
  bool FlushTransient(const TransientSlice&,Error&);
```

Na parte privada, trocar a linha `std::shared_ptr<BufferResource> Suballocate(Arena&,VkDeviceSize size,VkDeviceSize alignment,Error&);` por:

```cpp
  struct Placement {std::shared_ptr<BufferResource> chunk;VkDeviceSize offset=0;};
  bool Place(Arena&,VkDeviceSize size,VkDeviceSize alignment,Placement&,Error&);
  std::shared_ptr<BufferResource> Suballocate(Arena&,VkDeviceSize size,VkDeviceSize alignment,Error&);
  VkDeviceSize TransientAlignment() const;
```

- [ ] **Passo 7: `resources.cpp` — `Place`, `Suballocate`, `MapTransient`, `FlushTransient`**

Substituir a função `ResourceStore::Suballocate` inteira (da linha `std::shared_ptr<BufferResource> ResourceStore::Suballocate(` até o `}` que antecede `std::shared_ptr<BufferResource> ResourceStore::UploadTransient(`) por:

```cpp
bool ResourceStore::Place(Arena& arena,VkDeviceSize size,VkDeviceSize alignment,Placement& out,Error& e) {
  if(&arena==&staging_) submission_bytes_+=size;
  // Budget covers two frames in flight of constants (~36 MiB per 3000 draws).
  constexpr VkDeviceSize chunk_size=8ull*1024*1024,budget=128ull*1024*1024;
  auto fit=[&](ArenaChunk& chunk) {
    VkDeviceSize offset=(chunk.used+alignment-1)/alignment*alignment;
    if(offset+size>chunk.buffer->size) return false;
    if(chunk.serial!=serial_) {chunk.serial=serial_;submissions_.Keep(serial_,chunk.buffer);}
    chunk.used=offset+size;out.chunk=chunk.buffer;out.offset=offset;return true;
  };
  if(arena.current<arena.chunks.size() && arena.chunks[arena.current].serial==serial_) if(fit(arena.chunks[arena.current])) return true;
  if(arena.overflow.buffer && arena.overflow.serial==serial_) if(fit(arena.overflow)) return true;
  // A chunk referenced only by the arena has no pending submission or view.
  for(size_t i=0;i<arena.chunks.size();++i) {
    auto& chunk=arena.chunks[i];
    if(chunk.buffer.use_count()!=1 || chunk.buffer->size<size) continue;
    chunk.used=0;chunk.serial=0;arena.current=i;if(fit(chunk)) return true;
  }
  auto buffer=NewBuffer(std::max(chunk_size,size),arena.usage,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,e);if(!buffer) return false;
  if(buffer->size==chunk_size && arena.bytes+buffer->allocation<=budget) {
    arena.bytes+=buffer->allocation;arena.chunks.push_back({buffer,0,0});arena.current=arena.chunks.size()-1;return fit(arena.chunks.back());
  }
  // Oversized or over budget: lives for this submission only. Keep filling
  // it, otherwise every later small allocation would create another chunk.
  arena.overflow={buffer,0,0};return fit(arena.overflow);
}
std::shared_ptr<BufferResource> ResourceStore::Suballocate(Arena& arena,VkDeviceSize size,VkDeviceSize alignment,Error& e) {
  Placement at;if(!Place(arena,size,alignment,at,e)) return {};
  auto v=std::make_shared<BufferResource>();v->context=&c_;v->owner=at.chunk;v->handle=at.chunk->handle;v->memory=at.chunk->memory;
  v->size=size;v->allocation=size;v->offset=at.offset;v->coherent=at.chunk->coherent;v->mapped=static_cast<std::byte*>(at.chunk->mapped)+at.offset;
  return v;
}
VkDeviceSize ResourceStore::TransientAlignment() const {
  return std::lcm<VkDeviceSize>(std::max<VkDeviceSize>(16,c_.properties.limits.minStorageBufferOffsetAlignment),std::max<VkDeviceSize>(1,c_.properties.limits.nonCoherentAtomSize));
}
bool ResourceStore::MapTransient(VkDeviceSize size,TransientSlice& out,Error& e) {
  if(!Ready(e)) return false;
  if(!size || size>c_.properties.limits.maxStorageBufferRange) return Fail(e,"Map transient","Invalid storage buffer range");
  Placement at;if(!Place(transient_,size,TransientAlignment(),at,e)) return false;
  out.chunk=std::move(at.chunk);out.offset=at.offset;out.size=size;out.data=static_cast<std::byte*>(out.chunk->mapped)+at.offset;
  e={};return true;
}
bool ResourceStore::FlushTransient(const TransientSlice& slice,Error& e) {
  if(!slice.chunk || !slice.data) return Fail(e,"Flush transient","Slice was not mapped");
  if(slice.chunk->coherent) {e={};return true;}
  auto aligned=AlignFlushRange(slice.offset,slice.size,slice.chunk->allocation,std::max<VkDeviceSize>(1,c_.properties.limits.nonCoherentAtomSize));
  if(!aligned.valid) return Fail(e,"Flush transient","Invalid flush range");
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=slice.chunk->memory;range.offset=aligned.offset;range.size=aligned.size;
  return Check(c_.f.vkFlushMappedMemoryRanges(c_.device,1,&range),"Flush transient",e);
}
```

E em `UploadTransient`, trocar a linha `auto alignment=std::lcm<VkDeviceSize>(...);` por `auto alignment=TransientAlignment();`.

- [ ] **Passo 8: `descriptor_sets.h/.cpp` — `Prepare` com a fatia**

Em `descriptor_sets.h`: trocar a declaração de `Prepare` por

```cpp
  // `constants` is the draw's 12 KiB block, already filled (BuildBindings); Prepare flushes it and binds it.
  std::shared_ptr<DescriptorDraw> Prepare(const DrawBindings&,
      const std::array<std::array<uint32_t,6>,32>& fetch,
      ResourceStore&,uint64_t submission,const TransientSlice& constants,Error&);
```

e a declaração `VkDescriptorSet ConstantSet(const BufferResource& block,Error&);` por `VkDescriptorSet ConstantSet(const std::shared_ptr<BufferResource>& chunk,Error&);`.

Em `descriptor_sets.cpp`: remover o `static_assert(...)` e o comentário de duas linhas acima dele (agora vive em `descriptors.h`). Substituir a função `DescriptorStore::Prepare` inteira por:

```cpp
std::shared_ptr<DescriptorDraw> DescriptorStore::Prepare(const DrawBindings& bindings,
    const std::array<std::array<uint32_t,6>,32>& fetch,ResourceStore& store,uint64_t serial,const TransientSlice& constants,Error& e) {
  if(!layouts_[0] || serial<=completed_ || store.CurrentSerial()!=serial) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Missing layouts or recording submission"};return {};
  }
  for(uint32_t slot=0;slot<32;++slot) if((bindings.texture_indices[slot]&0x7fffu)>=32 || bindings.sampler_indices[slot]!=slot) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Descriptor index was not remapped into the draw arrays"};return {};
  }
  if(!constants.chunk || constants.size!=sizeof(guest::ConstantSnapshot)) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Constant block is missing or has the wrong size"};return {};
  }
  if(constants.offset>UINT32_MAX) {e={"Draw descriptors",VK_ERROR_OUT_OF_DEVICE_MEMORY,"Constant block offset exceeds the dynamic offset range"};return {};}
  auto draw=std::make_shared<DescriptorDraw>();
  draw->shared=Shared(bindings,fetch,store,serial,draw->resources,e);if(!draw->shared) return {};
  // VS, PS and shared constants are contiguous in ConstantSnapshot: one block per draw, written in place.
  if(!store.FlushTransient(constants,e)) return {};
  draw->sets[0]=ConstantSet(constants.chunk,e);if(!draw->sets[0]) return {};
  draw->dynamic_offsets.fill(uint32_t(constants.offset));
  for(uint32_t i=0;i<3;++i) draw->sets[1+i]=draw->shared->sets[i];
  pending_.Keep(serial,draw);e={};return draw;
}
```

E substituir o início de `ConstantSet` (as 8 primeiras linhas, até o `constant_sets_.emplace`) mantendo o resto: a função passa a ser

```cpp
VkDescriptorSet DescriptorStore::ConstantSet(const std::shared_ptr<BufferResource>& chunk,Error& e) {
  // Blocks are suballocations: every block of one arena chunk shares its handle and its set.
  if(!chunk) {e={"Constant descriptors",VK_ERROR_INITIALIZATION_FAILED,"Constants were not suballocated from an arena"};return VK_NULL_HANDLE;}
  if(auto found=constant_sets_.find(chunk->handle);found!=constant_sets_.end()) {
    if(found->second.owner.lock()==chunk) return found->second.set;
    FreeConstantSet(found->second.set);constant_sets_.erase(found);  // the driver reused the handle of a destroyed chunk
  }
```

e, no fim da função, trocar `constant_sets_.emplace(block.handle,ConstantSetEntry{chunk,set});` por `constant_sets_.emplace(chunk->handle,ConstantSetEntry{chunk,set});`. O resto de `ConstantSet` (limpeza de sets de chunks destruídos, pool, escrita dos 3 bindings `{chunk->handle,i*block_bytes,block_bytes}`) fica como está.

- [ ] **Passo 9: chamadores — `game_renderer.cpp`**

Trocar o bloco (linhas 206–209)

```cpp
  auto bindings=BuildBindings(draw,e);if(!e.message.empty()) return false;
  lap(RecordProfile::kBindings);
  auto descriptors=descriptors_.Prepare(bindings,draw.texture_fetch,resources_,serial_,e);if(!descriptors) return false;
  lap(RecordProfile::kDescriptors);
```

por

```cpp
  TransientSlice constants;if(!resources_.MapTransient(sizeof(guest::ConstantSnapshot),constants,e)) return false;
  DrawBindings bindings;if(!BuildBindings(draw,constants.data,bindings,e)) return false;
  lap(RecordProfile::kBindings);
  auto descriptors=descriptors_.Prepare(bindings,draw.texture_fetch,resources_,serial_,constants,e);if(!descriptors) return false;
  lap(RecordProfile::kDescriptors);
```

Trocar as 4 linhas de `struct Binding ... vertices` (linhas 211–214) por:

```cpp
  struct Binding {uint32_t slot;VkBuffer handle;VkDeviceSize offset;};
  std::array<Binding,33> vertices;uint32_t vertex_count=0;  // at most 32 streams (BuildBindings) plus the missing-vertex stub
  if(draw.inline_vertices) {auto b=resources_.Buffer(InlineBufferBase|draw.command_serial,e);if(!b) return false;vertices[vertex_count++]={0,b->handle,b->offset};}
  else for(auto& stream:draw.streams) {auto b=resources_.Buffer(stream.update.plan.key,e);if(!b || stream.offset>b->size) return Fail(e,"Vertex stream offset exceeds buffer");vertices[vertex_count++]={stream.stream,b->handle,b->offset+stream.offset};}
  auto missing=resources_.Buffer(MissingVertex,e);if(!missing) return false;vertices[vertex_count++]={31,missing->handle,missing->offset};
```

e a linha do laço de vertex buffers (`for(auto& b:vertices) if(std::any_of(...)) c_.f.vkCmdBindVertexBuffers(command,b.slot,1,&b.resource->handle,&b.offset);`) por:

```cpp
  for(uint32_t i=0;i<vertex_count;++i) {const auto& b=vertices[i];if(std::any_of(pipeline->bindings.begin(),pipeline->bindings.end(),[&](const auto& binding){return binding.binding==b.slot;})) c_.f.vkCmdBindVertexBuffers(command,b.slot,1,&b.handle,&b.offset);}
```

- [ ] **Passo 10: chamadores — `composition.cpp`, `immediate.cpp`, teste de contrato**

`composition.cpp` (função `FrontbufferCompositor::Prepare`): trocar

```cpp
  auto bindings=BuildBindings(draw,e);if(!e.message.empty()) return {};
  bindings.textures[0][0]=source_id;bindings.texture_indices[0]=0;
  std::memcpy(bindings.constants.shared.data(),gamma.data(),gamma.size_bytes());
  uint32_t options[]{uint32_t(gamma_enabled),uint32_t(pass.formats[0]==VK_FORMAT_R8G8B8A8_SRGB || pass.formats[0]==VK_FORMAT_B8G8R8A8_SRGB)};
  std::memcpy(bindings.constants.shared.data()+1024,options,sizeof(options));
```

por

```cpp
  TransientSlice constants;if(!resources.MapTransient(sizeof(guest::ConstantSnapshot),constants,e)) return {};
  DrawBindings bindings;if(!BuildBindings(draw,constants.data,bindings,e)) return {};
  bindings.textures[0][0]=source_id;bindings.texture_indices[0]=0;
  std::byte* shared=constants.data+kSharedConstantsOffset;
  std::memcpy(shared,gamma.data(),gamma.size_bytes());
  uint32_t options[]{uint32_t(gamma_enabled),uint32_t(pass.formats[0]==VK_FORMAT_R8G8B8A8_SRGB || pass.formats[0]==VK_FORMAT_B8G8R8A8_SRGB)};
  std::memcpy(shared+1024,options,sizeof(options));
```

e a chamada `descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),e)` por `descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),constants,e)`.

`immediate.cpp` (`ImmediateRenderer::Draw`): trocar

```cpp
  auto bindings=BuildBindings(draw,e);if(!e.message.empty()) return false;
  bindings.vertex_buffers.fill(DummyBuffer);
```

por

```cpp
  TransientSlice constants;if(!resources.MapTransient(sizeof(guest::ConstantSnapshot),constants,e)) return false;
  DrawBindings bindings;if(!BuildBindings(draw,constants.data,bindings,e)) return false;
  bindings.vertex_buffers.fill(DummyBuffer);
```

a linha `std::memcpy(bindings.constants.shared.data(),&options,sizeof(options));` por `std::memcpy(constants.data+kSharedConstantsOffset,&options,sizeof(options));` e a chamada de `Prepare` por `descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),constants,e)`.

`tests/vulkan/shader_contract_integration.cpp` (dentro do lambda de `production`): trocar

```cpp
          auto bindings=BuildBindings(packet,e);if(!e.message.empty()) throw std::runtime_error(e.message);
          auto sets=descriptors.Prepare(bindings,packet.texture_fetch,resources,uint64_t(variant)+1,e);
```

por

```cpp
          TransientSlice constants;if(!resources.MapTransient(sizeof(guest::ConstantSnapshot),constants,e)) throw std::runtime_error(e.operation+": "+e.message);
          DrawBindings bindings;if(!BuildBindings(packet,constants.data,bindings,e)) throw std::runtime_error(e.message);
          auto sets=descriptors.Prepare(bindings,packet.texture_fetch,resources,uint64_t(variant)+1,constants,e);
```

- [ ] **Passo 11: compilar e rodar tudo**

Rodar os **Testes unitários nativos** (para o `render_packet` não regredir) e a **suíte de regressão Vulkan**. Esperado: os testes novos de `test_descriptors`/`test_resources` passam, o contrato `--production-bindings` mostra `contract variant 0/1/2 pixels passed`, e os 15 fixtures passam (em especial `game-record`, `game-record-merged`, `composition`, `immediate`, `game-frame`).

Qualquer erro de compilação restante em outro chamador de `BuildBindings`/`Prepare` (procure com `grep -rn "BuildBindings\|descriptors_.Prepare\|\.Prepare(" port tests`) segue o mesmo padrão dos passos 9–10.

- [ ] **Passo 12: medir e comparar**

Passo de medição com `<tag>` = `t1`. Critério: `bindings` em µs/draw abaixo da linha de base `c3_base` (esperado de ~2,3–3,2 µs/draw para ~0,7–1 µs/draw) e gate `PASS`. Se `bindings` não cair, **reverta a tarefa** (`git checkout -- .`) e reporte.

- [ ] **Passo 13: commit**

```powershell
git add -A port/src/graphics/vulkan tests/vulkan
git commit -m "perf(vulkan): write each draw's constants once, in the arena, and build bindings without allocations

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Tarefa 2: Posse por submissão e descritores sem alocação

Cada recurso ganha `held_serial`; `Hold` só acrescenta à lista da submissão uma vez por serial. O cache de descritores promove os recursos de uma entrada (e a própria entrada) só no primeiro uso da submissão, e `DescriptorDraw` vira um valor.

**Arquivos:**
- Modificar: `port/src/graphics/vulkan/resources.h`, `resources.cpp`, `descriptor_sets.h`, `descriptor_sets.cpp`, `game_renderer.cpp`, `composition.h`, `composition.cpp`, `immediate.cpp`, `tools/verify_vulkan_m3.ps1`
- Testes: `tests/vulkan/test_resources.cpp`, `tests/vulkan/resources_integration.cpp` (fixture `--hold-lifetime`), `tests/vulkan/shader_contract_integration.cpp`

**Interfaces:**
- Consome: `TransientSlice`, `MapTransient`, `Prepare(..., constants, Error&)` da Tarefa 1.
- Produz: `template<class T> void SubmissionResources::Hold(uint64_t serial,const std::shared_ptr<T>&)` e `template<class T> void ResourceStore::Hold(const std::shared_ptr<T>&)` (T precisa de `uint64_t held_serial`); `BufferResource::held_serial`, `TextureResource::held_serial`, `DescriptorCacheEntry::held_serial`; `struct DescriptorDraw {std::array<VkDescriptorSet,4> sets;std::array<uint32_t,3> dynamic_offsets;}`; `bool DescriptorStore::Prepare(const DrawBindings&,fetch,ResourceStore&,uint64_t serial,const TransientSlice&,DescriptorDraw& out,Error&)`.
- Invariante: todo recurso que um draw grava passa por `Hold` antes de os comandos desse draw serem gravados; o cache só guarda `weak_ptr`.

- [ ] **Passo 1: teste unitário que falha — `tests/vulkan/test_resources.cpp`**

Acrescentar depois do teste `submission_resources_retire_only_completed_versions`:

```cpp
SR_TEST(submission_resources_hold_keeps_a_resource_once_per_serial_and_until_retire) {
  SubmissionResources submissions;
  auto buffer=std::make_shared<BufferResource>();std::weak_ptr<BufferResource> view=buffer;
  submissions.Hold(4,buffer);submissions.Hold(4,buffer);submissions.Hold(4,buffer);
  SR_CHECK_EQ(buffer.use_count(),2);          // the caller's reference and one entry of submission 4
  submissions.Hold(7,buffer);
  SR_CHECK_EQ(buffer.use_count(),3);          // a later submission holds it again
  buffer.reset();                              // the store replaced the resource: only the lists keep it
  submissions.Retire(4);SR_CHECK(!view.expired());
  submissions.Retire(7);SR_CHECK(view.expired());
  // Lists are reused: later submissions still work and release their resources.
  auto next=std::make_shared<BufferResource>();std::weak_ptr<BufferResource> next_view=next;
  submissions.Hold(9,next);next.reset();SR_CHECK(!next_view.expired());
  submissions.Retire(9);SR_CHECK(next_view.expired());
}
```

- [ ] **Passo 2: `resources.h` — `held_serial`, `Hold`, listas reaproveitadas**

Substituir a classe `SubmissionResources` por:

```cpp
class SubmissionResources {
public:
  void Keep(uint64_t serial,std::shared_ptr<void> resource);
  // Keeps a resource for the submission once: `held_serial` says it is already in that submission's list.
  // Invariant: everything a recorded command refers to is held before the command is recorded.
  template<class T> void Hold(uint64_t serial,const std::shared_ptr<T>& resource) {
    if(!resource || resource->held_serial==serial) return;
    resource->held_serial=serial;Keep(serial,resource);
  }
  void Retire(uint64_t completed_serial);
private:
  std::map<uint64_t,std::vector<std::shared_ptr<void>>> pending_;
  std::vector<std::vector<std::shared_ptr<void>>> spare_;  // emptied lists keep their capacity for the next submission
};
```

Em `struct BufferResource`, acrescentar depois de `uint64_t version=0;`:

```cpp
  uint64_t held_serial=0;  // submission whose list already holds this resource (SubmissionResources::Hold)
```

Em `struct TextureResource`, depois de `uint64_t version=0;VkDeviceSize allocation=0;`:

```cpp
  uint64_t held_serial=0;  // see BufferResource::held_serial
```

Na classe `ResourceStore`, na seção pública, depois de `uint64_t CurrentSerial() const {...}`:

```cpp
  // Keeps a buffer or texture alive until the current submission retires (once per submission).
  template<class T> void Hold(const std::shared_ptr<T>& resource) {submissions_.Hold(serial_,resource);}
```

- [ ] **Passo 3: `resources.cpp`**

Substituir `SubmissionResources::Keep` e `SubmissionResources::Retire` (linhas 8–13) por:

```cpp
void SubmissionResources::Keep(uint64_t serial,std::shared_ptr<void> resource) {
  if(!resource) return;
  auto found=pending_.find(serial);
  if(found==pending_.end()) {
    std::vector<std::shared_ptr<void>> list;
    if(!spare_.empty()) {list=std::move(spare_.back());spare_.pop_back();}
    found=pending_.emplace(serial,std::move(list)).first;
  }
  found->second.push_back(std::move(resource));
}
void SubmissionResources::Retire(uint64_t completed) {
  auto end=pending_.upper_bound(completed);
  for(auto it=pending_.begin();it!=end;++it) {
    auto list=std::move(it->second);list.clear();  // releases the resources
    if(spare_.size()<8) spare_.push_back(std::move(list));
  }
  pending_.erase(pending_.begin(),end);
}
```

Em todas as chamadas `submissions_.Keep(serial_,X)` onde `X` é um buffer ou textura (linhas ~210, 219, 224 (duas), 230, 264 (duas), 275, 298, 308, 314, 335, 340, 352, 358), trocar `Keep` por `Hold`. **Não** trocar a de `Place` (`submissions_.Keep(serial_,chunk.buffer)`, que já tem dedupe por `chunk.serial`) nem a de `~ResourceStore` (`submissions_.Retire`). Conferir com:

```powershell
Select-String -Path port\src\graphics\vulkan\resources.cpp -Pattern 'submissions_\.(Keep|Hold)'
```

Esperado: exatamente uma ocorrência de `Keep` (em `Place`), as demais `Hold`.

- [ ] **Passo 4: `descriptor_sets.h`**

No comentário acima de `DescriptorCacheEntry`, trocar a última frase (`Draws hold strong references while their submission is in flight.`) por `The submission holds the entry and the resources strongly (SubmissionResources::Hold) while it is in flight.` Depois, substituir a struct `DescriptorCacheEntry` e a struct `DescriptorDraw` por:

```cpp
struct DescriptorCacheEntry {
  Context* context=nullptr;
  std::vector<uint64_t> key;
  std::array<VkDescriptorSet,3> sets{};
  std::shared_ptr<DescriptorPage> pool;
  std::vector<std::weak_ptr<BufferResource>> buffers;
  std::vector<std::weak_ptr<TextureResource>> textures;
  std::vector<std::shared_ptr<void>> samplers;
  uint64_t last_used=0;
  uint64_t held_serial=0;  // submission whose list already holds this entry (SubmissionResources::Hold)
  // Returns the sets to their pool; runs once no submission or cache holds it.
  ~DescriptorCacheEntry();
};
// A draw's four descriptor sets. Everything they refer to (the cache entry, the buffers, the textures and the
// constants chunk) is held by the submission until it retires, so the draw owns nothing.
struct DescriptorDraw {
  std::array<VkDescriptorSet,4> sets{};
  // Set 0 binds the draw's constants with dynamic offsets: bind with these three offsets.
  std::array<uint32_t,3> dynamic_offsets{};
};
```

(`std::array<uint32_t,3> dynamic_offsets` mantém o comentário.) Trocar a declaração de `Prepare` por:

```cpp
  // `constants` is the draw's 12 KiB block, already filled (BuildBindings); Prepare flushes it and binds it.
  bool Prepare(const DrawBindings&,const std::array<std::array<uint32_t,6>,32>& fetch,
      ResourceStore&,uint64_t submission,const TransientSlice& constants,DescriptorDraw& out,Error&);
```

e a declaração de `Shared` por:

```cpp
  // Returns the entry, held by the submission (and by the cache when cacheable); null on error.
  DescriptorCacheEntry* Shared(const DrawBindings&,const std::array<std::array<uint32_t,6>,32>&,ResourceStore&,uint64_t serial,Error&);
```

- [ ] **Passo 5: `descriptor_sets.cpp` — `Shared` e `Prepare`**

Acrescentar `#include <cstring>` no topo. Substituir a função `DescriptorStore::Shared` inteira por:

```cpp
DescriptorCacheEntry* DescriptorStore::Shared(const DrawBindings& bindings,
    const std::array<std::array<uint32_t,6>,32>& fetch,ResourceStore& store,uint64_t serial,Error& e) {
  // Key: identity of every bound resource plus the sampler/filter state that decides which sampler and which
  // format checks apply. Built in a fixed array together with its hash, so a hit allocates nothing.
  // Consecutive slots mostly bind the same dummy IDs, so repeated lookups are memoized.
  std::array<uint64_t,6*32> key;uint32_t words=0;uint64_t hash=14695981039346656037ull;
  auto push=[&](uint64_t value) {key[words++]=value;hash^=value;hash*=1099511628211ull;};
  guest::ResourceId last_buffer=~0ull,last_texture=~0ull;const void* buffer_ptr=nullptr;const void* texture_ptr=nullptr;
  auto texture_key=[&](guest::ResourceId id) -> const void* {
    if(id!=last_texture) {auto* t=store.FindTexture(id);texture_ptr=t?t->get():nullptr;last_texture=id;}
    return texture_ptr;
  };
  // Draws binding per-submission transient buffers (inline geometry) never
  // repeat: they are written but not cached, so they cannot pin arena chunks.
  bool complete=true,cacheable=true;
  for(uint32_t slot=0;slot<32;++slot) {
    if(bindings.vertex_buffers[slot]!=last_buffer) {auto* b=store.FindBuffer(bindings.vertex_buffers[slot]);buffer_ptr=b?b->get():nullptr;last_buffer=bindings.vertex_buffers[slot];if(b && (*b)->owner) cacheable=false;}
    push(uint64_t(reinterpret_cast<uintptr_t>(buffer_ptr)));complete&=buffer_ptr!=nullptr;
    for(uint32_t dimension=0;dimension<3;++dimension) {auto* t=texture_key(bindings.textures[dimension][slot]);push(uint64_t(reinterpret_cast<uintptr_t>(t)));complete&=t!=nullptr;}
    // Only the sampler/filter bits of texture slots matter. Other slots hold
    // vertex fetch constants whose addresses change every frame.
    if((fetch[slot][0]&3)==2) {
      push((uint64_t(fetch[slot][0]&0x7fc00u)<<32)|(fetch[slot][3]&0xff80000u));
      push((uint64_t(1)<<63)|(uint64_t((fetch[slot][5]>>9)&3)<<32)|bindings.texture_indices[slot]);
    } else push(bindings.texture_indices[slot]);
  }
  if(complete && cacheable) if(auto found=cache_.find(hash);found!=cache_.end()) {
    auto& list=found->second;
    for(auto it=list.begin();it!=list.end();++it) {
      auto& entry=*it;
      if(entry->key.size()!=words || std::memcmp(entry->key.data(),key.data(),words*sizeof(uint64_t))!=0) continue;
      if(entry->held_serial!=serial) {
        // First use in this submission: every resource must still be alive (a dead one means the key's addresses
        // may have been reused). Hold them and the entry until the submission retires; later draws skip this.
        bool alive=true;
        for(auto& weak:entry->buffers) {auto strong=weak.lock();if(!strong) {alive=false;break;}store.Hold(strong);}
        if(alive) for(auto& weak:entry->textures) {auto strong=weak.lock();if(!strong) {alive=false;break;}store.Hold(strong);}
        if(!alive) {list.erase(it);--cache_entries_;break;}  // a resource died: rebuild
        pending_.Hold(serial,entry);
      }
      entry->last_used=serial;++cache_stats_.hits;e={};return entry.get();
    }
  }
  ++cache_stats_.misses;
  // Miss: resolve, validate and write the sets exactly as an uncached draw would. store.Buffer/Texture hold what they return.
  auto entry=std::make_shared<DescriptorCacheEntry>();entry->key.assign(key.begin(),key.begin()+words);entry->last_used=serial;entry->context=&c_;
  std::array<VkDescriptorBufferInfo,32> vertex_info{};
  std::array<std::array<VkDescriptorImageInfo,32>,3> texture_info{};
  std::array<VkDescriptorImageInfo,32> sampler_info{};
  std::vector<std::shared_ptr<BufferResource>> buffers;std::vector<std::shared_ptr<TextureResource>> textures;
  for(uint32_t slot=0;slot<32;++slot) {
    auto buffer=store.Buffer(bindings.vertex_buffers[slot],e);if(!buffer) return nullptr;
    vertex_info[slot]={buffer->handle,buffer->offset,buffer->size};buffers.push_back(buffer);
    for(uint32_t dimension=0;dimension<3;++dimension) {
      auto texture=store.Texture(bindings.textures[dimension][slot],e);if(!texture) return nullptr;
      auto expected=dimension==0?VK_IMAGE_VIEW_TYPE_2D:(dimension==1?VK_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_CUBE);
      if(texture->view_type!=expected) {e={"Texture descriptors",VK_ERROR_INITIALIZATION_FAILED,"Texture view dimension mismatches shader array"};return nullptr;}
      texture_info[dimension][slot]={VK_NULL_HANDLE,texture->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};textures.push_back(texture);
    }
    // Non-texture slots only expose the dummy texture: any sampler will do.
    static constexpr std::array<uint32_t,6> no_texture{};
    auto sampler=Sampler((fetch[slot][0]&3)==2?std::span<const uint32_t,6>(fetch[slot]):std::span<const uint32_t,6>(no_texture),e);if(!sampler) return nullptr;
    sampler_info[slot]={sampler->handle,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
    if(entry->samplers.empty() || entry->samplers.back()!=sampler) entry->samplers.push_back(sampler);
    if((fetch[slot][0]&3)==2 && (((fetch[slot][3]>>19)&3)==1 || ((fetch[slot][3]>>21)&3)==1 || ((fetch[slot][3]>>25)&7)>1)) {
      uint32_t dimension=(fetch[slot][5]>>9)&3;dimension=dimension?dimension-1:0;
      auto texture=store.Texture(bindings.textures[dimension][bindings.texture_indices[slot]&0x7fffu],e);if(!texture) return nullptr;
      auto features=filter_features_.find(texture->format);
      if(features==filter_features_.end()) {VkFormatProperties properties{};c_.f.vkGetPhysicalDeviceFormatProperties(c_.physical,texture->format,&properties);features=filter_features_.emplace(texture->format,properties.optimalTilingFeatures).first;}
      if(!(features->second&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        e={"Texture filtering",VK_ERROR_FORMAT_NOT_SUPPORTED,"Guest sampler requests linear filtering of an unsupported format"};return nullptr;
      }
    }
  }
  // Watch each distinct buffer/texture once, weakly: the entry never extends their lifetime.
  std::sort(buffers.begin(),buffers.end());buffers.erase(std::unique(buffers.begin(),buffers.end()),buffers.end());
  std::sort(textures.begin(),textures.end());textures.erase(std::unique(textures.begin(),textures.end()),textures.end());
  for(auto& buffer:buffers) entry->buffers.push_back(buffer);
  for(auto& texture:textures) entry->textures.push_back(texture);
  constexpr uint32_t entries_per_pool=64;
  // Entries own old texture versions (e.g. movie frames): sweep regularly.
  // A full cache stops admitting entries instead of scanning on every miss.
  if(serial>=last_evict_+4) {Evict(serial);last_evict_=serial;}
  if(cache_entries_>=4096) cacheable=false;
  for(auto& pool:cache_pools_) if(pool->draws<entries_per_pool) {entry->pool=pool;break;}
  if(!entry->pool) {
    auto pool=std::make_shared<DescriptorPage>();pool->context=&c_;
    VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,32*entries_per_pool},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,96*entries_per_pool},{VK_DESCRIPTOR_TYPE_SAMPLER,32*entries_per_pool}};
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};info.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;info.maxSets=3*entries_per_pool;info.poolSizeCount=3;info.pPoolSizes=sizes;
    if(!Check(c_.f.vkCreateDescriptorPool(c_.device,&info,nullptr,&pool->pool),"Create shared descriptor pool",e)) return nullptr;
    cache_pools_.push_back(pool);entry->pool=pool;
  }
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=entry->pool->pool;allocate.descriptorSetCount=3;allocate.pSetLayouts=layouts_.data()+1;
  if(!Check(c_.f.vkAllocateDescriptorSets(c_.device,&allocate,entry->sets.data()),"Allocate shared descriptor sets",e)) return nullptr;
  ++entry->pool->draws;
  std::array<VkWriteDescriptorSet,5> writes{};
  auto write=[&](uint32_t index,uint32_t set,uint32_t binding,VkDescriptorType type,const VkDescriptorBufferInfo* buffers,const VkDescriptorImageInfo* images) {
    auto& w=writes[index];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=entry->sets[set];w.dstBinding=binding;w.descriptorCount=32;w.descriptorType=type;w.pBufferInfo=buffers;w.pImageInfo=images;
  };
  for(uint32_t dim=0;dim<3;++dim) write(dim,0,dim,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,nullptr,texture_info[dim].data());
  write(3,1,0,VK_DESCRIPTOR_TYPE_SAMPLER,nullptr,sampler_info.data());
  write(4,2,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,vertex_info.data(),nullptr);
  c_.f.vkUpdateDescriptorSets(c_.device,uint32_t(writes.size()),writes.data(),0,nullptr);
  pending_.Hold(serial,entry);
  if(complete && cacheable) {cache_[hash].push_back(entry);++cache_entries_;}
  e={};return entry.get();  // the submission list keeps the entry alive until it retires
}
```

Substituir `DescriptorStore::Prepare` inteira por:

```cpp
bool DescriptorStore::Prepare(const DrawBindings& bindings,const std::array<std::array<uint32_t,6>,32>& fetch,
    ResourceStore& store,uint64_t serial,const TransientSlice& constants,DescriptorDraw& out,Error& e) {
  if(!layouts_[0] || serial<=completed_ || store.CurrentSerial()!=serial) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Missing layouts or recording submission"};return false;
  }
  for(uint32_t slot=0;slot<32;++slot) if((bindings.texture_indices[slot]&0x7fffu)>=32 || bindings.sampler_indices[slot]!=slot) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Descriptor index was not remapped into the draw arrays"};return false;
  }
  if(!constants.chunk || constants.size!=sizeof(guest::ConstantSnapshot)) {
    e={"Draw descriptors",VK_ERROR_INITIALIZATION_FAILED,"Constant block is missing or has the wrong size"};return false;
  }
  if(constants.offset>UINT32_MAX) {e={"Draw descriptors",VK_ERROR_OUT_OF_DEVICE_MEMORY,"Constant block offset exceeds the dynamic offset range"};return false;}
  const DescriptorCacheEntry* shared=Shared(bindings,fetch,store,serial,e);if(!shared) return false;
  // VS, PS and shared constants are contiguous in ConstantSnapshot: one block per draw, written in place.
  if(!store.FlushTransient(constants,e)) return false;
  out.sets[0]=ConstantSet(constants.chunk,e);if(!out.sets[0]) return false;
  out.dynamic_offsets.fill(uint32_t(constants.offset));
  for(uint32_t i=0;i<3;++i) out.sets[1+i]=shared->sets[i];
  e={};return true;
}
```

- [ ] **Passo 6: chamadores com `DescriptorDraw` por valor**

`game_renderer.cpp`: trocar `auto descriptors=descriptors_.Prepare(bindings,draw.texture_fetch,resources_,serial_,constants,e);if(!descriptors) return false;` por `DescriptorDraw descriptors;if(!descriptors_.Prepare(bindings,draw.texture_fetch,resources_,serial_,constants,descriptors,e)) return false;` e a linha do `vkCmdBindDescriptorSets` por `...,descriptors.sets.data(),uint32_t(descriptors.dynamic_offsets.size()),descriptors.dynamic_offsets.data());`.

`composition.h`: no `struct CompositionDraw`, trocar `std::shared_ptr<DescriptorDraw> descriptors;` por `DescriptorDraw descriptors;`. `composition.cpp`: `result->descriptors=descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),constants,e);if(!result->descriptors) return {};` vira `if(!descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),constants,result->descriptors,e)) return {};` e, em `Record`, `draw.descriptors->sets.data(),uint32_t(draw.descriptors->dynamic_offsets.size()),draw.descriptors->dynamic_offsets.data()` vira `draw.descriptors.sets.data(),uint32_t(draw.descriptors.dynamic_offsets.size()),draw.descriptors.dynamic_offsets.data()`.

`immediate.cpp`: `auto descriptor=descriptors_.Prepare(...,constants,e);if(!descriptor) return false;` vira `DescriptorDraw descriptor;if(!descriptors_.Prepare(bindings,draw.texture_fetch,resources,resources.CurrentSerial(),constants,descriptor,e)) return false;` e `descriptor->sets.data(),uint32_t(descriptor->dynamic_offsets.size()),descriptor->dynamic_offsets.data()` vira `descriptor.sets.data(),uint32_t(descriptor.dynamic_offsets.size()),descriptor.dynamic_offsets.data()`.

Teste de contrato (`shader_contract_integration.cpp`): trocar

```cpp
          auto sets=descriptors.Prepare(bindings,packet.texture_fetch,resources,uint64_t(variant)+1,constants,e);
          if(!sets) throw std::runtime_error(e.operation+": "+e.message);
          fixture.sets=sets->sets;
          fixture.dynamic_offsets=sets->dynamic_offsets;fixture.dynamic_count=uint32_t(sets->dynamic_offsets.size());
```

por

```cpp
          DescriptorDraw sets;
          if(!descriptors.Prepare(bindings,packet.texture_fetch,resources,uint64_t(variant)+1,constants,sets,e)) throw std::runtime_error(e.operation+": "+e.message);
          fixture.sets=sets.sets;
          fixture.dynamic_offsets=sets.dynamic_offsets;fixture.dynamic_count=uint32_t(sets.dynamic_offsets.size());
```

- [ ] **Passo 7: fixture de GPU `--hold-lifetime` (teste de posse)**

Em `tests/vulkan/resources_integration.cpp`, antes de `int main(`, acrescentar:

```cpp
// A texture replaced while a recorded draw still refers to it must live until its submission retires, on the
// cache-miss path and on the cache-hit paths (a hit in the same submission, and a hit from an earlier submission,
// which promotes the entry's resources). Every texture is uploaded in a submission that has already retired, so the
// upload's own Hold cannot be what keeps it alive: only the draw's Hold can.
// Nothing is submitted: the command buffer only exists so that the stores have a recording submission.
void CheckHoldLifetime(Context& c) {
  Error e;UploadFixture fixture(c);ResourceStore resources(c);DescriptorStore descriptors(c);Require(descriptors.Initialize(e),e);
  guest::DrawPacket packet;packet.texture_fetch[3][0]=2|(2<<10)|(2<<13)|(2<<16);packet.texture_fetch[3][1]=0x1006;packet.texture_fetch[3][5]=1<<9;
  const auto id=TextureResourceId(packet.texture_fetch[3]);
  auto begin=[&](uint64_t serial) {Require(resources.BeginSubmission(fixture.command,serial,e),e);if(serial==1) Require(resources.CreateDummies(e),e);};
  auto upload=[&](uint64_t version,uint8_t value) {
    guest::LinearTexture t;t.width=t.height=1;t.format=guest::LinearFormat::kRGBA8Unorm;t.levels={{1,1,4,1,0}};t.data={value,value,value,255};
    Require(resources.UploadTexture(id,t,version,e),e);
  };
  auto draw=[&](uint64_t serial) {
    TransientSlice constants;Require(resources.MapTransient(sizeof(guest::ConstantSnapshot),constants,e),e);
    DrawBindings bindings;Require(BuildBindings(packet,constants.data,bindings,e),e);
    DescriptorDraw sets;Require(descriptors.Prepare(bindings,packet.texture_fetch,resources,serial,constants,sets,e),e);
  };
  auto current=[&]() {return std::weak_ptr<TextureResource>(*resources.FindTexture(id));};
  auto expect=[](bool ok,const char* what) {if(!ok) throw std::runtime_error(what);};
  auto retire=[&](uint64_t serial) {descriptors.Retire(serial);resources.Retire(serial);};
  // Submission 1 only uploads v1 and retires: from now on v1 is held by the store alone.
  begin(1);upload(1,10);retire(1);
  // Submission 2, cache miss: the draw binds v1, then v1 is replaced by v2 before the fence.
  begin(2);auto v1=current();draw(2);
  expect(descriptors.TakeCacheStats().misses==1,"first draw must miss the descriptor cache");
  upload(2,20);expect(!v1.expired(),"a texture bound by a recorded draw was destroyed before its submission retired (miss path)");
  retire(2);expect(v1.expired(),"a replaced texture leaked after its submission retired");
  // Submission 3: a miss for v2, then the same bindings again in the same submission (a hit that skips the promotion).
  begin(3);auto v2=current();draw(3);draw(3);
  auto stats=descriptors.TakeCacheStats();expect(stats.misses==1 && stats.hits==1,"the second draw of a submission must hit the cache");
  upload(3,30);expect(!v2.expired(),"a texture bound by a recorded draw was destroyed before its submission retired (same-submission hit)");
  retire(3);expect(v2.expired(),"a replaced texture leaked after its submission retired");
  // Submission 4 builds the entry for v3 (a miss); submission 5 hits it (a hit from an earlier submission), then v3 is replaced.
  begin(4);draw(4);retire(4);descriptors.TakeCacheStats();
  begin(5);auto v3=current();draw(5);
  stats=descriptors.TakeCacheStats();expect(stats.hits==1 && stats.misses==0,"a later submission must hit the cached entry");
  upload(4,40);expect(!v3.expired(),"a texture promoted by a cache hit was destroyed before its submission retired (hit path)");
  retire(5);expect(v3.expired(),"a replaced texture leaked after its submission retired");
  std::cout<<"Resources bound by recorded draws stay alive until their submission retires (cache miss, hit in the same and in a later submission)\n";
}
```

E em `main`, junto das outras linhas `if(argc==2 && std::string(argv[1])=="--...")`, acrescentar:

```cpp
    if(argc==2 && std::string(argv[1])=="--hold-lifetime") {CheckHoldLifetime(c);return c.validation_errors.load()?1:0;}
```

Em `tools/verify_vulkan_m3.ps1`, acrescentar `'hold-lifetime'` ao fim da lista `foreach ($fixture in @(... 'composition'))` (linha 48): `..., 'composition', 'hold-lifetime')`. Também na lista "Comandos padrão" deste plano.

- [ ] **Passo 8: compilar, rodar tudo**

Suíte de regressão Vulkan **com** `'hold-lifetime'` na lista de fixtures (rode também `& $gpu --hold-lifetime` isoladamente). Esperado: o teste unitário `submission_resources_hold_...` passa, `hold-lifetime` imprime a linha de sucesso, todo o resto continua passando.

Para ter certeza de que o fixture pega o defeito do ciclo 2, faça a prova negativa uma vez (e desfaça): em `ResourceStore::Texture`, troque temporariamente `submissions_.Hold(serial_,found->second)` por nada, recompile e rode `--hold-lifetime`: deve falhar com "destroyed before its submission retired". Restaure o `Hold`.

- [ ] **Passo 9: medir e comparar**

Passo de medição com `<tag>` = `t2`. Critério: `descriptors` em µs/draw abaixo da Tarefa 1 (esperado de ~6 µs/draw para ~2–3; spec: 13–19 ms por quadro para ~6–8) e gate `PASS`; gate reprovado = **a tarefa não passa, mesmo com a medição boa**. Se `descriptors` não cair, reverta e reporte.

- [ ] **Passo 10: commit**

```powershell
git add -A port/src/graphics/vulkan tests/vulkan tools/verify_vulkan_m3.ps1
git commit -m "perf(vulkan): hold resources once per submission and prepare descriptors without allocations

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Tarefa 3: Filtro de comandos redundantes (`StateShadow`)

**Arquivos:**
- Criar: `port/src/graphics/vulkan/state_shadow.h`, `tests/vulkan/test_state_shadow.cpp`
- Modificar: `port/src/graphics/vulkan/game_renderer.h`, `game_renderer.cpp`, `tests/vulkan/CMakeLists.txt`

**Interfaces:**
- Consome: `DescriptorDraw` por valor (Tarefa 2).
- Produz: `class StateShadow` com `enabled()`, `Invalidate()`, `SetViewport`, `SetScissor`, `SetBlend`, `SetStencil`, `SetSharedSets`, `SetVertexBuffer`, `SetIndexBuffer`; cada `Set*` devolve `true` quando o comando precisa ser emitido (valor novo, estado invalidado ou filtro desligado).

- [ ] **Passo 1: teste que falha — `tests/vulkan/test_state_shadow.cpp`**

```cpp
#include "state_shadow.h"
#include "test_main.h"
using namespace superman_returns::graphics::vulkan;
namespace {
VkBuffer Buffer(uintptr_t value) {return reinterpret_cast<VkBuffer>(value);}
VkDescriptorSet Set(uintptr_t value) {return reinterpret_cast<VkDescriptorSet>(value);}
}
SR_TEST(state_shadow_skips_repeated_dynamic_state_and_reemits_changes) {
  StateShadow s;
  VkViewport viewport{0,0,1280,720,0,1};
  SR_CHECK(s.SetViewport(viewport));SR_CHECK(!s.SetViewport(viewport));
  viewport.width=640;SR_CHECK(s.SetViewport(viewport));SR_CHECK(!s.SetViewport(viewport));
  VkRect2D scissor{{1,2},{3,4}};
  SR_CHECK(s.SetScissor(scissor));SR_CHECK(!s.SetScissor(scissor));scissor.offset.x=0;SR_CHECK(s.SetScissor(scissor));
  float blend[4]{0,0.5f,1,0};
  SR_CHECK(s.SetBlend(blend));SR_CHECK(!s.SetBlend(blend));blend[2]=0.25f;SR_CHECK(s.SetBlend(blend));
  SR_CHECK(s.SetStencil(0));SR_CHECK(!s.SetStencil(0));SR_CHECK(s.SetStencil(255));SR_CHECK(!s.SetStencil(255));
}
SR_TEST(state_shadow_compares_blend_constants_bitwise) {
  StateShadow s;
  float positive[4]{0,0,0,0},negative[4]{-0.0f,0,0,0};
  SR_CHECK(s.SetBlend(positive));
  SR_CHECK(s.SetBlend(negative));   // -0 and +0 are different values on the GPU: emit
  SR_CHECK(!s.SetBlend(negative));
}
SR_TEST(state_shadow_tracks_the_three_shared_sets_as_a_group) {
  StateShadow s;
  VkDescriptorSet sets[3]{Set(0x10),Set(0x20),Set(0x30)};
  SR_CHECK(s.SetSharedSets(sets));SR_CHECK(!s.SetSharedSets(sets));
  sets[2]=Set(0x31);SR_CHECK(s.SetSharedSets(sets));SR_CHECK(!s.SetSharedSets(sets));
  sets[0]=Set(0x11);SR_CHECK(s.SetSharedSets(sets));
}
SR_TEST(state_shadow_tracks_each_vertex_slot_and_the_index_buffer_separately) {
  StateShadow s;
  SR_CHECK(s.SetVertexBuffer(0,Buffer(1),0));SR_CHECK(!s.SetVertexBuffer(0,Buffer(1),0));
  SR_CHECK(s.SetVertexBuffer(1,Buffer(1),0));            // another slot with the same buffer is still a new binding
  SR_CHECK(s.SetVertexBuffer(0,Buffer(1),16));           // same buffer, another offset
  SR_CHECK(s.SetVertexBuffer(0,Buffer(2),16));           // another buffer
  SR_CHECK(!s.SetVertexBuffer(1,Buffer(1),0));SR_CHECK(!s.SetVertexBuffer(0,Buffer(2),16));
  SR_CHECK(s.SetIndexBuffer(Buffer(5),0,VK_INDEX_TYPE_UINT32));SR_CHECK(!s.SetIndexBuffer(Buffer(5),0,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.SetIndexBuffer(Buffer(5),4,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.SetIndexBuffer(Buffer(6),4,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.SetIndexBuffer(Buffer(6),4,VK_INDEX_TYPE_UINT16));
}
SR_TEST(state_shadow_never_filters_a_vertex_slot_it_cannot_track) {
  StateShadow s;
  SR_CHECK(s.SetVertexBuffer(32,Buffer(1),0));SR_CHECK(s.SetVertexBuffer(32,Buffer(1),0));
}
SR_TEST(state_shadow_invalidate_forgets_everything) {
  StateShadow s;
  VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{1,1}};float blend[4]{};VkDescriptorSet sets[3]{Set(1),Set(2),Set(3)};
  s.SetViewport(viewport);s.SetScissor(scissor);s.SetBlend(blend);s.SetStencil(7);s.SetSharedSets(sets);
  s.SetVertexBuffer(3,Buffer(9),0);s.SetIndexBuffer(Buffer(8),0,VK_INDEX_TYPE_UINT32);
  s.Invalidate();
  SR_CHECK(s.SetViewport(viewport));SR_CHECK(s.SetScissor(scissor));SR_CHECK(s.SetBlend(blend));SR_CHECK(s.SetStencil(7));
  SR_CHECK(s.SetSharedSets(sets));SR_CHECK(s.SetVertexBuffer(3,Buffer(9),0));SR_CHECK(s.SetIndexBuffer(Buffer(8),0,VK_INDEX_TYPE_UINT32));
  SR_CHECK(s.enabled());
}
SR_TEST(state_shadow_disabled_always_emits_and_stays_disabled_after_invalidate) {
  StateShadow s(false);
  SR_CHECK(!s.enabled());
  VkViewport viewport{0,0,1,1,0,1};
  SR_CHECK(s.SetViewport(viewport));SR_CHECK(s.SetViewport(viewport));
  SR_CHECK(s.SetStencil(1));SR_CHECK(s.SetStencil(1));
  s.Invalidate();SR_CHECK(!s.enabled());SR_CHECK(s.SetStencil(1));
}
```

Em `tests/vulkan/CMakeLists.txt`, trocar a linha `target_sources(sr_vulkan_tests PRIVATE test_composition.cpp)` por `target_sources(sr_vulkan_tests PRIVATE test_composition.cpp test_state_shadow.cpp)`.

- [ ] **Passo 2: rodar e confirmar a falha**

Run: `powershell -File tools\build_vulkan.ps1`
Esperado: erro de compilação, `state_shadow.h` não existe.

- [ ] **Passo 3: `port/src/graphics/vulkan/state_shadow.h`**

```cpp
#pragma once
#include "loader.h"
#include <array>
#include <cstring>
namespace superman_returns::graphics::vulkan {
// What the recorder last emitted on the open command buffer. A command is skipped only when the same value is
// already in effect. Invalidate() forgets everything; call it wherever the command buffer's state can change
// behind the recorder's back (new submission, end of a render pass, any other recorder). With `enabled=false`
// every Set* asks for the command (SR_VULKAN_NO_STATE_FILTER=1: A/B diagnostics).
class StateShadow {
public:
  explicit StateShadow(bool enabled=true):enabled_(enabled) {}
  bool enabled() const {return enabled_;}
  void Invalidate() {*this=StateShadow(enabled_);}
  bool SetViewport(const VkViewport& value) {return Update(viewport_,value);}
  bool SetScissor(const VkRect2D& value) {return Update(scissor_,value);}
  bool SetBlend(const float (&value)[4]) {return Update(blend_,std::array<float,4>{value[0],value[1],value[2],value[3]});}
  bool SetStencil(uint32_t reference) {return Update(stencil_,reference);}
  // Descriptor sets 1-3 (textures, samplers, vertex buffers), as one group of three handles.
  bool SetSharedSets(const VkDescriptorSet* sets) {return Update(sets_,std::array<VkDescriptorSet,3>{sets[0],sets[1],sets[2]});}
  bool SetVertexBuffer(uint32_t slot,VkBuffer buffer,VkDeviceSize offset) {
    if(slot>=vertex_.size()) return true;  // not tracked: always emit
    auto& v=vertex_[slot];
    if(enabled_ && v.valid && v.buffer==buffer && v.offset==offset) return false;
    v={buffer,offset,true};return true;
  }
  bool SetIndexBuffer(VkBuffer buffer,VkDeviceSize offset,VkIndexType type) {
    if(enabled_ && index_.valid && index_.buffer==buffer && index_.offset==offset && index_.type==type) return false;
    index_={buffer,offset,type,true};return true;
  }
private:
  // Compared bitwise: the types used here have no padding, and -0.0f must not equal +0.0f.
  template<class T> struct Slot {T value{};bool valid=false;};
  template<class T> bool Update(Slot<T>& slot,const T& value) {
    if(enabled_ && slot.valid && std::memcmp(&slot.value,&value,sizeof(T))==0) return false;
    slot.value=value;slot.valid=true;return true;
  }
  struct VertexBinding {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;bool valid=false;};
  struct IndexBinding {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceSize offset=0;VkIndexType type=VK_INDEX_TYPE_UINT16;bool valid=false;};
  bool enabled_;
  Slot<VkViewport> viewport_;Slot<VkRect2D> scissor_;Slot<std::array<float,4>> blend_;Slot<uint32_t> stencil_;
  Slot<std::array<VkDescriptorSet,3>> sets_;
  std::array<VertexBinding,32> vertex_{};
  IndexBinding index_;
};
}
```

- [ ] **Passo 4: rodar os testes unitários**

Run: `powershell -File tools\build_vulkan.ps1` e `.\build\tests-vulkan\sr_vulkan_tests.exe`
Esperado: os 7 testes `state_shadow_*` passam.

- [ ] **Passo 5: usar o `StateShadow` no `GameRenderer`**

`game_renderer.h`: acrescentar `#include "state_shadow.h"` depois de `#include "depth_resolve.h"`. No construtor, trocar `alias_options_(aliases) {` por `alias_options_(aliases),shadow_(StateFilterEnabled()) {`. Na parte privada, depois de `bool Upload(const guest::BufferUpdate&,bool indices,uint64_t version,Error&);` acrescentar `static bool StateFilterEnabled();`, e depois da linha `std::shared_ptr<TargetPass> open_pass_;VkPipeline bound_pipeline_=VK_NULL_HANDLE;bool merge_passes_=false;` acrescentar:

```cpp
  // Last value emitted of each redundant command; invalidated wherever bound_pipeline_ is reset.
  StateShadow shadow_;
```

`game_renderer.cpp`: acrescentar `#include <cstdlib>` e, depois de `GameRenderer::Initialize`:

```cpp
// SR_VULKAN_NO_STATE_FILTER=1 turns the redundant-command filter off (A/B diagnostics).
bool GameRenderer::StateFilterEnabled() {
  const char* value=std::getenv("SR_VULKAN_NO_STATE_FILTER");
  return !(value && value[0] && value[0]!='0');
}
```

Em `BeginSubmission`, trocar `open_pass_.reset();bound_pipeline_=VK_NULL_HANDLE;` por `open_pass_.reset();bound_pipeline_=VK_NULL_HANDLE;shadow_.Invalidate();`. Em `ClosePass`, trocar `c_.f.vkCmdEndRenderPass(command_);open_pass_.reset();bound_pipeline_=VK_NULL_HANDLE;` por `c_.f.vkCmdEndRenderPass(command_);open_pass_.reset();bound_pipeline_=VK_NULL_HANDLE;shadow_.Invalidate();`.

Em `Draw`, substituir o trecho (que, depois das Tarefas 1 e 2, é):

```cpp
  c_.f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines_.Layout(),0,4,descriptors.sets.data(),uint32_t(descriptors.dynamic_offsets.size()),descriptors.dynamic_offsets.data());
  for(uint32_t i=0;i<vertex_count;++i) {const auto& b=vertices[i];if(std::any_of(pipeline->bindings.begin(),pipeline->bindings.end(),[&](const auto& binding){return binding.binding==b.slot;})) c_.f.vkCmdBindVertexBuffers(command,b.slot,1,&b.handle,&b.offset);}
  c_.f.vkCmdSetViewport(command,0,1,&viewport);c_.f.vkCmdSetScissor(command,0,1,&scissor);c_.f.vkCmdSetBlendConstants(command,blend);c_.f.vkCmdSetStencilReference(command,VK_STENCIL_FACE_FRONT_AND_BACK,draw.registers[0x10d]&255);
  if(draw.indexed) {c_.f.vkCmdBindIndexBuffer(command,indices->handle,indices->offset,VK_INDEX_TYPE_UINT32);c_.f.vkCmdDrawIndexed(command,draw.count,1,draw.first,draw.base_vertex,0);}else c_.f.vkCmdDraw(command,draw.count,1,draw.first,0);
```

por:

```cpp
  // Set 0 (the constants) moves with every draw. Sets 1-3 only when their resources change: every game pipeline
  // shares one layout, so binding set 0 alone leaves sets 1-3 in place. The four dynamic states persist on the
  // command buffer across pipeline binds because every game pipeline declares the same four.
  if(shadow_.enabled()) {
    c_.f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines_.Layout(),0,1,descriptors.sets.data(),uint32_t(descriptors.dynamic_offsets.size()),descriptors.dynamic_offsets.data());
    if(shadow_.SetSharedSets(descriptors.sets.data()+1)) c_.f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines_.Layout(),1,3,descriptors.sets.data()+1,0,nullptr);
  } else {
    c_.f.vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines_.Layout(),0,4,descriptors.sets.data(),uint32_t(descriptors.dynamic_offsets.size()),descriptors.dynamic_offsets.data());
  }
  for(uint32_t i=0;i<vertex_count;++i) {
    const auto& b=vertices[i];
    if(std::any_of(pipeline->bindings.begin(),pipeline->bindings.end(),[&](const auto& binding){return binding.binding==b.slot;}) && shadow_.SetVertexBuffer(b.slot,b.handle,b.offset))
      c_.f.vkCmdBindVertexBuffers(command,b.slot,1,&b.handle,&b.offset);
  }
  if(shadow_.SetViewport(viewport)) c_.f.vkCmdSetViewport(command,0,1,&viewport);
  if(shadow_.SetScissor(scissor)) c_.f.vkCmdSetScissor(command,0,1,&scissor);
  if(shadow_.SetBlend(blend)) c_.f.vkCmdSetBlendConstants(command,blend);
  const uint32_t stencil=draw.registers[0x10d]&255;
  if(shadow_.SetStencil(stencil)) c_.f.vkCmdSetStencilReference(command,VK_STENCIL_FACE_FRONT_AND_BACK,stencil);
  if(draw.indexed) {
    if(shadow_.SetIndexBuffer(indices->handle,indices->offset,VK_INDEX_TYPE_UINT32)) c_.f.vkCmdBindIndexBuffer(command,indices->handle,indices->offset,VK_INDEX_TYPE_UINT32);
    c_.f.vkCmdDrawIndexed(command,draw.count,1,draw.first,draw.base_vertex,0);
  } else c_.f.vkCmdDraw(command,draw.count,1,draw.first,0);
```

Confirme que nenhum outro ponto de `GameRenderer` grava no command buffer fora de um render pass aberto sem passar por `ClosePass`/`BeginSubmission` (resolves, clears, aliases e `DumpTargets` já chamam `ClosePass()` antes; `state_.before_barrier` também). Se achar um gravador que não invalida, acrescente `shadow_.Invalidate();` nele.

- [ ] **Passo 6: suíte de regressão Vulkan**

Esperado: tudo passa. Os fixtures `game-record`, `game-record-merged`, `game-frame`, `game-frame-async`, `resolve-sample` e `composition` desenham vários draws seguidos e comparam pixels: um estado sombra errado quebra pelo menos um deles. Rode também com o desligador, para confirmar que o caminho antigo continua correto: `$env:SR_VULKAN_NO_STATE_FILTER='1'; & $gpu --game-record-merged; Remove-Item env:SR_VULKAN_NO_STATE_FILTER`.

- [ ] **Passo 7: medir e comparar**

Passo de medição com `<tag>` = `t3`. Critério: `commands` em µs/draw abaixo da Tarefa 2 (esperado de ~1,1–1,4 para ~0,4–0,7) e gate `PASS`. Faça também uma execução com `$env:SR_VULKAN_NO_STATE_FILTER='1'` (`-Name c3_t3_nofilter`, depois `Remove-Item env:SR_VULKAN_NO_STATE_FILTER`) para ter a referência A/B. Se `commands` não cair, reverta e reporte.

- [ ] **Passo 8: commit**

```powershell
git add -A port/src/graphics/vulkan tests/vulkan
git commit -m "perf(vulkan): skip redundant descriptor, vertex, index and dynamic state commands

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Tarefa 4: Hash das texturas com XXH3 em AVX2

O hash continua exato e na thread do jogo; só fica mais rápido. O valor é o mesmo (XXH3 não depende do vetor); a seleção é em tempo de execução, resolvida uma vez antes de qualquer thread auxiliar. O despacho lazy do xxhash (`XXH_X86DISPATCH`) fica de fora de propósito (veja `rex/hash.h`: ele escreve em `XXH_g_dispatch` na primeira chamada, perigoso com várias threads).

**Arquivos:**
- Criar: `port/src/native_renderer/xxh3_avx2.h`, `xxh3_avx2.cpp`, `cpu_features.h`, `tests/native/test_xxh3_avx2.cpp`, `tests/native/xxh3_reference.cpp`
- Modificar: `port/src/native_renderer/native_renderer.cpp`, `port/CMakeLists.txt`, `tests/native/CMakeLists.txt`

**Interfaces:**
- Produz: `uint64_t superman_returns::native::Xxh3Avx2(const void*,size_t,uint64_t seed)`; `bool superman_returns::native::Avx2Available()`; em `native_renderer.cpp`, `TextureHash()` devolve o ponteiro de função escolhido uma vez.

- [ ] **Passo 1: teste que falha — `tests/native/test_xxh3_avx2.cpp` e `xxh3_reference.cpp`**

`tests/native/xxh3_reference.cpp`:

```cpp
// Baseline build of the library's XXH3 (no -mavx2): what the game used before the AVX2 build.
#define XXH_INLINE_ALL
#include <xxhash.h>
#include <cstddef>
#include <cstdint>
uint64_t Xxh3Reference(const void* data, size_t size, uint64_t seed) {
  return uint64_t(XXH3_64bits_withSeed(data, size, seed));
}
```

`tests/native/test_xxh3_avx2.cpp`:

```cpp
#ifdef _WIN32
#include "cpu_features.h"
#include "xxh3_avx2.h"
#include "test_main.h"
#include <cstdio>
#include <vector>
uint64_t Xxh3Reference(const void* data, size_t size, uint64_t seed);  // xxh3_reference.cpp
using namespace superman_returns::native;
SR_TEST(avx2_xxh3_matches_the_baseline_xxh3_for_every_length_class_and_seed) {
  if (!Avx2Available()) { std::puts("skip: this CPU has no AVX2"); return; }
  std::vector<uint8_t> bytes((1u << 20) + 16);
  uint32_t state = 12345;
  for (auto& b : bytes) { state = state * 1664525u + 1013904223u; b = uint8_t(state >> 24); }
  // XXH3 switches algorithm at 16, 128, 240 bytes, and its long path works in 1 KiB stripes.
  for (size_t size : {0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 128, 129, 239, 240, 241, 255, 256, 257,
                      1023, 1024, 1025, 4096, 4097, 100003, 262144, 1 << 20}) {
    for (uint64_t seed : {uint64_t(0), uint64_t(0xcbf29ce484222325ull), ~uint64_t(0)}) {
      SR_CHECK_EQ(Xxh3Avx2(bytes.data(), size, seed), Xxh3Reference(bytes.data(), size, seed));
    }
  }
  for (size_t offset : {1, 3, 7, 13}) {  // unaligned starts
    SR_CHECK_EQ(Xxh3Avx2(bytes.data() + offset, 100000, 5), Xxh3Reference(bytes.data() + offset, 100000, 5));
  }
}
#endif
```

Em `tests/native/CMakeLists.txt`, antes de `enable_testing()`:

```cmake
# XXH3 compiled with AVX2 (the only file built with -mavx2) against the baseline build. Needs the SDK's xxhash.h
# (.tools/rexglue-sdk); without it these tests are left out.
set(SR_XXHASH_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../.tools/rexglue-sdk/win-amd64/include")
if(WIN32 AND EXISTS "${SR_XXHASH_INCLUDE}/xxhash.h")
    target_sources(sr_native_tests PRIVATE test_xxh3_avx2.cpp xxh3_reference.cpp "${SR_NATIVE_DIR}/xxh3_avx2.cpp")
    set_source_files_properties("${SR_NATIVE_DIR}/xxh3_avx2.cpp" PROPERTIES
        COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2,-mavx2>")
    set_source_files_properties(xxh3_reference.cpp "${SR_NATIVE_DIR}/xxh3_avx2.cpp" PROPERTIES
        INCLUDE_DIRECTORIES "${SR_XXHASH_INCLUDE}")
else()
    message(STATUS "xxhash.h not found: the AVX2 XXH3 tests are left out")
endif()
```

- [ ] **Passo 2: rodar e confirmar a falha**

Rodar os **Testes unitários nativos**. Esperado: erro de compilação (`cpu_features.h`/`xxh3_avx2.h` não existem).

- [ ] **Passo 3: os três arquivos novos**

`port/src/native_renderer/cpu_features.h`:

```cpp
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace superman_returns::native {
// True when the CPU and the OS run AVX2. Deliberately outside xxh3_avx2.cpp: that file is built with -mavx2.
inline bool Avx2Available() { return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0; }
}  // namespace superman_returns::native
```

`port/src/native_renderer/xxh3_avx2.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
namespace superman_returns::native {
// XXH3_64bits_withSeed built with AVX2 (xxh3_avx2.cpp, the only file compiled with -mavx2). The value is the same
// as the baseline XXH3's; only the speed changes. Call it only when Avx2Available() (cpu_features.h).
uint64_t Xxh3Avx2(const void* data, size_t size, uint64_t seed);
}  // namespace superman_returns::native
```

`port/src/native_renderer/xxh3_avx2.cpp`:

```cpp
// Built with -mavx2 (port/CMakeLists.txt, tests/native/CMakeLists.txt). Nothing else may live in this file: any
// inline function emitted here could be picked by the linker for the whole program.
#define XXH_INLINE_ALL
#include <xxhash.h>
#include "xxh3_avx2.h"
namespace superman_returns::native {
uint64_t Xxh3Avx2(const void* data, size_t size, uint64_t seed) { return uint64_t(XXH3_64bits_withSeed(data, size, seed)); }
}  // namespace superman_returns::native
```

- [ ] **Passo 4: rodar os testes nativos**

Esperado: `avx2_xxh3_matches_the_baseline_xxh3_...` passa (ou imprime `skip` numa CPU sem AVX2 — este notebook tem). Se o clang reclamar de `-mavx2` ou `xxhash.h`, confira o `INCLUDE_DIRECTORIES` do passo 1.

- [ ] **Passo 5: ligar no jogo — `port/CMakeLists.txt`**

Na lista de fontes de `add_library(sr_native OBJECT ...)`, acrescentar depois de `${SR_NATIVE_ROOT}/hang_watchdog.cpp`:

```cmake
            ${SR_NATIVE_ROOT}/xxh3_avx2.cpp
```

e, depois do bloco `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU") target_compile_options(sr_native ...) endif()`:

```cmake
        # The only file built with AVX2: selected at run time (cpu_features.h), never called without it.
        set_source_files_properties(${SR_NATIVE_ROOT}/xxh3_avx2.cpp PROPERTIES
            COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2,-mavx2>")
```

- [ ] **Passo 6: usar no `native_renderer.cpp`**

Depois de `#include "guest_hash.h"` (linha 9) acrescentar:

```cpp
#include "cpu_features.h"
#include "xxh3_avx2.h"
```

No namespace anônimo onde ficam `CaptureTimings` e `GuestSource` (depois de `GuestSource`), acrescentar:

```cpp
uint64_t Xxh3Baseline(const void* data,size_t size,uint64_t seed) {return uint64_t(XXH3_64bits_withSeed(data,size,seed));}
using Xxh3Fn=uint64_t(*)(const void*,size_t,uint64_t);
// The texture hash: the same XXH3 value either way, the AVX2 build when the CPU has it. Chosen once, on the first
// call (the guest thread, before any helper thread exists); SR_NATIVE_HASH_NO_AVX2=1 forces the baseline (A/B).
Xxh3Fn TextureHash() {
  static const Xxh3Fn fn=[]() -> Xxh3Fn {
    const char* off=std::getenv("SR_NATIVE_HASH_NO_AVX2");
    const bool avx2=superman_returns::native::Avx2Available() && !(off && off[0] && off[0]!='0');
    return avx2?&superman_returns::native::Xxh3Avx2:&Xxh3Baseline;
  }();
  return fn;
}
```

(`<cstdlib>` já vem transitivamente; se não, acrescente `#include <cstdlib>`.) Em `CaptureTextures`, trocar a lambda do primeiro hash:

```cpp
        const bool hashed=HashGuestRange(GuestSource(base,0xa0000000u+range.address,range.length),range.length,hash,
            [](const void* data,size_t size,uint64_t seed) {return uint64_t(XXH3_64bits_withSeed(data,size,seed));},hash);
```

por

```cpp
        const bool hashed=HashGuestRange(GuestSource(base,0xa0000000u+range.address,range.length),range.length,hash,TextureHash(),hash);
```

e o segundo hash (dos bytes copiados): `hash=XXH3_64bits_withSeed(bytes.data(),bytes.size(),hash);` por `hash=TextureHash()(bytes.data(),bytes.size(),hash);`.

- [ ] **Passo 7: testes e medição**

**Testes unitários nativos** e **suíte de regressão Vulkan** (o jogo é recompilado no passo de medição). Passo de medição com `<tag>` = `t4`, e uma execução extra com `$env:SR_NATIVE_HASH_NO_AVX2='1'` (`-Name c3_t4_noavx2`, depois `Remove-Item env:SR_NATIVE_HASH_NO_AVX2`) como referência A/B na mesma cena. Critério: `hash_ms` da captura (linha `native Vulkan capture`) abaixo da referência sem AVX2 (esperado de ~14–18 para ~8 ms) e gate `PASS`. Confirme também que o D3D12 continua rodando: um bench rápido `-Api d3d12 -Name c3_t4_d3d12` (o D3D12 usa o mesmo hash).

- [ ] **Passo 8: commit**

```powershell
git add -A port/src/native_renderer port/CMakeLists.txt tests/native
git commit -m "perf(native): hash guest textures with an AVX2 build of the same XXH3, selected at run time

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Tarefa 5: Hash paralelo (só se a Tarefa 4 não bastar)

**Condição de entrada:** depois da Tarefa 4, meça os dois estágios. Se o FPS já passa de 29 (duas execuções) **e** a captura de texturas na thread do jogo ficou ≤ ~28 ms, **pule esta tarefa** e vá para a Tarefa 6. Se a gravação está ≤ 33 ms mas a thread do jogo (jogo + captura) continua acima de 33 ms, faça esta tarefa. Se a gravação ainda passa de 33 ms, a Tarefa 5 sozinha não resolve: reporte antes de seguir.

Cada textura é hasheada inteira por uma única thread, na ordem das faixas, com o seed encadeado: o valor é o mesmo do hash serial. A thread do jogo só segue depois do join, como hoje. Só o caminho `!texture_watch_` (Vulkan) muda; o D3D12 não é tocado.

**Arquivos:**
- Criar: `port/src/native_renderer/job_pool.h`, `texture_hash_batch.h`, `tests/native/test_job_pool.cpp`, `tests/native/test_texture_hash_batch.cpp`
- Modificar: `port/src/native_renderer/native_renderer.cpp`, `native_renderer.h`, `tests/native/CMakeLists.txt`

**Interfaces:**
- Produz: `class JobPool {explicit JobPool(unsigned helpers,unsigned spin_microseconds=500);unsigned helpers() const;void Run(size_t count,const std::function<void(size_t)>&);}`; `struct TextureHashJob {struct Range{const void* source;uint32_t length;};std::vector<Range> ranges;uint64_t seed=0,hash=0;bool ok=false;uint64_t bytes() const;}`; `template<class Hash> void HashTextureJobs(std::span<TextureHashJob>,JobPool*,Hash,uint64_t parallel_bytes)`; cvar `sr_native_hash_threads` (-1 = por núcleos, 0 = nenhuma).

- [ ] **Passo 1: testes que falham**

`tests/native/test_job_pool.cpp`:

```cpp
#include "job_pool.h"
#include "test_main.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
using superman_returns::native::JobPool;
namespace {
void RunAndCheck(JobPool& pool, size_t count) {
  std::vector<std::atomic<int>> hits(count);
  pool.Run(count, [&](size_t i) { hits[i].fetch_add(1); });
  for (size_t i = 0; i < count; ++i) SR_CHECK_EQ(hits[i].load(), 1);
}
}  // namespace
SR_TEST(job_pool_runs_every_index_exactly_once_for_any_helper_count) {
  for (unsigned helpers : {0u, 1u, 3u}) {
    JobPool pool(helpers);
    SR_CHECK_EQ(pool.helpers(), helpers);
    for (size_t count : {size_t(0), size_t(1), size_t(2), size_t(7), size_t(100), size_t(5000)}) RunAndCheck(pool, count);
  }
}
SR_TEST(job_pool_survives_many_back_to_back_runs) {
  JobPool pool(3, 100);
  std::atomic<uint64_t> sum{0};
  uint64_t expected = 0;
  for (size_t run = 0; run < 2000; ++run) {
    const size_t count = 1 + run % 17;
    pool.Run(count, [&](size_t i) { sum.fetch_add(i + 1); });
    for (size_t i = 0; i < count; ++i) expected += i + 1;
  }
  SR_CHECK_EQ(sum.load(), expected);
}
SR_TEST(job_pool_runs_work_after_the_helpers_went_to_sleep) {
  JobPool pool(2, 0);  // no spinning: the helpers sleep at once
  for (int run = 0; run < 5; ++run) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    RunAndCheck(pool, 50);
  }
}
SR_TEST(job_pool_destructor_returns_with_idle_helpers) {
  { JobPool pool(3, 200); RunAndCheck(pool, 10); }
  { JobPool pool(3); }
  SR_CHECK(true);
}
```

`tests/native/test_texture_hash_batch.cpp`:

```cpp
#ifdef _WIN32
#include "texture_hash_batch.h"
#include "test_main.h"
#include <vector>
using namespace superman_returns::native;
namespace {
uint64_t Fnv(const void* data, size_t size, uint64_t seed) {
  uint64_t h = seed ^ 0xcbf29ce484222325ull;
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}
struct Texture {std::vector<std::vector<uint8_t>> parts;};
std::vector<Texture> MakeTextures(size_t count) {
  std::vector<Texture> textures(count);
  uint32_t state = 99;
  for (size_t t = 0; t < count; ++t) {
    const size_t part_count = 1 + t % 3;
    for (size_t p = 0; p < part_count; ++p) {
      std::vector<uint8_t> bytes(500 + (t * 7919 + p * 104729) % 30000);
      for (auto& b : bytes) { state = state * 1664525u + 1013904223u; b = uint8_t(state >> 24); }
      textures[t].parts.push_back(std::move(bytes));
    }
  }
  return textures;
}
std::vector<TextureHashJob> MakeJobs(std::vector<Texture>& textures) {
  std::vector<TextureHashJob> jobs;
  for (auto& texture : textures) {
    TextureHashJob job;job.seed = 0xcbf29ce484222325ull;
    for (auto& part : texture.parts) job.ranges.push_back({part.data(), uint32_t(part.size())});
    jobs.push_back(std::move(job));
  }
  return jobs;
}
uint64_t Expected(const Texture& texture) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (auto& part : texture.parts) h = Fnv(part.data(), part.size(), h);
  return h;
}
}  // namespace
SR_TEST(texture_hash_batch_serial_chains_the_seed_across_ranges) {
  auto textures = MakeTextures(8);auto jobs = MakeJobs(textures);
  HashTextureJobs(std::span<TextureHashJob>(jobs), nullptr, Fnv, 1);
  for (size_t i = 0; i < jobs.size(); ++i) {SR_CHECK(jobs[i].ok);SR_CHECK_EQ(jobs[i].hash, Expected(textures[i]));}
}
SR_TEST(texture_hash_batch_on_the_pool_gives_the_serial_values) {
  auto textures = MakeTextures(64);
  JobPool pool(3);
  auto parallel = MakeJobs(textures);
  HashTextureJobs(std::span<TextureHashJob>(parallel), &pool, Fnv, 1);   // 1 byte threshold: always spread
  for (size_t i = 0; i < parallel.size(); ++i) {SR_CHECK(parallel[i].ok);SR_CHECK_EQ(parallel[i].hash, Expected(textures[i]));}
  auto below = MakeJobs(textures);
  HashTextureJobs(std::span<TextureHashJob>(below), &pool, Fnv, ~uint64_t(0));  // below the threshold: inline
  for (size_t i = 0; i < below.size(); ++i) SR_CHECK_EQ(below[i].hash, parallel[i].hash);
}
SR_TEST(texture_hash_batch_marks_only_the_unreadable_texture_as_failed) {
  auto* pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  SR_CHECK(pages != nullptr);
  if (!pages) return;
  DWORD previous = 0;
  SR_CHECK(VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &previous));
  auto textures = MakeTextures(6);auto jobs = MakeJobs(textures);
  jobs[3].ranges.push_back({pages + 4000, 200});   // runs into the inaccessible page
  JobPool pool(2);
  HashTextureJobs(std::span<TextureHashJob>(jobs), &pool, Fnv, 1);
  for (size_t i = 0; i < jobs.size(); ++i) {
    if (i == 3) SR_CHECK(!jobs[i].ok);
    else {SR_CHECK(jobs[i].ok);SR_CHECK_EQ(jobs[i].hash, Expected(textures[i]));}
  }
  SR_CHECK(VirtualFree(pages, 0, MEM_RELEASE));
}
#endif
```

Em `tests/native/CMakeLists.txt`, na lista de `add_executable(sr_native_tests ...)`, acrescentar `test_job_pool.cpp` e `test_texture_hash_batch.cpp` depois de `test_guest_hash.cpp`.

- [ ] **Passo 2: rodar e confirmar a falha**

Rodar os **Testes unitários nativos**. Esperado: erro de compilação (`job_pool.h`/`texture_hash_batch.h` não existem).

- [ ] **Passo 3: `port/src/native_renderer/job_pool.h`**

```cpp
#pragma once
#include <xmmintrin.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
namespace superman_returns::native {
// A few helper threads that run the indices of one job list alongside the calling thread, which works too and
// returns only after every index has finished. After a job the helpers spin briefly (the next job usually follows
// within microseconds, as the guest thread captures draw after draw), then sleep on a condition variable.
// Every Run gets its own immutable Job, so a helper that is late for one run cannot touch the next one.
class JobPool {
public:
  explicit JobPool(unsigned helpers, unsigned spin_microseconds = 500) : spin_us_(spin_microseconds) {
    for (unsigned i = 0; i < helpers; ++i) threads_.emplace_back([this] { HelperLoop(); });
  }
  ~JobPool() {
    { std::lock_guard<std::mutex> lock(mutex_); stop_.store(true); }
    wake_.notify_all();
    for (auto& thread : threads_) thread.join();
  }
  JobPool(const JobPool&) = delete;
  JobPool& operator=(const JobPool&) = delete;
  unsigned helpers() const { return unsigned(threads_.size()); }
  void Run(size_t count, const std::function<void(size_t)>& fn) {
    if (!count) return;
    if (threads_.empty() || count == 1) { for (size_t i = 0; i < count; ++i) fn(i); return; }
    auto job = std::make_shared<Job>();
    job->fn = &fn;job->count = count;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      job_ = job;generation_.fetch_add(1, std::memory_order_release);
    }
    wake_.notify_all();
    Drain(*job);
    // A helper may still be inside its last index: wait for it (spinning, then yielding).
    for (unsigned spins = 0; job->done.load(std::memory_order_acquire) < count; ++spins) {
      if (spins < 2000) _mm_pause(); else std::this_thread::yield();
    }
  }
private:
  struct Job {
    const std::function<void(size_t)>* fn = nullptr;
    size_t count = 0;
    std::atomic<size_t> next{0}, done{0};
  };
  static void Drain(Job& job) {
    for (;;) {
      const size_t i = job.next.fetch_add(1, std::memory_order_relaxed);
      if (i >= job.count) return;
      (*job.fn)(i);
      job.done.fetch_add(1, std::memory_order_release);
    }
  }
  void HelperLoop() {
    uint64_t seen = 0;
    for (;;) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(spin_us_);
      while (generation_.load(std::memory_order_acquire) == seen && !stop_.load(std::memory_order_relaxed) &&
             std::chrono::steady_clock::now() < deadline) _mm_pause();
      std::shared_ptr<Job> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [&] { return stop_.load() || generation_.load() != seen; });
        if (stop_.load()) return;
        seen = generation_.load();job = job_;
      }
      Drain(*job);
    }
  }
  std::mutex mutex_;
  std::condition_variable wake_;
  std::shared_ptr<Job> job_;
  std::atomic<uint64_t> generation_{0};
  std::atomic<bool> stop_{false};
  unsigned spin_us_;
  std::vector<std::thread> threads_;
};
}  // namespace superman_returns::native
```

- [ ] **Passo 4: `port/src/native_renderer/texture_hash_batch.h`**

```cpp
#pragma once
#include "guest_hash.h"
#include "job_pool.h"
#include <algorithm>
#include <numeric>
#include <span>
#include <vector>
namespace superman_returns::native {
// One texture to hash: its guest memory ranges, in order. The seed chains from one range to the next, exactly as
// the serial loop in Renderer::CaptureTextures does.
struct TextureHashJob {
  struct Range {const void* source;uint32_t length;};
  std::vector<Range> ranges;
  uint64_t seed=0;
  uint64_t hash=0;
  bool ok=false;  // false when a range was unreadable (a null source counts as unreadable)
  uint64_t bytes() const {uint64_t total=0;for(const auto& r:ranges) total+=r.length;return total;}
};
// Hashes every job. With no pool, fewer than two jobs, or fewer than `parallel_bytes` in total, everything runs on
// this thread. Otherwise the jobs are spread over the pool, largest first, each one whole on a single thread, so
// every hash is the one the serial loop gives. Returns when all of them are done.
template <class Hash>
void HashTextureJobs(std::span<TextureHashJob> jobs, JobPool* pool, Hash hash, uint64_t parallel_bytes) {
  auto run = [&](TextureHashJob& job) {
    uint64_t value = job.seed;
    job.ok = true;
    for (const auto& range : job.ranges)
      if (!HashGuestRange(range.source, range.length, value, hash, value)) { job.ok = false; break; }
    job.hash = value;
  };
  uint64_t total = 0;
  for (const auto& job : jobs) total += job.bytes();
  if (!pool || pool->helpers() == 0 || jobs.size() < 2 || total < parallel_bytes) {
    for (auto& job : jobs) run(job);
    return;
  }
  std::vector<size_t> order(jobs.size());
  std::iota(order.begin(), order.end(), size_t(0));
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return jobs[a].bytes() > jobs[b].bytes(); });
  pool->Run(order.size(), [&](size_t i) { run(jobs[order[i]]); });
}
}  // namespace superman_returns::native
```

- [ ] **Passo 5: rodar os testes nativos**

Esperado: os 4 testes de `job_pool` e os 3 de `texture_hash_batch` passam. Rode o executável três vezes seguidas (`1..3 | % { .\build\tests-native\sr_native_tests.exe }`) para pegar corrida intermitente.

- [ ] **Passo 6: `native_renderer.cpp` e `.h` — cvar, pool e pré-passe**

`native_renderer.h`: na seção privada, junto de `captured_textures_` (linha ~665), acrescentar:

```cpp
  std::shared_ptr<superman_returns::native::JobPool> hash_pool_;  // helper threads for texture hashing (Vulkan path only)
```

e, no topo do arquivo, junto dos outros forward declarations (antes de `class Renderer`), se não houver: `namespace superman_returns::native {class JobPool;}`.

`native_renderer.cpp`: acrescentar `#include "job_pool.h"` e `#include "texture_hash_batch.h"` depois de `#include "xxh3_avx2.h"`. Perto da definição de `sr_native_anisotropic_filtering` (linha ~251), acrescentar o cvar:

```cpp
REXCVAR_DEFINE_INT32(sr_native_hash_threads, -1, "Superman Returns Native",
                     "Helper threads that hash guest texture memory every frame (-1 = by core count, 0 = none)")
    .range(-1, 8);
```

No namespace anônimo (depois de `TextureHash()`), acrescentar:

```cpp
// Textures first seen this frame are hashed ahead on helper threads only when the batch is big enough: below
// this, waking a helper costs more than the hash saves.
constexpr uint64_t kParallelHashBytes=1u<<20;
unsigned HashHelperCount() {
  const int32_t configured=REXCVAR_GET(sr_native_hash_threads);
  if(configured>=0) return unsigned(configured);
  const unsigned cores=std::thread::hardware_concurrency();
  return cores>=8?3u:(cores>=6?2u:0u);
}
struct PrehashedTexture {std::array<uint32_t,6> fetch{};uint64_t hash=0;bool ok=false;};
```

Reescrever o começo de `Renderer::CaptureTextures` (da linha `void Renderer::CaptureTextures(uint8_t* base) {` até a linha `if(!IsTextureBound(fetch[0])) continue;` + `auto& entry=captured_textures_[fetch];` inclusive) para ler cada fetch uma vez, fazer o pré-passe e reaproveitar a entrada do mapa. O novo começo é:

```cpp
void Renderer::CaptureTextures(uint8_t* base) {
  // The fetch constants of every slot, read once (the pre-pass and the loop below share them).
  std::array<std::array<uint32_t,6>,32> fetches{};
  std::array<CapturedTextureEntry*,32> entries{};
  uint32_t bound=0;
  for(uint32_t slot=0;slot<32;++slot) {
    auto& fetch=fetches[slot];
    for(uint32_t i=0;i<6;++i) {
      uint32_t reg=Pm4Mirror::kFetchConstantBase+slot*6+i;
      fetch[i]=capture_mirror_.written(reg) ? capture_mirror_.reg(reg)
          : Load32(base,cur_.device+kDev.fetch_constants+slot*24+i*4);
    }
    if(IsTextureBound(fetch[0])) bound|=1u<<slot;
  }
  // Pre-pass (the Vulkan path, no write watch): the textures that this draw is the first to need in the frame are
  // hashed together, on helper threads when the batch is big. Each texture is hashed whole and in order with the
  // same seed, so the values are exactly those of the serial hash below, which simply uses them.
  std::array<PrehashedTexture,32> prehashed;uint32_t prehashed_count=0;
  if(!texture_watch_ && bound) {
    std::vector<superman_returns::native::TextureHashJob> jobs;std::array<uint32_t,32> job_slot;uint64_t pending_bytes=0;
    for(uint32_t slot=0;slot<32;++slot) {
      if(!(bound&(1u<<slot))) continue;
      const auto& fetch=fetches[slot];
      auto found=captured_textures_.find(fetch);
      if(found!=captured_textures_.end()) {
        entries[slot]=&found->second;
        const auto& entry=found->second;
        if(!entry.snapshot && entry.failed_frame!=~0ull && front_frame_<entry.failed_frame+16) continue;  // reported by the loop
        if(entry.snapshot && entry.checked_frame==front_frame_) continue;                                  // already checked this frame
      }
      bool duplicate=false;
      for(size_t j=0;j<jobs.size();++j) if(fetches[job_slot[j]]==fetch) {duplicate=true;break;}
      if(duplicate) continue;
      std::string error;std::vector<graphics::guest::TextureRange> ranges;
      if(!graphics::guest::DescribeTextureRanges(fetch,ranges,error)) continue;  // the loop below records the error
      superman_returns::native::TextureHashJob job;job.seed=0xcbf29ce484222325ull;
      for(const auto& range:ranges) job.ranges.push_back({GuestSource(base,0xa0000000u+range.address,range.length),range.length});
      pending_bytes+=job.bytes();job_slot[jobs.size()]=slot;jobs.push_back(std::move(job));
    }
    if(jobs.size()>=2 && pending_bytes>=kParallelHashBytes) {
      if(!hash_pool_) hash_pool_=std::make_shared<superman_returns::native::JobPool>(HashHelperCount());
      const auto started=std::chrono::steady_clock::now();
      superman_returns::native::HashTextureJobs(std::span(jobs),hash_pool_.get(),TextureHash(),kParallelHashBytes);
      capture_timings.hash_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
      for(size_t j=0;j<jobs.size();++j) prehashed[prehashed_count++]={fetches[job_slot[j]],jobs[j].hash,jobs[j].ok};
    }
  }
  for (uint32_t slot=0;slot<32;++slot) {
    if(!(bound&(1u<<slot))) continue;
    const auto& fetch=fetches[slot];
    auto& entry=entries[slot]?*entries[slot]:captured_textures_[fetch];
```

Na primeira linha do bloco acima, o tipo de `entries` é `std::array<CapturedTextureEntry*,32>` (a struct é privada da classe, mas este é um método dela): escreva `std::array<CapturedTextureEntry*,32> entries{};`. O restante do laço continua igual ao atual, exceto o ponto do hash (próximo passo). `captured_textures_` é um `std::map`, então os ponteiros em `entries` continuam válidos mesmo se o laço inserir outras entradas.

Trocar o bloco do hash (hoje: `uint64_t hash=0xcbf29ce484222325ull; entry.watch_seq=...; for(const auto& range:ranges) {...}`) por:

```cpp
      uint64_t hash=0xcbf29ce484222325ull;
      entry.watch_seq=write_seq_.load(std::memory_order_acquire);
      const PrehashedTexture* ahead=nullptr;
      for(uint32_t j=0;j<prehashed_count;++j) if(prehashed[j].fetch==fetch) {ahead=&prehashed[j];break;}
      if(ahead) {
        for(const auto& range:ranges) {++capture_timings.reads;capture_timings.bytes+=range.length;}
        if(ahead->ok) hash=ahead->hash; else error="Texture memory is not readable";
      } else for(const auto& range:ranges) {
        auto started=std::chrono::steady_clock::now();
        if(texture_watch_) ArmTextureWatch(range.address,range.length);
        capture_timings.watch_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        // Hash the guest bytes where they are. Copying ~100 MB per frame only to learn that nothing
        // changed cost ~30 ms of the guest thread; the copy now happens only for a changed texture.
        ++capture_timings.reads;capture_timings.bytes+=range.length;
        started=std::chrono::steady_clock::now();
        const bool hashed=HashGuestRange(GuestSource(base,0xa0000000u+range.address,range.length),range.length,hash,TextureHash(),hash);
        capture_timings.hash_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        if(!hashed) {error="Texture memory is not readable";break;}
      }
```

(O `if(!error.empty()) {...continue;}` que vem logo depois fica como está.) Confira ainda que as variáveis `fetch` usadas no resto do laço (`entry.snapshot`, `DescribeTextureRanges(fetch,...)`, `CaptureTexture(fetch,...)`, `captured_textures_` pruning no fim) continuam compilando com `const auto& fetch=fetches[slot];` (`CaptureTexture`/`DescribeTextureRanges` recebem o array por referência constante; se alguma assinatura pedir não-`const`, copie para uma variável local).

- [ ] **Passo 7: testes, regressão e medição**

**Testes unitários nativos**, **suíte de regressão Vulkan** e o passo de medição com `<tag>` = `t5`. Para a referência sem pool na mesma cena, faça uma execução extra (`-Name c3_t5_threads0`) com o cvar em 0: acrescente temporariamente `'--sr_native_hash_threads=0'` à lista `$gameArgs` de `tools\bench\bench_api.ps1`, rode, e **desfaça a edição** (`git checkout -- tools/bench/bench_api.ps1`).

Critérios: a linha `native Vulkan capture` mostra `textures_ms` e `hash_ms` menores com o pool do que com `threads=0` na mesma tarefa, o gate `PASS`, nenhum travamento nem crash em três execuções, e a thread do jogo cabe em ~33 ms. Se o ganho for pequeno (< ~15%), tente `kParallelHashBytes` de 512 KB e 2 MB e o `spin_microseconds` do `JobPool` (0, 500, 2000) **uma vez cada**; escolha o melhor ou, se nada ajudar, **reverta a tarefa e reporte** (o pool não compensa neste notebook). O D3D12 deve rodar igual (bench rápido `-Api d3d12 -Name c3_t5_d3d12`; o caminho dele não muda).

- [ ] **Passo 8: commit**

```powershell
git add -A port/src/native_renderer tests/native
git commit -m "perf(native): hash newly seen guest textures of a draw on helper threads (exact, same values)

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Rodada 2: dieta da thread de gravação guiada pela investigação (Tarefas 7 a 9)

Decisão do usuário (depois da Tarefa 4): mais uma rodada de dieta por draw, sem threads novas. A investigação (`.superpowers/sdd/c3-investigation-report.md`, números em ms por quadro, com ~2.900 draws) mostrou: os ~10 ms "sem fase" do perfil são um artefato (cada `Lap` trunca para µs); a gravação pode ir de ~47 para ~28–33 ms só com mudanças exatas e de baixo risco; um protótipo delas (`.superpowers/sdd/c3-prototype.diff`, **referência, não aplicar às cegas**: foi feito sem testes nem revisão) passou no gate, mas o FPS só foi para ~20, porque o worker de replay (~40 ms), a captura na thread do jogo (22–26 ms) e a GPU (32–35 ms) passam a mandar. Estas tarefas reimplementam o conjunto do protótipo limpo, testado e em três commits; o que sobra fora da thread de gravação fica para uma decisão do usuário.

Restrições (as mesmas do plano): imagem idêntica; texturas exatas por quadro; todo recurso que um draw grava passa por `Hold` antes dos comandos; sem gravação em várias threads; gate de imagem `PASS` e os 15 fixtures + `hold-lifetime` + contrato continuam passando; custo medido em ns por draw com o `Lap` em nanossegundos (a partir da Tarefa 7 o perfil soma `record`).

## Tarefa 7: Perfil em ns e buscas baratas (itens 14, 2, 1, 13 da investigação)

**Arquivos:** `port/src/graphics/vulkan/game_renderer.h` (`Lap`, `RecordProfile`), `game_frame.cpp` (linha de log), `resources.h` (`buffers_`, `textures_`), `descriptor_sets.cpp` (`Shared`), `game_renderer.cpp` (`Draw`); testes em `tests/vulkan/test_resources.cpp`.

- [ ] **Passo 1: `Lap` acumula nanossegundos.** `RecordProfile::us` passa a guardar nanossegundos de cada fase (renomeie para `ns`); a linha `Vulkan profile (ms/frame ...)` converte para ms com a mesma formatação. Critério: a soma das fases fica a ~1 ms de `record`.
- [ ] **Passo 2: `ResourceStore::buffers_` e `textures_` viram `std::unordered_map`** (reserve inicial grande, ex. 4096). Nenhum código itera nesses mapas em ordem (confirme com `grep`); `FindBuffer`/`FindTexture` devolvem ponteiros para o valor, que continuam estáveis (nós). Teste: os testes existentes de `test_resources.cpp` seguem passando; acrescente um teste que insere/consulta/substitui/esquece ~5000 ids e confere os valores (cobre rehash).
- [ ] **Passo 3: memo por dimensão em `DescriptorStore::Shared`.** Troque o memo de um id (`last_texture`) por um por dimensão (`last_id[3]`/`last_ptr[3]`), mantendo exatamente as mesmas palavras de chave e o mesmo hash. Critério: `descriptor_cache hits/misses` no perfil parecidos com antes; o fixture `hold-lifetime` continua passando.
- [ ] **Passo 4: cópias evitáveis em `Draw`.** `const auto& capture=draw.textures[slot];` e sem cópia de `ShaderResult`/`shared_ptr` onde só se lê (passe, pipeline, `Texture()` quando só se usa o valor).
- [ ] **Passo 5: testes, medição e commit.** Suíte de regressão Vulkan e os testes nativos; passo de medição com `<tag>` = `t7` (`-Profile -Gate check`). Critério: `uploads` + `descriptors` em ns/draw abaixo da linha de base `c3_t4` (esperado: uploads ~5,7→~3,5 ms, descriptors ~14→~9 ms por quadro); gate `PASS`. Commit: `perf(vulkan): nanosecond profile laps, hashed resource maps and a per-dimension descriptor key memo`.

## Tarefa 8: Alias, pipeline e posse sem repetição (itens 3, 4, 12, 11 da investigação)

**Arquivos:** `game_renderer.cpp` (`Draw`), `render_targets.h/.cpp` (`TargetStore::Aliases`, `PreparePass`), `game_pipeline.h/.cpp` (`GamePipelineStore::Acquire`), `resources.h` (se `Hold` precisar de ajuste); testes em `tests/vulkan/`.

- [ ] **Passo 1: pular `Aliases` com o passe aberto.** Quando `open_pass_==pass` (o mesmo passe já aberto), `targets_.Aliases(*pass)` não é chamado: o invariante é que tudo que escreve num alvo (clear, resolve, alias de depth, outro passe) fecha o passe antes, então o plano é sempre vazio. Como a premissa é de runtime, acrescente a verificação de diagnóstico `SR_VULKAN_CHECK_ALIASES=1` (lida uma vez): com ela, `Aliases` roda mesmo assim e, se achar trabalho com o passe aberto, registra erro claro e falha o draw. Rode **um bench** com a variável ligada e confirme 0 violações no log; registre no relatório.
- [ ] **Passo 2: memo de pipeline.** Em `GamePipelineStore::Acquire`, memo do último draw sobre as entradas brutas que `PlanGamePipeline` lê (lista no relatório da investigação, item 4: artefatos de VS/PS, `TargetPass*`, primitiva, indexado, restart, registradores 0x200/0x201/0x205/0x104/0x10d, bias/slope, inline e stride, atributos, strides dos streams). A lista de entradas tem de ser **completa**: em caso de dúvida sobre alguma entrada, não memoize. Acerto = mesmo `shared_ptr<GamePipeline>`; use `Hold` (com `held_serial` em `GamePipeline`) no lugar de `Keep` por draw. Teste unitário (sem GPU; aproveite o que `test_game_pipeline.cpp` já tiver) que prove: mesmas entradas → mesmo pipeline sem recalcular; mudar qualquer entrada da lista → recalcula.
- [ ] **Passo 3: `Hold` em `TargetStore::PreparePass`/`TargetPass`** no lugar de `Keep` por draw, com `held_serial` (itens 12 e 11: fast path quando o plano mapeia no passe aberto, **só se** for trivial e exato).
- [ ] **Passo 4: testes, medição e commit.** Regressão Vulkan; passo de medição `t8` (gate `PASS`). Critério: `targets` e `pipeline` em ns/draw abaixo da Tarefa 7 (esperado: targets ~5→~2 ms, pipeline ~4,8→~2,5 ms por quadro); a contagem de pipelines criados no log não muda. Commit: `perf(vulkan): skip alias planning on the open pass, memoize the last pipeline and hold targets once per submission`.

## Tarefa 9: Bindings e comandos (itens 7, 10 da investigação)

**Arquivos:** `descriptors.cpp` (`BuildBindings`), `game_renderer.cpp` (`Draw`); testes em `tests/vulkan/test_descriptors.cpp`.

- [ ] **Passo 1: `BuildBindings` copia só o necessário.** Do bloco de 12 KB, copiar do pacote apenas `vs`, `ps` e `shared[256,512)`; `shared[0,256)` (índices de textura e de sampler) e `shared[512,4096)` (vertex fetch, 224×16 bytes) já são reescritos por inteiro logo depois. Teste (estende `build_bindings_writes_vs_ps_and_patches_shared_in_the_block`): preencha o bloco de saída com um padrão (ex. 0xCD) antes de chamar e confira que **todos** os 12.288 bytes resultantes são iguais aos do `BuildBindings` antigo (copie a implementação antiga para uma função de referência no teste), em pelo menos 3 pacotes diferentes (sem texturas/streams, com texturas e streams, com `inline_vertices`).
- [ ] **Passo 2: um só `vkCmdBindDescriptorSets` quando os sets 1–3 mudam.** Com o filtro ligado: se `SetSharedSets` pede emissão, uma chamada `(0,4,...)` com os 3 offsets; senão a chamada de set 0 sozinha (como hoje). Com o filtro desligado, igual a hoje.
- [ ] **Passo 3: testes, medição e commit.** Regressão Vulkan; passo de medição `t9` (gate `PASS`). Critério: `bindings` e `commands` em ns/draw abaixo da Tarefa 8. Em seguida, duas execuções seguidas e registre `record` (alvo: ≤ 33 ms) e o FPS. Commit: `perf(vulkan): copy only the constants a draw needs and bind descriptor sets in one call when the shared sets change`.

## Tarefa 6: Medição final e documentação

**Arquivos:**
- Modificar: `docs/superpowers/specs/2026-10-06-vulkan-fps-parity-cycle3-design.md` (seção `## Resultado`), `docs/vulkan-m3.md`, `README.md`, `tools/README.md` (se algo mudou)

- [ ] **Passo 1: duas execuções finais seguidas no Vulkan, mais o D3D12**

Com o jogo recompilado na HEAD (passo de medição, recompilar), rode **duas vezes seguidas**:

```powershell
$exe = "$PWD\port\out\build\win-amd64-release\superman_returns.exe"
powershell -File tools\bench\bench_api.ps1 -Api vulkan -Name c3_final1 -Profile -Gate check -Exe $exe
powershell -File tools\bench\bench_api.ps1 -Api vulkan -Name c3_final2 -Profile -Gate check -Exe $exe
powershell -File tools\bench\bench_api.ps1 -Api d3d12 -Name c3_final_d3d12 -Exe $exe
```

Meta: média ≥ 29 FPS parado e andando nas duas execuções do Vulkan. **Se não atingir, não relaxe nenhuma restrição: pare e pergunte** (a próxima opção, gravação em várias threads, exige uma decisão nova).

- [ ] **Passo 2: a suíte completa**

Rode os **Testes unitários nativos**, a **suíte de regressão Vulkan**, `powershell -NoProfile -File tests\tools\test_image_gate.ps1` e `powershell -NoProfile -File tests\tools\test_bench_hud.ps1`. Se os alvos `build/vulkan-main` e o corpus local existirem, rode também `powershell -File tools\verify_vulkan_m3.ps1`.

- [ ] **Passo 3: registrar os resultados**

Acrescentar à seção `## Resultado` do spec do ciclo 3 uma tabela no formato do ciclo 1, com uma linha por tarefa (`Linha de base c3_base`, `1 constantes t1`, `2 posse t2`, `3 filtro t3`, `4a AVX2 t4`, `4b pool t5` ou "não aplicado", `Final c3_final1/2`, `D3D12 c3_final_d3d12`) e as colunas `Parado (FPS)`, `Andando (FPS)`, `bindings / descriptors / commands (µs por draw)`, `textures_ms / hash_ms`, `record (ms)`. Em `docs/vulkan-m3.md` e no `README.md`, atualizar os números de desempenho do Vulkan (siga o formato do ciclo 1) e a descrição do gate de imagem em `tools/README.md` (já feita na Tarefa 0). Registrar também o que não funcionou ou foi revertido.

- [ ] **Passo 4: commit**

```powershell
git add docs README.md tools/README.md
git commit -m "docs: record the cycle 3 Vulkan FPS parity measurements

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

Não faça `git push` nem merge sem o usuário pedir.

---

## Auto-revisão

**Cobertura do spec:**
- Seção 0 (gate de imagem, calibração, teste sintético, `-Gate`): Tarefa 0.
- Seção 1 (constantes sem cópias, `MapTransient`, arrays no stack, `RemapTextureBindings` sem cópia, composição e immediate na mesma fatia): Tarefa 1.
- Seção 2 (`held_serial`/`Hold`, listas reaproveitadas, `DescriptorDraw` por valor, chave em array fixo com hash junto, promoção no acerto, fixture `--hold-lifetime`, teste unitário da lista): Tarefa 2.
- Seção 3 (`StateShadow`, set 0 sempre e sozinho, sets 1–3 só ao mudar, invalidação em `BeginSubmission`/`ClosePass`, desligador `SR_VULKAN_NO_STATE_FILTER`, teste unitário): Tarefa 3.
- Seção 4a (XXH3 AVX2, seleção em tempo de execução, resolvida antes de threads) e 4b (pool, cvar `sr_native_hash_threads`, padrão por núcleos, 0 desliga, join antes de seguir): Tarefas 4 e 5.
- Ordem, critério de parada, medição por estágio normalizada por draw, reversão se o estágio não melhorar, parar e perguntar antes da fase 2: cabeçalho, passos de medição e Tarefa 6.

**Pontos em que o plano se afasta do texto do spec (e por quê):**
- O `ResourceStore::MapTransient` devolve uma `TransientSlice` com o `shared_ptr` do chunk (e não `{VkBuffer, offset, ptr}`): `ConstantSet` precisa do `shared_ptr` do chunk para guardar o `weak_ptr` dono; copiar um `shared_ptr` por draw custa duas operações atômicas, sem alocação.
- O pré-passe do hash (Tarefa 5) agrupa as texturas novas de **um draw** (a captura roda por draw); a paralelização só vale para lotes grandes, por isso o limiar `kParallelHashBytes` e a medição obrigatória.
- Os fixtures de GPU ficam na lista da `verify_vulkan_m3.ps1`; o `verify` completo exige alvos que o `clean.ps1` apaga (`build/vulkan-main`), por isso as tarefas usam a suíte de regressão explícita.

**Consistência de tipos entre tarefas:** `TransientSlice`/`MapTransient`/`FlushTransient` (T1) são usados por `Prepare` (T1, T2), `composition.cpp`, `immediate.cpp`, `game_renderer.cpp` e o fixture da T2. `Prepare` devolve `shared_ptr<DescriptorDraw>` na T1 e `bool` com `DescriptorDraw&` na T2 (a T2 lista todos os chamadores). `Hold` (T2) usa `held_serial` em `BufferResource`, `TextureResource` e `DescriptorCacheEntry`. `StateShadow` (T3) consome `DescriptorDraw` por valor (`descriptors.sets`). `TextureHash()` (T4) é o functor passado a `HashTextureJobs` (T5).
