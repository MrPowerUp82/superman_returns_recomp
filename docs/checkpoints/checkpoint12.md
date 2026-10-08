# Checkpoint 12 — custo da leitura guest e caminho de escrita

Data: 2026-10-08. Continuação do checkpoint 11.

## Entrega

Diagnóstico `SR_NATIVE_TEXTURE_SOURCE_PROBE=1`, desligado por padrão.
Usa `SR_NATIVE_TEXTURE_REPLAY_START_FRAME` para o início (padrão 1800),
independentemente do switch de replay C8.

Amostra uma em cada 32 verificações reais, mantendo repetições de texturas.
Mede hash original, alocação/cópia verificada, hash guest após a cópia e hash
da cópia. Alterna a ordem guest/cópia e faz um último hash guest. Descarta
do agregado de tempos amostras cujos hashes divergem do original ou falham.
Isso detecta mudanças observadas, sem tornar leituras guest atômicas.

Limites por thread: 1024 amostras, 256 MiB cumulativos copiados, 4 MiB por
textura, 120 frames após o início configurado. Libera cada cópia ao terminar
a amostra; não grava payloads. Hashes, periodicidade e recursos GPU normais
permanecem no caminho existente.

## Resultado

| API | Janela | Amostras estáveis | Bases distintas | KiB |
| --- | --- | ---: | ---: | ---: |
| D3D12 | 1800–1843 | 1024 | 470 | 123844 |
| Vulkan | 1800–1920 | 227 | 200 | 30064 |

Nenhuma mudança/falha de leitura observada. Médias em µs por verificação:

| API | Hash original | Guest após cópia | Hash da cópia | Alocação/cópia verificada | Cópia + hash / original |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 8,459 | 3,413 | 3,149 | 33,377 | 4,32× |
| Vulkan | 11,833 | 5,872 | 5,119 | 54,379 | 5,03× |

A cópia verificada experimentada custa mais que o hash original. Inclui
alocação, zeragem e ReadProcessMemory, exclui destruição/upload; não é uma
medição de scratch buffer reutilizável. Leituras posteriores são mais rápidas,
mas o teste não isola cache de escalonamento. Não há ganho de FPS comprovado.
O D3D12 atingiu o limite antes da detecção de gameplay visível pelo runner;
Vulkan sobrepôs gameplay. Janelas curtas, stride determinístico e corpora
diferentes limitam generalização. Flags da primeira faixa: OR `0x4` em ambos
os tipos de memória, sem cobertura de todas as páginas/aliases.

## Referência e próximo passo

O [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/gpu/video.cpp#L2005)
intercepta lock/unlock de textura e enfileira upload no desbloqueio. É uma
pista concreta para identificar produtores aqui. O perfil local confirma
unlocks de vertex/index buffers, sem equivalente identificado para textura.
Não transferir endereços/layouts do outro jogo. Nenhum código externo copiado.

Localizar e auditar escritores de textura, incluindo aliases, vídeo,
reutilização e resolves, antes de confiar em versões ou retirar hashes.
Repetir a comparação visual C7 Vulkan com estado/pose correspondente.

## Validação e artefatos

- Release passou; 116 casos nativos, orçamento de alocações, 88 Vulkan e
  13 de textura com SDK passaram. Novos testes cobrem ordem, leitura final,
  divergência em cada observação e fontes ilegíveis.
- Gameplay parado/em movimento completou nas duas APIs; sem build/testes
  pesados durante medição.
- Gates contra C8 passaram: D3D12 30,43 dB / 0,06189; Vulkan 36,45 / 0,00865.
  Imagens inspecionadas, cenário/HUD presentes.
- Contra C7: D3D12 passou (21,46 / 0,04699); Vulkan ainda falhou na capa
  (cromaticidade 0,065 > 0,05; global 21,53 / 0,02220). Limiares mantidos.
- Logs/PNGs: `logs/bench_native_c9_source_{d3d12,vulkan}.*`.
- Resumo reproduzível: `tools/bench/summarize_texture_source_probe.ps1`,
  saída `logs/native_c9_source_summary.json`.
- Gates: `build/compare_native_c9.ps1`, `logs/native_c9_image_gates.json`
  (Windows PowerShell 5.1; falha intencionalmente no gate C7 Vulkan).
- SDK: `logs/native_c9_guest_texture_tests.log`.
- Build: `build/native_c9_build_final.log`.
- Executável preservado: `artifacts/native-renderer-performance/native_c9_source_probe.exe`.
  SHA-256 `2cbf1a740c5d92773afcf73152c57d192eeb21e1cf4bccb8f9aadb0c6b9cd8c7`.
  Precisa das dependências de runtime da pasta Release.

Implementação em `texture_source_probe.h` e `native_renderer.cpp`, testes em
`tests/native/test_texture_source_probe.cpp`. Detalhes em
`docs/native-renderer-performance.md`, Ninth pass.

Alterações anteriores preservadas. Sem commit/publicação.
