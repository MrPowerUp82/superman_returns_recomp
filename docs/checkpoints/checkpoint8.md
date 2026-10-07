# Checkpoint 8 — referências externas e primeira implementação

Data: 2026-10-07. Continuação do checkpoint 7, incluindo pesquisa solicitada
pelo usuário e autorização para começar as adaptações.

## Estado entregue

- C4 evita rearmar watches de textura D3D12 que continuam limpos; preserva
  hashes para detectar alterações por aliases. Não demonstrou ganho de FPS.
- Pesquisa primária de Unleashed, NFSMW, Conan e Skate 3 registrada em
  `docs/native-renderer-reference-research.md`, com revisões e links de código.
- C5 adiciona `SR_NATIVE_TEXTURE_AUDIT=1` em D3D12/captura Vulkan. Mantém os
  hashes e conta mudanças com/sem notificação, considerando notificações
  durante o hash. Detalhes limitados a 64 por thread; agregados a cada 120 frames.
- Cache exato de constantes compartilhadas D3D12 com chave de 4096 bytes,
  commit após upload, reset no reaproveitamento do ring e bindings por draw.
  Fica **desligado por padrão**, ativável com `SR_NATIVE_SHARED_CONSTANTS_CACHE=1`.
  O experimento evitou cerca de 5095 KiB/frame, mas não melhorou o desempenho.
  O padrão preserva o upload direto; Vulkan não utiliza esse cache.
- Release compilado; 104 casos nativos, orçamento de alocações e 88 casos
  Vulkan passaram. Gates de imagem pareados do experimento passaram nas duas APIs.
- Resultados completos, hashes dos executáveis e limitações:
  `docs/native-renderer-performance.md`, seções Fourth/Fifth pass.
- Auditoria final: D3D12 1325570 verificações / 1952 mudanças; Vulkan
  116541 verificações / 311 mudanças. Nenhuma mudança sem notificação nos
  intervalos completos. Não prova cobertura de escritores. Gates das imagens
  da build final também passaram; Vulkan teve maior variação da cena.

## Artefatos locais

- `logs/bench_native_c5_{before,after}_{d3d12,vulkan}.log` e PNGs/CSV.
- `logs/bench_native_c5_audit_{d3d12,vulkan}.log` e PNGs/CSV, cache desligado.
- `logs/native_c5_profile_summary.json`, `logs/native_c5_image_gates.json`,
  `logs/native_c5_texture_audit_summary.json`.
- Scripts correspondentes em `build/summarize_native_c5*.ps1` e
  `build/compare_native_c5.ps1` (comparação usa Windows PowerShell 5.1).
- Executáveis anteriores em `artifacts/native-renderer-performance/`;
  precisam rodar junto às dependências da pasta release.

## Continuação recomendada

Investigar escritores das texturas antes de substituir hashes por versões.
A auditoria atual registra base/formato/tamanho para mudanças sem notificação,
mas não identifica o PC escritor. Ausência de falhas nas cenas medidas não
prova cobertura completa. Verificar vídeo, UI, mipmaps, aliases, reutilização
de memória e resolves; preservar snapshots e recursos de submissões anteriores.
`InvalidateGuestRange` ainda trata buffers, não constitui um contrato completo
de invalidação de texturas. Não reutilizar endereços de hooks de outros jogos.

Não há commit nem publicação desta rodada. Preservar mudanças preexistentes,
inclusive `port/superman_returns_manifest.toml`. Não executar compilação ou
testes pesados junto de medições de FPS. Build usa VS 18/BuildTools e LLVM local.
