# Checkpoint 9 — cobertura de mipmaps no D3D12

Data: 2026-10-08. Continuação autorizada pelo usuário a partir do checkpoint 8.

## Correção entregue

O decoder D3D12 lia mipmaps, mas o cache verificava apenas a faixa do nível
base. Uma alteração restrita ao mip tail podia manter a textura GPU antiga.
Agora `TextureEntry` guarda as faixas de `DescribeTextureRanges`, também
utilizadas pela captura Vulkan. Watches, hashes e auditoria D3D12 cobrem todas
essas faixas, com sequência obtida antes de armar os watches e leitura antes
do decode. A frequência de revalidação e o lifetime de recursos continuam
os existentes. Nenhum hash foi retirado; o cache compartilhado permanece opt-in.

`texture_content.h` reúne as operações testáveis usadas no caminho D3D12.
O teste com mipmaps compactados calculados pelo SDK demonstra que uma mudança
no mip tail altera o hash completo enquanto o hash base permanece idêntico.

## Build e validação

- Removido o marcador explícito `THIS_IS_A_SYNTAX_ERROR_TO_TEST_BUILD()`.
- Corrigidos erros da separação de interfaces gráficas já presente: include
  fora do namespace, alias de compatibilidade dos chamadores antigos e uso
  do ponteiro de interface no renderer. O stub Vulkan separado continua fora
  da fábrica ativa; o caminho existente ainda fornece a sincronização guest.
- Build Release passou. 107 testes nativos, orçamento de alocações, 88 Vulkan
  e 12 testes de layout de textura com o SDK passaram.
- O target opcional `sr_guest_texture_tests` foi habilitado no cache CMake
  local com `SR_GUEST_TEXTURE_TESTS=ON`; ele deve ser executado diretamente.
- D3D12 e Vulkan completaram os cenários parado/em movimento. Auditoria:
  790 mudanças D3D12 e 743 Vulkan, nenhuma sem notificação na amostra.
- Gates de imagem grosseiros passaram, mas houve grande variação de cena;
  não constituem comprovação de equivalência visual fina.
- FPS com auditoria: D3D12 27.9/27.9; Vulkan 22.9/23.9. Sem baseline pareado
  do mesmo estado. A correção amplia o hashing e não demonstra ganho de FPS.

Resultados e limitações completos em `docs/native-renderer-performance.md`,
Sixth pass. Artefatos ignorados: `logs/bench_native_c6_*`,
`logs/native_c6_*`, `build/native_c6_build.log` e
`build/native_c6_texture_tests_build.log`. Executável Release SHA-256:
`3715567a248da2e8aa1b33638188145b7addae7781999f32db57098af5a318f0`.

## Próxima investigação

As faixas de mipmaps compactados podem se sobrepor; atualmente são verificadas
separadamente. Coalescer apenas a união dessas faixas pode evitar trabalho
duplicado, desde que leitores/capturas preservem todos os bytes solicitados.
Medir separadamente e manter testes de packed mips/cubemaps/volumes.

Ainda faltam identificar todos os produtores e verificar vídeo, aliases,
reutilização de memória e resolves antes de remover hashes. A ausência de
mudanças não notificadas nesta amostra não resolve esse contrato.

Preservadas as demais alterações preexistentes da árvore, inclusive
`native_graphics_system_vulkan.cpp` e a linha em branco de `superman_returns_app.h`.
Nenhum commit ou publicação. Não compilar durante medições de FPS.
