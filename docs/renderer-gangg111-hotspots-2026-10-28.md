# Renderer: as quatro pistas do gangg111 — 28/10/2026

Relato externo (fork privado, não medido aqui): worker ~20 ms/quadro por (1) leitura do upload buffer da GPU, (2) re-hash de texturas todo quadro, (3) VirtualQuery nas cargas de constantes e (4) render target recriado duas vezes por quadro; depois 60 FPS travados, frame pacing e pré-compilação de shaders.

## Situação neste checkout

| Pista | Estado |
| --- | --- |
| Leitura do upload buffer | Já resolvido: swaps de vértices/índices e constantes compartilhadas são montados em scratch CPU e só copiados para o heap de upload (write-combined). Nenhum caminho normal lê `UploadAlloc::cpu`. |
| Re-hash de texturas | Já resolvido: write watch + `TextureHashBackoff` (2..16 quadros) + tracking de escrita do runtime. Relatório de 23/10: 35,58 → 0,36 ms. |
| VirtualQuery nas constantes | Já trocado (24/10) por `ReadProcessMemory` limitado (`sr_native_pm4_bounded_reads`, padrão On), ainda uma syscall por load: ~1,65 µs × ~600 loads ≈ 1 ms/quadro. **Mudança de hoje:** `CopyGuestBytes` faz a cópia dentro de um frame SEH (`__try`/`__except`), sem syscall. Vale para todo `CheckedGuestReads` (loads PM4 do D3D12, captura do front-end Vulkan, auditorias). |
| Render target recriado | Não reproduzido: `GetSurface` mantém cache por EDRAM/formato/tamanho/MSAA, resolves por identidade completa; zero criações de superfície em gameplay (relatório de 24/10). |
| Frame pacing / pré-compilação | Já existem: `sr_native_fps_limit` + scheduler do jogo (`guest_render_scheduler.cpp`) e cache de PSOs (`pipeline_cache.*`, EXP-044). |

## Detalhes da cópia SEH

- A cópia é chamada por ponteiro volátil para `memcpy`, para continuar sendo uma chamada dentro do `__try`: sem `/EHa`, o clang só desenrola falhas levantadas por chamadas. Mesmo padrão de `HashGuestRange` (`guest_hash.h`), que já roda no hash de texturas.
- Só `EXCEPTION_ACCESS_VIOLATION` e `EXCEPTION_IN_PAGE_ERROR` são tratados; leitura parcial ou página sem acesso devolve vazio, como antes.
- Teste novo: `copy_guest_bytes_survives_faults_without_a_syscall` (`tests/native/test_checked_guest_memory.cpp`).

## A validar no jogo

Ganho esperado pequeno (ordem de 1 ms/quadro no worker D3D12, mais a parte de cópias do `fe_pm4` no Vulkan). Comparar `native CPU PM4 live reads` (`SR_NATIVE_PM4_READ_PROFILE=1` + CPU profiling): `validation_ms` deve cair de ~1,0–1,3 ms. O resto dos 60 FPS continua em `UploadConstants` (~12 ms, slots ~6–7 ms) e streams (~6 ms), conforme `docs/d3d12-pm4-read-profile-2026-10-24.md`.
