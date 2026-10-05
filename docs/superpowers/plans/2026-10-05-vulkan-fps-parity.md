# Vulkan nativo: paridade de FPS com o D3D12 — Plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans para implementar este plano tarefa por tarefa. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fazer o renderer nativo Vulkan atingir média ≥ 29 FPS parado e andando na Intel UHD deste notebook, onde o D3D12 faz 30, sem mudar a imagem.

**Architecture:** Três cortes de custo de CPU, medidos em ordem: (a) hashear texturas no lugar em vez de copiá-las, (b) tirar as esperas de fence e de vblank de dentro do `gpu_mutex`, (c) trocar o descriptor set de constantes por draw por offsets dinâmicos. Um memo do draw anterior fecha o plano e só roda se o bench ainda ficar abaixo da meta.

**Tech Stack:** C++20 (clang++ 23, alvo Windows MSVC), Vulkan 1.1, XXH3, SEH (`__try/__except`), PowerShell, harness `SR_TEST` de `tests/native` e `tests/vulkan`.

**Spec:** `docs/superpowers/specs/2026-10-05-vulkan-fps-parity-design.md` (commit `ea830c5`).

## Global Constraints

- Meta: média **≥ 29 FPS parado e andando** no `tools\bench.ps1` (New Game, janela 1280x720, limite de 30 FPS), na Intel UHD deste notebook.
- Imagem idêntica: só otimização interna de CPU. Sem atraso em texturas dinâmicas. Sem mudança arquitetural grande.
- O D3D12 não pode regredir (hoje 29,5 a 30 FPS) e o caminho dele não é alterado.
- A verificação de texturas continua **exata, uma vez por quadro e por textura** (sem backoff).
- Cada passo é um commit próprio. Depois de cada tarefa de otimização, medir o bench no Vulkan; se as duas medições (parado e andando) passarem de 29 FPS, pular para a Task 5.
- Se após as Tasks 1 a 3 (e a 4, se executada) ainda ficar abaixo de 29 FPS, **parar e perguntar** antes de relaxar qualquer restrição. O próximo candidato seria hashear texturas em paralelo.
- Os arquivos do repositório usam CRLF. Depois de cada edição, `git diff --stat` precisa mostrar só as linhas mudadas, não o arquivo inteiro.
- Commits terminam com `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Nada de `git push` sem pedido do autor.
- Depois de mexer em código, `graphify update .` (regra do `CLAUDE.md`).

## File Structure

| Arquivo | Responsabilidade | Tarefa |
| --- | --- | --- |
| `tools/dev_env.ps1` (novo) | Carrega o ambiente do Visual Studio e o clang/cmake/ninja do projeto na sessão PowerShell | 0 |
| `tools/bench_api.ps1` (novo) | Roda o `bench.ps1` com os argumentos do launcher para uma API | 0 |
| `tools/vulkan_profile_summary.ps1` (novo) | Resume as linhas de perfil de um log do Vulkan | 0 |
| `port/src/native_renderer/guest_hash.h` (novo) | `HashGuestRange`: hash no lugar com proteção SEH | 1 |
| `tests/native/test_guest_hash.cpp` (novo) | Testes do hash no lugar | 1 |
| `port/src/native_renderer/native_renderer.cpp` | `GuestSource` e revalidação de textura sem cópia | 1 |
| `port/src/graphics/vulkan/frame_loop.h`, `frame_loop.cpp` | `FrameWork::queue_mutex` e esperas fora do lock | 2 |
| `port/src/graphics/vulkan/platform/native_provider.cpp` | Passa o mutex ao `DrawGame` em vez de segurá-lo | 2 |
| `tests/vulkan/test_frame_loop.cpp` | Teste de quem segura o mutex em cada chamada | 2 |
| `port/src/graphics/vulkan/device_requirements.cpp` | Set 0 com `STORAGE_BUFFER_DYNAMIC` | 3 |
| `port/src/graphics/vulkan/descriptor_sets.h`, `descriptor_sets.cpp` | Um set de constantes por chunk e offsets dinâmicos | 3 |
| `port/src/graphics/vulkan/game_renderer.cpp`, `composition.cpp`, `immediate.cpp` | Passam os offsets no `vkCmdBindDescriptorSets` | 3 |
| `tests/vulkan/test_device_requirements.cpp`, `shader_contract_integration.cpp` | Teste do layout e do bind com offsets | 3 |
| `port/src/graphics/vulkan/descriptors.h`, `descriptors.cpp` | `BindingSignature` | 4 |
| `port/src/graphics/vulkan/resources.h`, `resources.cpp` | `ResourceStore::Generation()` | 4 |
| `tests/vulkan/test_descriptors.cpp` | Testes da assinatura | 4 |
| `docs/vulkan-m3.md`, `README.md`, spec | Resultados medidos | 5 |

---

### Task 0: Ferramentas de desenvolvimento e linha de base

**Files:**
- Create: `tools/dev_env.ps1`
- Create: `tools/bench_api.ps1`
- Create: `tools/vulkan_profile_summary.ps1`

**Interfaces:**
- Produces: `. .\tools\dev_env.ps1` (dot-source), `tools\bench_api.ps1 -Api vulkan|d3d12 -Name <rótulo> [-Profile] [-Exe <caminho>]` e `tools\vulkan_profile_summary.ps1 -Log <arquivo>`. As tarefas seguintes usam os três.

- [ ] **Step 1: Criar `tools/dev_env.ps1`**

```powershell
# Dot-source: . .\tools\dev_env.ps1
# Carrega o ambiente x64 do Visual Studio e o clang/cmake/ninja do projeto nesta sessão PowerShell,
# para compilar e rodar os testes de tests/native e tests/vulkan. Só mexe em variáveis de ambiente
# (as variáveis locais levam o prefixo srEnv para não colidir com as da sessão).
$srEnvRoot = Split-Path $PSScriptRoot -Parent
$srEnvVs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
if (-not $srEnvVs) { throw 'Visual Studio Build Tools not found' }
foreach ($srEnvLine in (cmd /c "`"$srEnvVs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 >nul 2>&1 && set")) {
  if ($srEnvLine -match '^([^=]+)=(.*)$' -and $Matches[1] -notmatch '[()]') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
$env:PATH = "$srEnvRoot\.tools\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin;$srEnvVs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$srEnvVs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
```

- [ ] **Step 2: Criar `tools/bench_api.ps1`**

```powershell
<#
Roda tools\bench.ps1 com os argumentos que o launcher passa para a API escolhida (janela 1280x720,
limite de 30 FPS, opções de melhoria no padrão do jogo). O resultado vai para logs\bench_results.csv.

Uso: tools\bench_api.ps1 -Api vulkan|d3d12 -Name <rótulo> [-Profile] [-Exe <caminho>]
  -Profile liga SR_VULKAN_PROFILE=1 e copia o log do jogo para logs\bench_<Name>.log.
  -Exe     padrão: port\out\build\win-amd64-dist\superman_returns.exe
#>
param(
  [Parameter(Mandatory)] [ValidateSet('vulkan', 'd3d12')] [string]$Api,
  [Parameter(Mandatory)] [string]$Name,
  [string]$Exe = '',
  [switch]$Profile
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = "$root\port\out\build\win-amd64-dist\superman_returns.exe" }
$gameArgs = @(
  '--sr_renderer=native', '--sr_preset=custom', "--sr_native_api=$Api", '--sr_render_scale=100',
  '--window_width=1280', '--window_height=720', '--vsync=true', '--d3d12_adapter=-1',
  '--sr_native_fps_limit=30', '--sr_native_render_scale=1', '--sr_native_anisotropic_filtering=-1',
  '--sr_native_fxaa=false', '--sr_native_shadow_quality=1', '--sr_native_msaa_samples=1',
  '--mnk_mode=true', '--mnk_mouse=true'
) -join ' '
if ($Profile) { $env:SR_VULKAN_PROFILE = '1' }
& "$PSScriptRoot\bench.ps1" -Name $Name -Exe $Exe -ExtraArgs $gameArgs -TitleTimeout 180 -WorldTimeout 200
if ($Profile) { Copy-Item "$root\logs\game.log" "$root\logs\bench_$Name.log" -Force }
```

- [ ] **Step 3: Criar `tools/vulkan_profile_summary.ps1`**

```powershell
# Resume um log do Vulkan gravado com SR_VULKAN_PROFILE=1: as 3 últimas médias por 120 quadros do
# Vulkan profile (gravação) e as 3 últimas linhas da captura de texturas (thread do jogo).
# Uso: tools\vulkan_profile_summary.ps1 -Log logs\bench_<Name>.log
param([Parameter(Mandatory)] [string]$Log)
'--- Vulkan profile (ms/frame, média de 120 quadros)'
Select-String -Path $Log -Pattern 'Vulkan profile \(ms/frame' | Select-Object -Last 3 |
  ForEach-Object { ($_.Line -replace '^.*over 120\): ', '') -replace ' \| draws=.*$', '' }
'--- native Vulkan capture (thread do jogo)'
Select-String -Path $Log -Pattern 'native Vulkan capture frame' | Select-Object -Last 3 |
  ForEach-Object { $_.Line -replace '^.*native Vulkan capture ', '' }
```

- [ ] **Step 4: Verificar que o ambiente carrega**

Run (PowerShell, na raiz do repositório): `. .\tools\dev_env.ps1; clang++ --version; cmake --version; ninja --version`
Expected: três versões impressas (clang 23.1.2, cmake e ninja), sem erro.

- [ ] **Step 5: Linha de base do Vulkan e do D3D12**

O jogo abre sozinho e o script manda teclas: não mexer no teclado nem no mouse durante ~3 minutos por execução.

```powershell
.\tools\bench_api.ps1 -Api vulkan -Name par_base -Profile
.\tools\vulkan_profile_summary.ps1 -Log logs\bench_par_base.log
.\tools\bench_api.ps1 -Api d3d12 -Name par_base_d3d12
```
Expected: Vulkan em torno de 7,7 a 8,3 FPS parado e 8,0 a 10,1 andando (`logs\bench_results.csv`, linhas `par_base`); D3D12 em 29,5 a 30. O resumo mostra `record` ≈ 38, `fence` ≈ 27 a 34, `queue` ≈ 22 a 36 e `textures_ms` ≈ 37 a 46. Se os números forem bem diferentes, parar e investigar antes de seguir.

- [ ] **Step 6: Commit**

```bash
git add tools/dev_env.ps1 tools/bench_api.ps1 tools/vulkan_profile_summary.ps1
git commit -m "tools: dev environment, per-API bench wrapper and Vulkan profile summary"
```

---

### Task 1: Passo a — hash das texturas no lugar

**Files:**
- Create: `port/src/native_renderer/guest_hash.h`
- Create: `tests/native/test_guest_hash.cpp`
- Modify: `tests/native/CMakeLists.txt` (acrescenta `test_guest_hash.cpp`)
- Modify: `port/src/native_renderer/native_renderer.cpp` (include, `GuestSource`, `ReadCommittedGuest`, laço de revalidação em `Renderer::CaptureTextures`)

**Interfaces:**
- Produces: `superman_returns::native::HashGuestRange(const void* source, uint32_t length, uint64_t seed, Hash hash, uint64_t& out) -> bool`, em `guest_hash.h`. `hash` é qualquer chamável `uint64_t(const void*, size_t, uint64_t seed)`. Devolve `false` (sem tocar em `out`) para ponteiro nulo, tamanho 0, tamanho acima de `0x20000000` ou página ilegível.
- Produces: `GuestSource(uint8_t* base, uint32_t address, uint32_t length) -> const uint8_t*` no namespace anônimo de `native_renderer.cpp` (nullptr se fora do espaço do jogo).

- [ ] **Step 1: Escrever o teste que falha**

Criar `tests/native/test_guest_hash.cpp`:

```cpp
#ifdef _WIN32
#include "../../port/src/native_renderer/guest_hash.h"
#include "test_main.h"
#include <vector>
using superman_returns::native::HashGuestRange;
namespace {
// Qualquer hash com seed serve: o helper precisa devolver exatamente o que o hash de uma cópia devolve.
uint64_t Fnv(const void* data, size_t size, uint64_t seed) {
  uint64_t h = seed ^ 0xcbf29ce484222325ull;
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}
}  // namespace
SR_TEST(guest_hash_in_place_matches_hashing_a_copy) {
  for (uint32_t size : {1u, 7u, 4096u, 100003u}) {
    std::vector<uint8_t> bytes(size);
    for (uint32_t i = 0; i < size; ++i) bytes[i] = uint8_t(i * 31 + 7);
    for (uint64_t seed : {uint64_t(0), uint64_t(0xcbf29ce484222325ull), ~uint64_t(0)}) {
      const std::vector<uint8_t> copy = bytes;
      uint64_t out = 0;
      SR_CHECK(HashGuestRange(bytes.data(), size, seed, Fnv, out));
      SR_CHECK_EQ(out, Fnv(copy.data(), copy.size(), seed));
    }
  }
}
SR_TEST(guest_hash_chains_the_seed_across_ranges) {
  std::vector<uint8_t> a(100, 1), b(50, 2);
  uint64_t h = 0xcbf29ce484222325ull;
  SR_CHECK(HashGuestRange(a.data(), 100, h, Fnv, h));
  SR_CHECK(HashGuestRange(b.data(), 50, h, Fnv, h));
  SR_CHECK_EQ(h, Fnv(b.data(), 50, Fnv(a.data(), 100, 0xcbf29ce484222325ull)));
}
SR_TEST(guest_hash_fails_on_inaccessible_partial_and_empty_ranges_without_touching_out) {
  auto* pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  SR_CHECK(pages != nullptr);
  if (!pages) return;
  DWORD previous = 0;
  SR_CHECK(VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &previous));
  uint64_t out = 0x1234;
  SR_CHECK(HashGuestRange(pages, 4096, 0, Fnv, out));
  out = 0x1234;
  SR_CHECK(!HashGuestRange(pages + 4096, 1, 0, Fnv, out));
  SR_CHECK_EQ(out, uint64_t(0x1234));
  SR_CHECK(!HashGuestRange(pages, 4097, 0, Fnv, out));  // o final da faixa cai na página inacessível
  SR_CHECK_EQ(out, uint64_t(0x1234));
  SR_CHECK(!HashGuestRange(nullptr, 1, 0, Fnv, out));
  SR_CHECK(!HashGuestRange(pages, 0, 0, Fnv, out));
  SR_CHECK(VirtualFree(pages, 0, MEM_RELEASE));
}
#endif
```

Em `tests/native/CMakeLists.txt`, logo depois da linha `    test_checked_guest_memory.cpp`, acrescentar a linha `    test_guest_hash.cpp`.

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake -S tests/native -B build/tests-native -G Ninja
cmake --build build/tests-native
```
Expected: FALHA de compilação em `test_guest_hash.cpp` com `'../../port/src/native_renderer/guest_hash.h' file not found`.

- [ ] **Step 3: Implementar o helper**

Criar `port/src/native_renderer/guest_hash.h`:

```cpp
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace superman_returns::native {
// Hasheia memória do jogo onde ela está, sem a cópia de ReadProcessMemory de CheckedGuestReads.
// `hash(data, size, seed)` roda dentro de um frame SEH: uma faixa não confirmada ou ilegível devolve
// false e deixa `out` intacto, como uma leitura checada que falha. O valor é o mesmo que o hash de
// uma cópia daria.
template <class Hash>
bool HashGuestRange(const void* source, uint32_t length, uint64_t seed, Hash hash, uint64_t& out) {
  if (!source || !length || length > 0x20000000u) return false;
  uint64_t value = 0;
  __try {
    value = hash(source, size_t(length), seed);
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
    return false;
  }
  out = value;
  return true;
}
}  // namespace superman_returns::native
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-native
.\build\tests-native\sr_native_tests.exe
```
Expected: `PASS guest_hash_in_place_matches_hashing_a_copy`, `PASS guest_hash_chains_the_seed_across_ranges`, `PASS guest_hash_fails_on_inaccessible_partial_and_empty_ranges_without_touching_out` e a linha final `N tests, 0 failed checks`. (Verificado ao escrever o plano: `clang++ -std=c++20 -Wall -Wextra` aceita `__try` sem flags extras e os três testes passam, inclusive o da página `PAGE_NOACCESS`.)

- [ ] **Step 5: Usar o helper na captura**

Em `port/src/native_renderer/native_renderer.cpp`:

1. Depois da linha `#include "checked_guest_memory.h"` acrescentar a linha `#include "guest_hash.h"`.

2. Substituir a função `ReadCommittedGuest` inteira por estas duas funções:

```cpp
// Ponteiro do host para uma faixa do jogo, ou nullptr se ela cai fora do espaço de endereços.
const uint8_t* GuestSource(uint8_t* base, uint32_t address, uint32_t length) {
  if(uint64_t(address)+length>(uint64_t{1}<<32)) return nullptr;
  if(address>=0xa0000000u && address<0xc0000000u) {
    uint32_t physical=address-0xa0000000u;
    if(uint64_t(physical)+length>0x20000000ull) return nullptr;
    // PM4 references GPU physical memory. A cached virtual alias may be
    // PAGE_NOACCESS while the SDK's physical mapping remains committed.
    return REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(physical);
  }
  return base+address;
}
std::span<const uint8_t> ReadCommittedGuest(uint8_t* base, uint32_t address, uint32_t length) {
  ++capture_timings.reads;capture_timings.bytes+=length;
  const uint8_t* source=GuestSource(base,address,length);
  return source?checked_guest_reads.Read(source,length):std::span<const uint8_t>{};
}
```

3. Em `Renderer::CaptureTextures`, no laço `for(const auto& range:ranges)`, substituir este bloco (do segundo `started=` até a linha de `hash_us`):

```cpp
        started=std::chrono::steady_clock::now();
        auto bytes=ReadCommittedGuest(base,0xa0000000u+range.address,range.length);
        capture_timings.read_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        if(bytes.size()!=range.length) {error="Texture memory is not readable";break;}
        started=std::chrono::steady_clock::now();
        hash=XXH3_64bits_withSeed(bytes.data(),bytes.size(),hash);
        capture_timings.hash_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
```

por:

```cpp
        // Hash the guest bytes where they are. Copying ~100 MB per frame only to learn that nothing
        // changed cost ~30 ms of the guest thread; the copy now happens only for a changed texture.
        ++capture_timings.reads;capture_timings.bytes+=range.length;
        started=std::chrono::steady_clock::now();
        const bool hashed=HashGuestRange(GuestSource(base,0xa0000000u+range.address,range.length),range.length,hash,
            [](const void* data,size_t size,uint64_t seed) {return uint64_t(XXH3_64bits_withSeed(data,size,seed));},hash);
        capture_timings.hash_us+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        if(!hashed) {error="Texture memory is not readable";break;}
```

O `read_ms` do log passa a ser sempre 0 (a leitura virou parte do `hash_ms`); a cópia só aparece em `copy_ms`.

- [ ] **Step 6: Compilar o jogo (configuração `dist`)**

```powershell
$env:SR_BUILD_DIR = "$PWD\port\out\build\win-amd64-dist"; $env:SR_EMBED_SHADERS = 'OFF'; $env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c "$PWD\build.cmd"
```
Expected: termina com `Linking CXX executable superman_returns.exe` e exit code 0. O `__try` não precisa de flag extra com o clang do projeto.

- [ ] **Step 7: Medir**

```powershell
.\tools\bench_api.ps1 -Api vulkan -Name par_a -Profile
.\tools\vulkan_profile_summary.ps1 -Log logs\bench_par_a.log
```
Expected: `textures_ms` cai de 37 a 46 para cerca de 10 a 15; `read_ms=0`; `hash_ms` sobe para a faixa de 8 a 14; `changed` e `new` continuam em 0 ou 1; o FPS sobe em relação a `par_base`. Registrar os números (`logs\bench_results.csv`, linhas `par_a`). Se os dois FPS passarem de 29, pular para a Task 5.

- [ ] **Step 8: Conferir a imagem**

Abrir `logs\bench_par_base_idle.png` e `logs\bench_par_a_idle.png` (ferramenta Read). Expected: a mesma cena de gameplay (Superman, rua, HUD), sem texturas corrompidas, pretas ou trocadas. Qualquer diferença visível = parar e investigar.

- [ ] **Step 9: Commit**

```bash
graphify update .
git add port/src/native_renderer/guest_hash.h port/src/native_renderer/native_renderer.cpp tests/native/test_guest_hash.cpp tests/native/CMakeLists.txt
git commit -m "perf(native): hash textures in place instead of copying them for revalidation"
```

---

### Task 2: Passo b — esperas fora do `gpu_mutex`

**Files:**
- Modify: `port/src/graphics/vulkan/frame_loop.h`
- Modify: `port/src/graphics/vulkan/frame_loop.cpp` (`FrameLoop::DrawGame`)
- Modify: `port/src/graphics/vulkan/platform/native_provider.cpp` (`PaintAndPresentImpl`)
- Test: `tests/vulkan/test_frame_loop.cpp`

**Interfaces:**
- Produces: `FrameWork::queue_mutex` (`std::mutex*`, padrão `nullptr`). Com mutex, `DrawGame` o segura só em `retire`, `prepare`/gravação, `vkQueueSubmit` e `vkQueuePresentKHR`, nunca em `vkWaitForFences` nem `vkAcquireNextImageKHR`. Sem mutex o comportamento é o de hoje.

- [ ] **Step 1: Escrever o teste que falha**

Em `tests/vulkan/test_frame_loop.cpp`:

1. Depois de `#include <stdexcept>` acrescentar:

```cpp
#include <mutex>
#include <string>
#include <thread>
#include <vector>
```

2. Depois de `bool pass_open=false;` acrescentar:

```cpp
std::mutex* probed = nullptr;
std::vector<std::string> lock_states;
// Outra thread só consegue travar o mutex quando a thread do teste não o segura.
bool Held() {
  if (!probed) return false;
  bool acquired = false;
  std::thread([&] { acquired = probed->try_lock(); if (acquired) probed->unlock(); }).join();
  return !acquired;
}
void NoteLock(const char* where) { if (probed) lock_states.push_back(std::string(where) + (Held() ? ":held" : ":free")); }
```

3. Nas funções falsas, acrescentar `NoteLock(...)` na mesma linha do `calls.push_back`:
   - `  calls.push_back("wait");` vira `  calls.push_back("wait");NoteLock("wait");`
   - `  calls.push_back("acquire");` vira `  calls.push_back("acquire");NoteLock("acquire");`
   - `  calls.push_back("submit");` vira `  calls.push_back("submit");NoteLock("submit");`
   - `  calls.push_back("present");` vira `  calls.push_back("present");NoteLock("present");`

4. No construtor de `Fixture`, depois de `acquire_result = submit_result = present_result = VK_SUCCESS;` acrescentar `probed = nullptr;lock_states.clear();`.

5. No fim do arquivo acrescentar:

```cpp
SR_TEST(game_draw_waits_and_acquires_outside_the_queue_mutex_and_submits_under_it) {
  Fixture f;
  std::mutex queue;
  probed = &queue;
  FrameWork work;
  work.queue_mutex = &queue;
  work.prepare = [&](VkCommandBuffer, uint64_t, Error&) { NoteLock("prepare"); return true; };
  work.retire = [&](uint64_t) { NoteLock("retire"); };
  for (int i = 0; i < 3; ++i) {
    image = i;
    SR_CHECK(f.loop.DrawGame(f.c, f.s, work, f.e) == FrameOutcome::kPresented);
  }
  // O terceiro quadro reusa o slot 0: espera o fence, faz o retire do serial 1 sob lock e só então faz o acquire.
  SR_CHECK(lock_states == std::vector<std::string>({
      "wait:free", "acquire:free", "prepare:held", "submit:held", "present:held",
      "wait:free", "acquire:free", "prepare:held", "submit:held", "present:held",
      "wait:free", "retire:held", "acquire:free", "prepare:held", "submit:held", "present:held"}));
  probed = nullptr;
}
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-vulkan
```
Expected: FALHA de compilação: `no member named 'queue_mutex' in 'superman_returns::graphics::vulkan::FrameWork'`.

- [ ] **Step 3: Implementar**

Em `port/src/graphics/vulkan/frame_loop.h`:
- depois de `#include <array>` acrescentar `#include <mutex>`;
- dentro de `struct FrameWork`, depois da linha `std::function<void(uint64_t)> retire;`, acrescentar:

```cpp
  // Mutex que serializa a fila e os recursos compartilhados com a thread de gravação do jogo. DrawGame
  // só o segura em retire, prepare/paint, submit e present; nunca durante uma espera de fence ou de
  // imagem do swapchain (um vblank, com FIFO). Nulo = sem lock.
  std::mutex* queue_mutex = nullptr;
```

Em `port/src/graphics/vulkan/frame_loop.cpp`, dentro de `FrameLoop::DrawGame`:

1. Substituir

```cpp
  auto &slot = slots_[cursor_];
  auto &f = c.f;
  VkResult r = f.vkWaitForFences(c.device, 1, &slot.fence, VK_TRUE, 100000000);
  if (r == VK_TIMEOUT)
    return FrameOutcome::kSuspended;
  if (r != VK_SUCCESS)
    return fail(r, "WaitForFences");
  if(slot.serial && slot.retire) {slot.retire(slot.serial);slot.serial=0;slot.retire={};}
  uint32_t index = 0;
```

por

```cpp
  auto &slot = slots_[cursor_];
  auto &f = c.f;
  // The queue mutex is shared with the guest frame thread. Waiting for a fence or for a swapchain
  // image (a vblank under FIFO) must not hold it, or that thread cannot submit meanwhile: it is
  // taken only for work that touches the queue or shared resources.
  std::unique_lock<std::mutex> queue_lock;
  if (work.queue_mutex) queue_lock = std::unique_lock<std::mutex>(*work.queue_mutex, std::defer_lock);
  auto lock_queue = [&] { if (queue_lock.mutex() && !queue_lock.owns_lock()) queue_lock.lock(); };
  VkResult r = f.vkWaitForFences(c.device, 1, &slot.fence, VK_TRUE, 100000000);
  if (r == VK_TIMEOUT)
    return FrameOutcome::kSuspended;
  if (r != VK_SUCCESS)
    return fail(r, "WaitForFences");
  if(slot.serial && slot.retire) {
    lock_queue();
    slot.retire(slot.serial);slot.serial=0;slot.retire={};
    if (queue_lock.owns_lock()) queue_lock.unlock();
  }
  uint32_t index = 0;
```

2. Substituir a linha `  const uint64_t serial=++next_serial_;` por:

```cpp
  lock_queue();  // from here on: prepare, record, submit and present
  const uint64_t serial=++next_serial_;
```

Em `port/src/graphics/vulkan/platform/native_provider.cpp`, em `PaintAndPresentImpl`:
- substituir `    std::lock_guard lock(host_->gpu_mutex);if(!swapchain_.handle) return PaintResult::kNotPresented;` por `    if(!swapchain_.handle) return PaintResult::kNotPresented;`
- substituir a linha `    FrameWork work;` por:

```cpp
    FrameWork work;
    work.queue_mutex=&host_->gpu_mutex;  // DrawGame locks it only around recording, submit, present and retire
```
(Conferir antes com `grep -c "FrameWork work;" port/src/graphics/vulkan/platform/native_provider.cpp`: tem que dar 1.)

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe
```
Expected: `PASS game_draw_waits_and_acquires_outside_the_queue_mutex_and_submits_under_it` e todos os testes antigos de frame loop continuam `PASS`; a linha final `0 failed checks`.

- [ ] **Step 5: Regressão de GPU**

```powershell
$gpu = '.\build\tests-vulkan\sr_vulkan_resources_test.exe'
foreach ($fixture in 'targets','pipeline-cache','game-record','game-record-merged','resolve-copy','resolve-record','depth-resolve','edram-alias','alias-defaults','resolve-sample','game-frame','game-frame-async','immediate','stacked-resolve','composition') {
  & $gpu "--$fixture"; if ($LASTEXITCODE -ne 0) { "FALHOU: $fixture" }
}
```
Expected: nenhuma linha `FALHOU`. (O `sr_vulkan_resources_test.exe` precisa ser recompilado antes com `cmake --build build/tests-vulkan`, já feito no Step 4.)

- [ ] **Step 6: Compilar o jogo e medir**

```powershell
$env:SR_BUILD_DIR = "$PWD\port\out\build\win-amd64-dist"; $env:SR_EMBED_SHADERS = 'OFF'; $env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c "$PWD\build.cmd"
.\tools\bench_api.ps1 -Api vulkan -Name par_b -Profile
.\tools\vulkan_profile_summary.ps1 -Log logs\bench_par_b.log
```
Expected: `queue` e `fence` caem de ~25 a 35 ms para poucos ms; `swap_enqueue` no perfil do worker cai; o FPS sobe em relação a `par_a`. Registrar os números (`par_b`). Conferir a imagem como no Task 1 Step 8, com `logs\bench_par_b_idle.png`. Se os dois FPS passarem de 29, pular para a Task 5. Rodar também `.\tools\bench_api.ps1 -Api d3d12 -Name par_b_d3d12` e confirmar que continua em ~30.

- [ ] **Step 7: Commit**

```bash
graphify update .
git add port/src/graphics/vulkan/frame_loop.h port/src/graphics/vulkan/frame_loop.cpp port/src/graphics/vulkan/platform/native_provider.cpp tests/vulkan/test_frame_loop.cpp
git commit -m "perf(vulkan): wait for fences and swapchain images outside the queue mutex"
```

---

### Task 3: Passo c — constantes por offsets dinâmicos

**Files:**
- Modify: `port/src/graphics/vulkan/device_requirements.cpp`
- Modify: `port/src/graphics/vulkan/descriptor_sets.h`
- Modify: `port/src/graphics/vulkan/descriptor_sets.cpp`
- Modify: `port/src/graphics/vulkan/game_renderer.cpp`, `composition.cpp`, `immediate.cpp`
- Test: `tests/vulkan/test_device_requirements.cpp`, `tests/vulkan/shader_contract_integration.cpp`

**Interfaces:**
- Consumes: `ResourceStore::UploadTransient(guest::ResourceId, std::span<const std::byte>, VkDeviceSize reserve, Error&) -> std::shared_ptr<BufferResource>` (a sub-alocação devolvida tem `owner` = chunk da arena, `handle` = handle do chunk e `offset` = início do bloco).
- Alinhamento: `UploadTransient` alinha o bloco a `lcm(max(16, minStorageBufferOffsetAlignment), nonCoherentAtomSize)`, então o offset dinâmico é válido; os offsets estáticos dos três bindings (0, 4096 e 8192 bytes) são múltiplos de qualquer alinhamento permitido (no máximo 256).
- Produces: `DescriptorDraw::dynamic_offsets` (`std::array<uint32_t,3>`, os três iguais ao offset do bloco). Todo `vkCmdBindDescriptorSets` com `DescriptorDraw::sets` passa a usar `uint32_t(draw->dynamic_offsets.size())` e `draw->dynamic_offsets.data()`. O set 0 do layout do jogo passa a ser `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC` (3 bindings; o Vulkan garante pelo menos 4 por set).

- [ ] **Step 1: Escrever o teste que falha**

Acrescentar no fim de `tests/vulkan/test_device_requirements.cpp`:

```cpp
SR_TEST(game_layout_binds_the_constants_with_dynamic_offsets_inside_the_guaranteed_limits) {
  uint32_t dynamic = 0;
  for (const auto& b : GameBindingLayout()) {
    if (b.set == 0) {
      SR_CHECK(b.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
      SR_CHECK_EQ(b.count, 1u);
      ++dynamic;
    } else {
      SR_CHECK(b.type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
    }
  }
  SR_CHECK_EQ(dynamic, 3u);  // o Vulkan garante maxDescriptorSetStorageBuffersDynamic >= 4
  DeviceCaps caps{};
  caps.stage.storage_buffers = 35;caps.stage.sampled_images = 96;caps.stage.samplers = 32;caps.stage.descriptor_sets = 4;
  caps.layout = caps.stage;
  SR_CHECK(CheckDeviceRequirements({}, {}, GameBindingLayout(), caps).empty());
  caps.layout.storage_buffers = 34;  // 3 dinâmicos + 32 de vertex buffers exigem 35
  SR_CHECK(!CheckDeviceRequirements({}, {}, GameBindingLayout(), caps).empty());
}
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe
```
Expected: `FAIL game_layout_binds_the_constants_with_dynamic_offsets_inside_the_guaranteed_limits` (o tipo do set 0 ainda é `STORAGE_BUFFER`).

- [ ] **Step 3: Mudar o layout**

Em `port/src/graphics/vulkan/device_requirements.cpp`:

1. Em `GameBindingLayout`, trocar as três linhas do set 0:

```cpp
  return {{0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT},
          {0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT},
          {0,2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,both},
```

por

```cpp
  // Set 0 holds the draw's VS/PS/shared constants: dynamic offsets avoid allocating and updating a set per draw.
  return {{0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT},
          {0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_FRAGMENT_BIT},
          {0,2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,both},
```

2. Na contagem (`auto add=...`), trocar `    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:c.storage+=b.count;break;` por:

```cpp
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:c.storage+=b.count;break;
```

- [ ] **Step 4: Trocar o set de constantes por draw**

Em `port/src/graphics/vulkan/descriptor_sets.h`:

1. Em `struct DescriptorDraw`, trocar

```cpp
  std::array<VkDescriptorSet,4> sets{};
  std::shared_ptr<DescriptorPage> page;
  std::shared_ptr<DescriptorCacheEntry> shared;
```

por

```cpp
  std::array<VkDescriptorSet,4> sets{};
  // Set 0 binds the draw's constants with dynamic offsets: bind with these three offsets.
  std::array<uint32_t,3> dynamic_offsets{};
  std::shared_ptr<DescriptorCacheEntry> shared;
```

2. Na parte `private:`, trocar

```cpp
  std::map<uint64_t,std::vector<std::shared_ptr<DescriptorPage>>> pages_;
  std::vector<std::shared_ptr<DescriptorPage>> free_pages_;
```

por

```cpp
  // Set 0 (the draw's constants, bound with dynamic offsets): one set per arena chunk.
  struct ConstantSetEntry {std::weak_ptr<BufferResource> owner;VkDescriptorSet set=VK_NULL_HANDLE;};
  VkDescriptorSet ConstantSet(const BufferResource& block,Error&);
  void FreeConstantSet(VkDescriptorSet);
  std::unordered_map<VkBuffer,ConstantSetEntry> constant_sets_;
  std::shared_ptr<DescriptorPage> constant_pool_;
```

3. Trocar `  uint64_t constant_serial_=0,completed_=0;` por `  uint64_t completed_=0;`.

Em `port/src/graphics/vulkan/descriptor_sets.cpp`, salvar o script abaixo em `patch_descriptors.py` (na pasta temporária da sessão) e rodar `python patch_descriptors.py` na raiz do repositório. Ele troca o destrutor, o fim de `Prepare` e `Retire`, mantendo CRLF:

```python
p = 'port/src/graphics/vulkan/descriptor_sets.cpp'
s = open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')

old = "pending_.Retire(UINT64_MAX);pages_.clear();free_pages_.clear();cache_.clear();"
assert s.count(old) == 1
s = s.replace(old, "pending_.Retire(UINT64_MAX);constant_sets_.clear();constant_pool_.reset();cache_.clear();")

old = "namespace superman_returns::graphics::vulkan {\n"
assert s.count(old) == 1
s = s.replace(old, old +
    "// The three constant blocks of a draw are bound as one contiguous 12 KiB range.\n"
    "static_assert(sizeof(guest::ConstantSnapshot)==3*4096 && offsetof(guest::ConstantSnapshot,vs)==0 &&\n"
    "              offsetof(guest::ConstantSnapshot,ps)==4096 && offsetof(guest::ConstantSnapshot,shared)==8192);\n", 1)

start = s.index("  std::array<VkDescriptorBufferInfo,3> constant_info{};")
end = s.index("} // namespace superman_returns::graphics::vulkan")
tail = r'''  // VS, PS and shared constants are contiguous in ConstantSnapshot: one upload per draw.
  auto constants=store.UploadTransient(0,std::as_bytes(std::span(&bindings.constants,1)),sizeof(guest::ConstantSnapshot),e);
  if(!constants) return {};
  if(constants->offset>UINT32_MAX) {e={"Draw descriptors",VK_ERROR_OUT_OF_DEVICE_MEMORY,"Constant block offset exceeds the dynamic offset range"};return {};}
  draw->resources.push_back(constants);
  draw->sets[0]=ConstantSet(*constants,e);if(!draw->sets[0]) return {};
  draw->dynamic_offsets.fill(uint32_t(constants->offset));
  for(uint32_t i=0;i<3;++i) draw->sets[1+i]=draw->shared->sets[i];
  pending_.Keep(serial,draw);e={};return draw;
}
VkDescriptorSet DescriptorStore::ConstantSet(const BufferResource& block,Error& e) {
  // Blocks are suballocations: every block of one arena chunk shares its handle and its set.
  const auto& chunk=block.owner;
  if(!chunk) {e={"Constant descriptors",VK_ERROR_INITIALIZATION_FAILED,"Constants were not suballocated from an arena"};return VK_NULL_HANDLE;}
  if(auto found=constant_sets_.find(block.handle);found!=constant_sets_.end()) {
    if(found->second.owner.lock()==chunk) return found->second.set;
    FreeConstantSet(found->second.set);constant_sets_.erase(found);  // the driver reused the handle of a destroyed chunk
  }
  // Sets of destroyed chunks are dead weight: free them before adding another.
  for(auto it=constant_sets_.begin();it!=constant_sets_.end();) {
    if(it->second.owner.expired()) {FreeConstantSet(it->second.set);it=constant_sets_.erase(it);}
    else ++it;
  }
  constexpr uint32_t max_sets=256;
  if(!constant_pool_) {
    auto pool=std::make_shared<DescriptorPage>();pool->context=&c_;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,3*max_sets};
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};info.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;info.maxSets=max_sets;info.poolSizeCount=1;info.pPoolSizes=&size;
    if(!Check(c_.f.vkCreateDescriptorPool(c_.device,&info,nullptr,&pool->pool),"Create constant descriptor pool",e)) return VK_NULL_HANDLE;
    constant_pool_=std::move(pool);
  }
  VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate.descriptorPool=constant_pool_->pool;allocate.descriptorSetCount=1;allocate.pSetLayouts=layouts_.data();
  VkDescriptorSet set=VK_NULL_HANDLE;
  if(!Check(c_.f.vkAllocateDescriptorSets(c_.device,&allocate,&set),"Allocate constant descriptor set",e)) return VK_NULL_HANDLE;
  constexpr VkDeviceSize block_bytes=4096;
  std::array<VkDescriptorBufferInfo,3> info{};std::array<VkWriteDescriptorSet,3> writes{};
  for(uint32_t i=0;i<3;++i) {
    info[i]={chunk->handle,i*block_bytes,block_bytes};
    auto& w=writes[i];w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=set;w.dstBinding=i;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;w.pBufferInfo=&info[i];
  }
  c_.f.vkUpdateDescriptorSets(c_.device,uint32_t(writes.size()),writes.data(),0,nullptr);
  constant_sets_.emplace(block.handle,ConstantSetEntry{chunk,set});
  return set;
}
void DescriptorStore::FreeConstantSet(VkDescriptorSet set) {
  if(set && constant_pool_) c_.f.vkFreeDescriptorSets(c_.device,constant_pool_->pool,1,&set);
}
void DescriptorStore::Retire(uint64_t serial) {
  completed_=std::max(completed_,serial);pending_.Retire(completed_);
}
'''
s = s[:start] + tail + s[end:]
if '#include <cstddef>' not in s:
    s = s.replace('#include <algorithm>\n', '#include <algorithm>\n#include <cstddef>\n', 1)
open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))
print('ok')
```

Conferir com `git diff --stat port/src/graphics/vulkan/descriptor_sets.cpp` (poucas dezenas de linhas, não o arquivo inteiro).

Passar os offsets nos três `vkCmdBindDescriptorSets`, trocando o final `,0,nullptr);` do bind de descritores:

- `game_renderer.cpp`: `...pipelines_.Layout(),0,4,descriptors->sets.data(),0,nullptr);` vira `...pipelines_.Layout(),0,4,descriptors->sets.data(),uint32_t(descriptors->dynamic_offsets.size()),descriptors->dynamic_offsets.data());`
- `composition.cpp`: `...pipelines_.Layout(),0,4,draw.descriptors->sets.data(),0,nullptr);` vira `...pipelines_.Layout(),0,4,draw.descriptors->sets.data(),uint32_t(draw.descriptors->dynamic_offsets.size()),draw.descriptors->dynamic_offsets.data());`
- `immediate.cpp`: `...pipelines_.Layout(),0,4,descriptor->sets.data(),0,nullptr);` vira `...pipelines_.Layout(),0,4,descriptor->sets.data(),uint32_t(descriptor->dynamic_offsets.size()),descriptor->dynamic_offsets.data());`

Em `tests/vulkan/shader_contract_integration.cpp`:
- depois de `  std::array<VkDescriptorSet, 4> sets{};` acrescentar `  std::array<uint32_t, 3> dynamic_offsets{};uint32_t dynamic_count = 0;`
- `                              pipeline_layout, 0, 4, sets.data(), 0, nullptr);` vira `                              pipeline_layout, 0, 4, sets.data(), dynamic_count, dynamic_offsets.data());`
- depois de `          fixture.sets=sets->sets;` acrescentar `          fixture.dynamic_offsets=sets->dynamic_offsets;fixture.dynamic_count=uint32_t(sets->dynamic_offsets.size());`

- [ ] **Step 5: Rodar e ver passar**

```powershell
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe
.\build\tests-vulkan\sr_vulkan_contract_test.exe --production-bindings
```
Expected: `PASS game_layout_binds_the_constants_with_dynamic_offsets_inside_the_guaranteed_limits`, `0 failed checks`, e o contrato de produção sai com código 0, sem erro de validação do Vulkan.

- [ ] **Step 6: Regressão de GPU**

Rodar o mesmo laço do Task 2 Step 5 (os 15 fixtures de `sr_vulkan_resources_test.exe`).
Expected: nenhuma linha `FALHOU` e nenhum erro de validação impresso (`validation_errors` = 0 em cada fixture).

- [ ] **Step 7: Compilar o jogo e medir**

```powershell
$env:SR_BUILD_DIR = "$PWD\port\out\build\win-amd64-dist"; $env:SR_EMBED_SHADERS = 'OFF'; $env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c "$PWD\build.cmd"
.\tools\bench_api.ps1 -Api vulkan -Name par_c -Profile
.\tools\vulkan_profile_summary.ps1 -Log logs\bench_par_c.log
```
Expected: `descriptors` cai de 13 a 18 ms para algo perto de 5 ms; `record` cai de ~38 para ~28 ou menos; o FPS sobe em relação a `par_b`. Registrar (`par_c`) e conferir `logs\bench_par_c_idle.png` contra `par_base`. Se os dois FPS passarem de 29, pular para a Task 5.

- [ ] **Step 8: Commit**

```bash
graphify update .
git add port/src/graphics/vulkan/device_requirements.cpp port/src/graphics/vulkan/descriptor_sets.h port/src/graphics/vulkan/descriptor_sets.cpp port/src/graphics/vulkan/game_renderer.cpp port/src/graphics/vulkan/composition.cpp port/src/graphics/vulkan/immediate.cpp tests/vulkan/test_device_requirements.cpp tests/vulkan/shader_contract_integration.cpp
git commit -m "perf(vulkan): bind per-draw constants with dynamic offsets instead of a set per draw"
```

---

### Task 4: Memo do draw anterior (só se o bench ainda estiver abaixo de 29 FPS)

Executar apenas se, depois da Task 3, uma das duas medições do Vulkan (`par_c`) ainda estiver abaixo de 29 FPS e o perfil mostrar `descriptors` acima de ~4 ms. Caso contrário, pular para a Task 5.

**Files:**
- Modify: `port/src/graphics/vulkan/descriptors.h`, `descriptors.cpp` (`BindingSignature`, `MakeBindingSignature`)
- Modify: `port/src/graphics/vulkan/resources.h`, `resources.cpp` (`ResourceStore::Generation`)
- Modify: `port/src/graphics/vulkan/descriptor_sets.h`, `descriptor_sets.cpp` (memo em `Prepare`)
- Test: `tests/vulkan/test_descriptors.cpp`

**Interfaces:**
- Produces: `BindingSignature MakeBindingSignature(const DrawBindings&, const std::array<std::array<uint32_t,6>,32>& fetch)` em `descriptors.h` (com `operator==` padrão) e `uint64_t ResourceStore::Generation() const`, que sobe sempre que um ID passa a apontar para outro recurso ou é esquecido.

- [ ] **Step 1: Escrever os testes que falham**

Acrescentar no fim de `tests/vulkan/test_descriptors.cpp`:

```cpp
SR_TEST(binding_signature_ignores_vertex_fetch_words_and_tracks_sampler_state) {
  DrawBindings bindings{};
  std::array<std::array<uint32_t,6>,32> fetch{};
  fetch[3][0] = 2 | (3 << 10);fetch[3][3] = 1u << 19;fetch[3][5] = 1u << 9;  // slot 3 é uma textura
  fetch[5] = {0x1234, 0x5678, 1, 2, 3, 4};                                     // slot 5 é um vertex fetch (tipo != 2)
  const auto base = MakeBindingSignature(bindings, fetch);
  fetch[5] = {0x9999, 0x8888, 7, 6, 5, 4};  // outro vertex fetch: não muda o conjunto de descritores
  SR_CHECK(MakeBindingSignature(bindings, fetch) == base);
  fetch[3][3] |= 1u << 21;  // outro filtro na textura: muda o sampler
  SR_CHECK(!(MakeBindingSignature(bindings, fetch) == base));
}
SR_TEST(binding_signature_tracks_resource_ids_and_texture_indices) {
  DrawBindings bindings{};
  std::array<std::array<uint32_t,6>,32> fetch{};
  const auto base = MakeBindingSignature(bindings, fetch);
  bindings.textures[1][4] = 77;
  SR_CHECK(!(MakeBindingSignature(bindings, fetch) == base));
  bindings = DrawBindings{};bindings.vertex_buffers[2] = 9;
  SR_CHECK(!(MakeBindingSignature(bindings, fetch) == base));
  bindings = DrawBindings{};bindings.texture_indices[0] = 3;
  SR_CHECK(!(MakeBindingSignature(bindings, fetch) == base));
  bindings = DrawBindings{};
  SR_CHECK(MakeBindingSignature(bindings, fetch) == base);
}
```

- [ ] **Step 2: Rodar e ver falhar**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-vulkan
```
Expected: FALHA de compilação: `use of undeclared identifier 'MakeBindingSignature'`.

- [ ] **Step 3: Implementar a assinatura**

Em `port/src/graphics/vulkan/descriptors.h`, depois da definição de `DrawBindings` (antes de `guest::ResourceId TextureResourceId(...)`), acrescentar:

```cpp
// The part of a draw's bindings that decides its shared descriptor sets: resource IDs, texture indices
// and sampler/filter state. Equal signatures and an unchanged ResourceStore generation give the same sets.
struct BindingSignature {
  std::array<std::array<guest::ResourceId,32>,3> textures{};
  std::array<guest::ResourceId,32> vertex_buffers{};
  std::array<uint32_t,32> texture_indices{};
  std::array<uint64_t,32> sampler_state{};
  bool operator==(const BindingSignature&) const=default;
};
BindingSignature MakeBindingSignature(const DrawBindings&,const std::array<std::array<uint32_t,6>,32>& fetch);
```

Em `port/src/graphics/vulkan/descriptors.cpp`, antes de `} // namespace superman_returns::graphics::vulkan` (a última linha do arquivo), acrescentar:

```cpp
BindingSignature MakeBindingSignature(const DrawBindings& bindings,const std::array<std::array<uint32_t,6>,32>& fetch) {
  BindingSignature s;
  s.textures=bindings.textures;s.vertex_buffers=bindings.vertex_buffers;s.texture_indices=bindings.texture_indices;
  for(uint32_t slot=0;slot<32;++slot) {
    // Only texture slots (fetch type 2) carry sampler/filter state. The other slots hold vertex fetch
    // constants whose addresses change between draws and do not affect the shared sets.
    if((fetch[slot][0]&3)==2)
      s.sampler_state[slot]=(uint64_t(fetch[slot][0]&0x7fc00u)<<40)|(uint64_t(fetch[slot][3]&0xff80000u)<<8)|uint64_t((fetch[slot][5]>>9)&3)|(uint64_t(1)<<63);
  }
  return s;
}
```

- [ ] **Step 4: Rodar e ver passar**

```powershell
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe
```
Expected: os dois testes novos `PASS`, `0 failed checks`.

- [ ] **Step 5: Gerações do `ResourceStore`**

Em `port/src/graphics/vulkan/resources.h`:
- trocar `  void ForgetBuffer(guest::ResourceId id) {buffers_.erase(id);}` por `  void ForgetBuffer(guest::ResourceId id) {buffers_.erase(id);++generation_;}`;
- trocar `  void ForgetTexture(guest::ResourceId id) {textures_.erase(id);}` por `  void ForgetTexture(guest::ResourceId id) {textures_.erase(id);++generation_;}`;
- logo depois da linha de `FindTexture` (a que começa com `  const std::shared_ptr<TextureResource>* FindTexture`), acrescentar:

```cpp
  // Bumped whenever an ID is bound to a different resource or forgotten: while it is unchanged,
  // an ID resolves to the same resource.
  uint64_t Generation() const {return generation_;}
```
- na parte `private:`, junto de `Arena`/`buffers_`, acrescentar `  uint64_t generation_=0;`.

Em `port/src/graphics/vulkan/resources.cpp`, acrescentar `++generation_;` logo depois de cada atribuição abaixo (sete pontos; conferir cada um com `grep -n` antes):

1. `buffer->version=serial_;buffers_[id]=buffer;}` vira `buffer->version=serial_;buffers_[id]=buffer;++generation_;}`
2. `submissions_.Keep(serial_,gpu);buffers_[id]=std::move(gpu);e={};return true;}` (a linha com `if(upload_) {upload_barrier_=true;`) vira `...buffers_[id]=std::move(gpu);++generation_;e={};return true;}`
3. `submissions_.Keep(serial_,upload);submissions_.Keep(serial_,gpu);buffers_[id]=std::move(gpu);e={};return true;}` vira `...buffers_[id]=std::move(gpu);++generation_;e={};return true;}`
4. `submissions_.Keep(serial_,upload);submissions_.Keep(serial_,gpu);textures_[id]=std::move(gpu);e={};return true;}` vira `...textures_[id]=std::move(gpu);++generation_;e={};return true;}`
5. `submissions_.Keep(serial_,t);textures_[id]=t;e={};return t;` vira `...textures_[id]=t;++generation_;e={};return t;`
6. `submissions_.Keep(serial_,t);textures_[id]=t;e={};return true;` vira `...textures_[id]=t;++generation_;e={};return true;`
7. `buffer->version=version;submissions_.Keep(serial_,buffer);buffers_[id]=std::move(buffer);e={};return true;}` vira `...buffers_[id]=std::move(buffer);++generation_;e={};return true;}`

Conferir no fim: `grep -n "buffers_\[\|textures_\[" port/src/graphics/vulkan/resources.cpp` — cada ocorrência de atribuição tem que ter `++generation_` na mesma linha.

- [ ] **Step 6: Memo no `DescriptorStore::Prepare`**

Em `port/src/graphics/vulkan/descriptor_sets.h`, na parte `private:`, depois de `  std::shared_ptr<DescriptorPage> constant_pool_;`, acrescentar:

```cpp
  // Consecutive draws of a submission usually bind the same resources: reuse the shared sets without
  // rebuilding the cache key. Valid only while no ID changed what it resolves to (ResourceStore generation).
  struct Memo {
    bool valid=false;uint64_t serial=0,generation=0;BindingSignature signature;
    std::shared_ptr<DescriptorCacheEntry> entry;std::vector<std::shared_ptr<void>> bound;
  };
  Memo memo_;
```

Em `port/src/graphics/vulkan/descriptor_sets.cpp`, em `DescriptorStore::Prepare`, trocar

```cpp
  draw->shared=Shared(bindings,fetch,store,serial,draw->resources,e);if(!draw->shared) return {};
```

por

```cpp
  auto signature=MakeBindingSignature(bindings,fetch);
  if(memo_.valid && memo_.serial==serial && memo_.generation==store.Generation() && memo_.signature==signature) {
    ++cache_stats_.hits;draw->shared=memo_.entry;draw->resources=memo_.bound;
  } else {
    draw->shared=Shared(bindings,fetch,store,serial,draw->resources,e);
    if(!draw->shared) {memo_.valid=false;return {};}
    memo_=Memo{true,serial,store.Generation(),std::move(signature),draw->shared,draw->resources};
  }
```

- [ ] **Step 7: Regressão e medição**

```powershell
cmake --build build/tests-vulkan
.\build\tests-vulkan\sr_vulkan_tests.exe
.\build\tests-vulkan\sr_vulkan_contract_test.exe --production-bindings
```
Rodar o laço de 15 fixtures do Task 2 Step 5. Depois:

```powershell
$env:SR_BUILD_DIR = "$PWD\port\out\build\win-amd64-dist"; $env:SR_EMBED_SHADERS = 'OFF'; $env:SR_BUILD_LAUNCHER = 'OFF'
cmd /c "$PWD\build.cmd"
.\tools\bench_api.ps1 -Api vulkan -Name par_memo -Profile
.\tools\vulkan_profile_summary.ps1 -Log logs\bench_par_memo.log
```
Expected: testes e fixtures sem falha; `descriptors` cai mais ~1 a 2 ms; FPS não regride em relação a `par_c`; imagem igual (`logs\bench_par_memo_idle.png`). Se o FPS não melhorar, reverter este commit em vez de mantê-lo: o ganho esperado é pequeno.

- [ ] **Step 8: Commit**

```bash
graphify update .
git add port/src/graphics/vulkan/descriptors.h port/src/graphics/vulkan/descriptors.cpp port/src/graphics/vulkan/resources.h port/src/graphics/vulkan/resources.cpp port/src/graphics/vulkan/descriptor_sets.h port/src/graphics/vulkan/descriptor_sets.cpp tests/vulkan/test_descriptors.cpp
git commit -m "perf(vulkan): reuse the shared descriptor sets of the previous draw when its bindings are unchanged"
```

---

### Task 5: Resultados e documentação

**Files:**
- Modify: `docs/vulkan-m3.md` (nova seção em inglês, como o resto do arquivo)
- Modify: `README.md` (linha do Vulkan na tabela de desempenho)
- Modify: `docs/superpowers/specs/2026-10-05-vulkan-fps-parity-design.md` (resultado)

**Interfaces:**
- Consumes: as linhas `par_base`, `par_a`, `par_b`, `par_c` (e `par_memo`, se existir) de `logs\bench_results.csv`.

- [ ] **Step 1: Medição final nas duas APIs**

```powershell
.\tools\bench_api.ps1 -Api vulkan -Name par_final -Profile
.\tools\vulkan_profile_summary.ps1 -Log logs\bench_par_final.log
.\tools\bench_api.ps1 -Api d3d12 -Name par_final_d3d12
```
Expected: Vulkan com média ≥ 29 FPS parado e andando; D3D12 em ~30. Se o Vulkan ainda estiver abaixo, **parar e relatar os números ao autor** antes de qualquer outra mudança (ver Global Constraints).

- [ ] **Step 2: Rodar a suíte inteira**

```powershell
. .\tools\dev_env.ps1
cmake --build build/tests-native; .\build\tests-native\sr_native_tests.exe
cmake --build build/tests-vulkan; .\build\tests-vulkan\sr_vulkan_tests.exe
.\build\tests-vulkan\sr_vulkan_contract_test.exe --production-bindings
```
e o laço de 15 fixtures do Task 2 Step 5. Expected: tudo passa.

- [ ] **Step 3: Registrar os resultados**

Em `docs/vulkan-m3.md`, depois da seção "Optimizations and fixes from 2026-10-05 (Intel UHD)" (antes de `## Recorded checks`), acrescentar a seção abaixo, trocando cada número pelo `avg_fps` das linhas correspondentes de `logs\bench_results.csv` (parado e andando) e pelos `record`, `fence`, `queue` e `textures_ms` do resumo de cada etapa:

```markdown
### FPS parity with D3D12 (2026-10-05, Intel UHD, windowed 1280x720, 30 FPS limit)

Three CPU costs were removed, each measured with `tools/bench_api.ps1` and `SR_VULKAN_PROFILE=1`:

| Step | Idle FPS | Walking FPS | Guest-thread textures_ms | record / fence / queue (ms) |
| --- | --- | --- | --- | --- |
| baseline (`par_base`) | <idle> | <walking> | <ms> | <record> / <fence> / <queue> |
| a: texture hash in place (`par_a`) | ... | ... | ... | ... |
| b: waits outside the queue mutex (`par_b`) | ... | ... | ... | ... |
| c: dynamic offsets for constants (`par_c`) | ... | ... | ... | ... |
| final (`par_final`) | ... | ... | ... | ... |
| D3D12 (`par_final_d3d12`) | ... | ... | n/a | n/a |

- Texture revalidation hashes guest memory in place (`HashGuestRange`, SEH-guarded); only a changed texture is copied.
- `FrameLoop::DrawGame` takes the queue mutex only around retire, recording, submit and present, so a vblank wait no longer blocks the game's submissions.
- Draw constants (VS, PS, shared: one contiguous 12 KiB block) are bound through `STORAGE_BUFFER_DYNAMIC` descriptors, one set per arena chunk, instead of a descriptor set per draw.
```

(Cada `...` e cada `<...>` da tabela é uma célula a preencher com o número medido da linha respectiva; não deixar nenhum no commit.)

No `README.md`, na tabela de desempenho, trocar a linha

```
| `native`, Vulkan | ~7,7 FPS | ~8,0 FPS | experimental; GPU a ~35%, o gargalo é a CPU gravando cada quadro |
```

por uma linha com os valores de `par_final` (parado e andando) e a GPU medida, e o texto `experimental` mantido se o autor ainda considerar o Vulkan experimental. Ajustar a frase "em GPU integrada, o D3D12 nativo é bem mais rápido" (seção "Estado atual" e seção Vulkan) para refletir a paridade medida.

No fim do spec `docs/superpowers/specs/2026-10-05-vulkan-fps-parity-design.md`, acrescentar a seção `## Resultado` com a mesma tabela e uma linha dizendo qual meta foi atingida e em qual passo.

- [ ] **Step 4: Commit**

```bash
git add docs/vulkan-m3.md README.md docs/superpowers/specs/2026-10-05-vulkan-fps-parity-design.md
git commit -m "docs: record the Vulkan FPS parity measurements"
```
Perguntar ao autor antes de qualquer `git push`.
