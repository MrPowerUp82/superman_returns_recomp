# Checkpoint 13 — unlocks de textura e produtor de três planos

Data: 2026-10-08. Continuação do checkpoint 12.

## Entrega

O helper comum `0x820F3C18` foi confirmado estaticamente pelo destino dos
dois unlocks de buffer já confirmados. Decrementa contagem de lock e chama
`0x82106F98`, que contém loops dcbf/sync. Foi adicionado ao perfil e recebe
hook pass-through: o original sempre executa.

`SR_NATIVE_RESOURCE_UNLOCK_AUDIT=1`, desligado por padrão, observa 52 bytes
do cabeçalho após o original, com leitura verificada. Classifica candidatos
por tipo de fetch, argumentos de páginas e descrição SDK válida. Não copia
payloads nem altera hashes, períodos, backoff ou uploads.

Com `SR_NATIVE_TEXTURE_AUDIT=1` também ligado, correlaciona descritores e
partições físicas de unlocks anteriores com verificações reais de textura.
Limites: 64 chamadores, 1024 descritores, 1024 conjuntos de faixas, 256
detalhes de descritores distintos e 32 detalhes de primeiras correspondências.
Agregados cumulativos a cada 120 swaps frontend.

## Endereços encontrados

| Endereço | Evidência |
| --- | --- |
| `820FFCC8` | Unlock com páginas base/mip em objeto+32/+48; tail-call ao helper comum. |
| `821002F8` | Wrapper de surface: textura em objeto+24, depois mesma extração. |
| `820FFCB0` | Candidato lock de textura nível zero; tail-call `820FF530`. |
| `821002D8` | Candidato lock de surface; tail-call `820FF5C0`. |
| `8235CA48` | Bloqueia três saídas, chama produtor, desbloqueia e faz bind das três. |
| `824707B0` | Thunk indireto pela vtable do objeto, offset 72. Destino real/codec pendentes. |

Foi necessário normalizar aliases CPU do cabeçalho antes da comparação:
low 29 bits, com +4 KiB para E/F, conforme a aritmética do helper. Exemplo:
`EBDC9000 → 0BDCA000`. A correção é da auditoria; a tradução normal do
renderer não foi alterada. Teste rejeita overflow no fim da arena.

## Resultado

Últimos resumos cumulativos completos:

| API | Frame frontend | Unlocks | Candidatos | Faixas distintas armazenadas | Conjuntos correspondentes | Verificações correspondentes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 3120 | 19365 | 516 | 148 | 50 | 46104 |
| Vulkan | 2160 | 14931 | 2483 | 148 | 49 | 6296 |

18 chamadores em cada API; nenhuma leitura inválida, descrição inválida ou
overflow dos limites. Descritores completos tiveram zero correspondências,
mesmo normalizados; a evidência é igualdade das faixas físicas. Isso não
garante identidade do objeto, lifetime nem escrita desde o último hash.

PCs `8235CBD4/E0/EC`, do produtor de três planos, foram observados 84 vezes
cada no D3D12 e 740 no Vulkan. Descritores: 1280×720 e dois 640×360,
compatíveis com vídeo planar (inferência; codec não confirmado). Objetos de
textura vêm de `829761E4/E8/EC`; objeto do produtor de `829761D4`.
O agregado atual não demonstra correspondência individual dos três planos
nem atribui cada mudança de hash a um unlock específico.

Auditoria de conteúdo: D3D12 2056864 checks / 238 mudanças; Vulkan 79677 /
1960; zero mudanças sem notificação física observada. Janelas diferem das
dos unlocks. Isso não prova cobertura por unlock nem permite retirar hashes.
Nenhum ganho de FPS alegado; diagnósticos adicionam leitura, alocação e locks.

## Validação e artefatos

- Release passou; 120 casos nativos, orçamento de alocações, 3 testes Python
  do perfil, 88 Vulkan e 13 de textura com SDK passaram.
- Gameplay parado/em movimento nas duas APIs; sem build/testes pesados
  durante amostragem de FPS.
- Gates contra C9 passaram: D3D12 33,40 dB / 0,01844; Vulkan 37,65 / 0,00510.
  Cena/HUD inspecionados. O gate antigo C7 Vulkan continua sem resolução.
- Estática reproduzível: `tools/analysis/texture_unlock_candidates.py --out
  logs/native_c10_unlock_candidates.json`; registra instruções/chamadores e
  verifica os shapes dos wrappers.
- Runtime: `logs/bench_native_c10_ranges_{d3d12,vulkan}.*`.
- Resumo: `tools/bench/summarize_resource_unlock_audit.ps1`, saída
  `logs/native_c10_ranges_summary.json`.
- Gates: `build/compare_native_c10.ps1`, `logs/native_c10_image_gates.json`
  (Windows PowerShell 5.1).
- Build: `build/native_c10_ranges_build.log`; SDK:
  `logs/native_c10_guest_texture_tests.log`.
- Executável preservado: `artifacts/native-renderer-performance/native_c10_unlock_audit.exe`,
  SHA-256 `53a6711b57552767a427f7a57b527ad709d643377d4430d5148ea3b99030a9e6`.
  Usar junto às dependências de runtime da pasta Release.

Implementação: `resource_unlock_audit.h`, `native_hooks.cpp`, hooks de
correlação em `native_renderer.cpp`, novo papel no `game_profile.h`.
Detalhes em `docs/native-renderer-performance.md`, Tenth pass.

## Próximo passo

Resolver em execução o destino vtable+72 do produtor. Correlacionar sequências
de unlock e mudanças de conteúdo por recurso durante criação, reprodução,
skip, destruição e reutilização. Manter validação de conteúdo até comprovar
cobertura, incluindo aliases e resolves. A referência UnleashedRecomp orientou
a investigação; nenhum endereço ou código do outro jogo foi transferido.

Alterações anteriores preservadas. Sem commit/publicação.
