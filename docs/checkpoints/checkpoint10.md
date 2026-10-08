# Checkpoint 10 — união das faixas de textura

Data: 2026-10-08. Continuação autorizada a partir do checkpoint 9.

## Entrega

`DescribeTextureRanges` agora fornece a união ordenada das faixas físicas
validadas: sobreposições e adjacências são fundidas, sem incluir lacunas.
D3D12 e captura Vulkan usam essa união para watches, hashes e snapshots.
O decoder usa uma descrição privada por nível: não indexar a união por mip.
Todos os pedidos originais permanecem contidos nas faixas capturadas.

O helper `port/src/graphics/guest/texture_ranges.h` faz a união no próprio
vetor. Os hashes são locais ao processo, então a nova divisão de faixas não
exige migração. A periodicidade é mantida; o limite Vulkan de 4 MiB passa a
contar bytes únicos, podendo adicionar verificações periódicas a texturas
antes acima do limite por duplicação. Nenhum hash foi retirado.

## Evidência

- Build Release passou; 110 casos nativos, orçamento de alocações, 88 Vulkan
  e 13 casos de textura com o SDK passaram.
- Fixture packed mip: 4 leituras / 28672 bytes viraram 2 / 24576, com pixels
  idênticos. Comparações adicionais cobrem cubemap, volume, mips lineares e
  snapshot após alteração da fonte.
- Testes da união verificam cobertura exata, lacunas, duplicatas, encaixes,
  adjacência, borda da arena e 256 conjuntos variados.
- Benchmarks pareados da sessão, profiling ligado e auditoria/cache de
  constantes desligados: D3D12 28.0/27.2 → 28.2/28.9 FPS; Vulkan
  20.9/21.3 → 19.9/21.2 (parado/em movimento).
- D3D12: hashing 108430 → 86717.3 KiB/frame, mas tempo 7.973 → 8.160 ms;
  draws 2897 → 2448.3/frame. Não há ganho consistente de FPS/CPU comprovado.
- Gates de imagem passaram: D3D12 33.69 dB / 0.02383; Vulkan
  36.36 dB / 0.01038. Gameplay/HUD conferidos nas imagens.

Detalhes e limitações em `docs/native-renderer-performance.md`, Seventh pass.
Artefatos locais: `logs/bench_native_c7_*`, `logs/native_c7_*`,
`build/native_c7_build.log`, scripts `build/summarize_native_c7.ps1` e
`build/compare_native_c7.ps1` (Windows PowerShell 5.1).

Executáveis preservados em `artifacts/native-renderer-performance/`:

- `native_c7_before.exe`: SHA-256
  `3715567a248da2e8aa1b33638188145b7addae7781999f32db57098af5a318f0`.
- `native_c7_after.exe`: SHA-256
  `1bb9dcbe86aeebd0f6e6c32048ae63c07ab45e3dd8e0ae130373025a7cb4c7e0`.

Rodar junto às dependências de runtime da pasta release. O target opcional
`sr_guest_texture_tests` está habilitado no cache CMake local e deve ser
executado diretamente; não é registrado no CTest.

## Próximo passo

Medir hashes/capturas com entradas idênticas (replay de texturas capturadas),
para separar custo real de variação de cena/tráfego e repetição em cache CPU.
A união reduz bytes repetidos, mas a rodada ainda não mostrou menor tempo de
hash. Identificar produtores e validar vídeo, aliases, reutilização de memória
e resolves antes de retirar hashes ou aplicar versões autoritativas.

Preservadas alterações anteriores da árvore. Sem commit ou publicação.
Não compilar nem executar testes pesados durante medições de FPS.
