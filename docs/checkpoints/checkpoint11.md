# Checkpoint 11 — replay determinístico de hashes de textura

Data: 2026-10-08. Continuação do checkpoint 10.

## Entrega

Diagnóstico opcional `SR_NATIVE_TEXTURE_REPLAY=1`, desligado por padrão.
`SR_NATIVE_TEXTURE_REPLAY_START_FRAME` escolhe o início (padrão 1800).
D3D12 captura fontes com leituras verificadas; Vulkan retém snapshots da
captura existente. Nenhum payload de textura é gravado em disco.

Compara faixas por mip e união sobre os mesmos bytes imutáveis. Resolve
spans e decodifica fora da medição, alterna AB/BA em nove ensaios, com quatro
passagens por política, e verifica digests separados por política. Executa
também controle união/união. Limites por thread: 256 texturas / 64 MiB de
fonte retida, 4 MiB por textura, 512 descritores examinados ou 120 frames
desde a primeira amostra aceita. Libera snapshots após o replay.

## Resultado

Ambas as APIs coletaram 256 casos no frame 1800, AVX2, digests estáveis.

| Corpus | KiB por mip → união | Redução de bytes | Mediana ms por passagem | Redução de tempo observada | Variação do controle |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 52424 → 42240 | 19,43% | 2,896 → 2,795 | 3,49% | 3,91% |
| Vulkan | 69316 → 59100 | 14,74% | 4,016 → 3,728 | 7,18% | 3,21% |

Corpora diferentes entre APIs. A redução de bytes está demonstrada; ganho
de FPS não está. No D3D12, a diferença de tempo é menor que a variação do
controle. No Vulkan há um sinal favorável em uma única coleta. O controle
não constitui intervalo de confiança. Heap, cache aquecido e peso igual
por descritor limitam a extrapolação para leituras reais por frame.

## Validação e artefatos

- Build Release passou: 113 casos nativos, orçamento de alocações, 88 Vulkan
  e 13 de textura com SDK passaram. Sem compilação durante a medição.
- As duas APIs completaram gameplay parado/em movimento com o diagnóstico.
- Gate D3D12 passou: 21,17 dB / histograma 0,05992.
- Gate Vulkan falhou na cromaticidade da capa: 0,070 > 0,05; global
  21,52 dB / 0,02130. Inspeção mostra poses diferentes, cenário/HUD presentes.
  Limiares mantidos; equivalência visual não foi certificada por esse gate.
- Logs e PNGs: `logs/bench_native_c8_replay_{d3d12,vulkan}.*`.
- Resumos: `logs/native_c8_texture_replay_summary.json`,
  `logs/native_c8_image_gates.json`; testes SDK:
  `logs/native_c8_guest_texture_tests.log`.
- Scripts locais: `build/summarize_native_c8_replay.ps1`,
  `build/compare_native_c8.ps1` (Windows PowerShell 5.1).
- Build: `build/native_c8_build_final.log`.
- Executável preservado: `artifacts/native-renderer-performance/native_c8_replay.exe`,
  SHA-256 `ccd84cf529a4466e9737e755567599a3696aa4de3d3ac360e70ee0d095a42f43`.
  Usar junto às dependências de runtime da pasta Release.

Implementação: `port/src/native_renderer/texture_hash_replay.h`, hooks em
`native_renderer.cpp`, testes em `tests/native/test_texture_hash_replay.cpp`.
Detalhes em `docs/native-renderer-performance.md`, Eighth pass.

## Próximo passo

Medir leituras da memória guest com frequências iguais às verificações reais
para separar hashing, acesso à fonte e captura. Repetir comparação visual
com pose correspondente. Identificar produtores e comprovar cobertura de
aliases, vídeo, reutilização e resolves antes de retirar hashes ou confiar
em versões autoritativas. Replay atual não mede captura nem o frame completo.

Alterações anteriores preservadas. Sem commit ou publicação.
