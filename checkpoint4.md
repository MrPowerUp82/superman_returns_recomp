# Correções de vídeos e HUD — 03/10/2026

Continuação de `checkpoint3.md`: correções de vídeos, HUD e geometria de War World.

## Causas e correções

1. **Cutscenes verdes/marrons:** o player de vídeo escreve os fetch constants das texturas Y/U/V diretamente no PM4. Os slots de objetos de textura do dispositivo ficam nulos. `UploadConstants` exigia também um objeto XDK e acabava vinculando texturas vazias. A vinculação agora usa o tipo do fetch constant, com regressão em `tests/native/test_texture.cpp`. A abertura da EA e o vídeo inicial do novo jogo foram vistos com imagem após a correção.
2. **Cursor e elementos do HUD sem posição:** o layout guest usa `0x2C2259` (USHORT2, coordenadas inteiras de 16 bits sem normalização), que `MapDeclType` descartava. Os VS `9C34733AC0F58C2D` e `C8A4458E8F870A92` recebiam a posição padrão e a geometria colapsava. O host agora vincula R16G16_UNORM e o patch `0010-ushort2-vertex-input.patch` recupera os valores inteiros no shader. Os metadados da declaração são preenchidos também nos draws inline; fetches dinâmicos decodificam o formato diretamente.

## Verificação

- Build Release recompilada em `port/out/build/win-amd64-release`.
- 49 testes nativos passaram. A regressão de emissão do tradutor passou; ambos os casos novos foram observados falhando antes da correção.
- Corpus regenerado: 240 shaders compilados (98 VS, 142 PS). Permanece a falha já conhecida de `978B0FF62C3693C2`, com entrada Position0 duplicada.
- Confirmado visualmente: triângulo laranja do player e ponto de destino no minimapa, durante o tutorial da cidade.
- Overlay dos primeiros meteoros: o usuário confirmou que os marcadores de rastreio aparecem na build corrigida, no tutorial inicial da cidade.
- Runtime após a correção: zero draws pulados no log `logs/hud_verified/game.log`. Logs e dumps são locais e ignorados pelo Git.

## Reconstrução

`tools/shaders/build_corpus.ps1 -DumpDir logs/native_shaders -NoSpirv`, seguido de `.\build.cmd`. As ferramentas de shader da distribuição também precisam ser reconstruídas para incluir o patch 0010; o pacote de distribuição deve ser gerado com o tradutor atualizado.

A biblioteca local anterior estava desatualizada (221 shaders). Foi regenerada para os 240 já descritos no checkpoint anterior antes da comparação do HUD.

## War World: geometria da arena (slot 1)

A arena apresentava polígonos gigantes cobrindo o chão, as construções e a câmera. A captura por draw localizou a primeira deformação no shader VS `14392FB0E2BE545C` / PS `8B9C6CFD94AC3509`, durante o draw do chão. O índice varia com a cena (643 ou 644 nas capturas).

- Os vértices CPU e GPU do buffer `1BC2C370`, stride 32, correspondem exatamente: a falha não era o byte swap das posições.
- O draw usa 5.156 índices de 32 bits, no buffer `1BBA1000`. Contém 329 separadores `0x007FFFFF`. O renderer tratava apenas o índice todo em uns como corte de strip; portanto lia os separadores como vértices fora da malha.
- O upload agora converte o separador programável de `VGT_MULTI_PRIM_IB_RESET_INDX` para o corte fixo do D3D12. Respeita a habilitação em `PA_SU_SC_MODE_CNTL`, a largura do índice e os 24 bits usados pelo Xenos. O valor de reset participa da chave do cache de buffers, e o cálculo do intervalo de vértices exclui os separadores.
- A regressão cobre o separador capturado em War World, índices comuns, restart desativado, índices de 16 bits e bits superiores ignorados.

Verificação: 50 testes nativos passaram; build Release concluída; slot 1 carregado pelo usuário. A captura da janela pausada mostrou o chão com seus detalhes luminosos, as torres e os painéis da arena, sem os polígonos gigantes. Runtime: 586.752 draws e zero pulados no ponto verificado (`logs/warworld_fixed/game.log`). Não foi realizado um percurso completo da fase.

A instrumentação temporária de leitura de buffers e rearmamento de capturas foi removida. Logs e capturas de diagnóstico permanecem locais em `logs/warworld_hot`; a build corrigida ficou aberta em War World, pausada. As correções e este checkpoint foram preparados para commit, push e geração do pacote Release a pedido do usuário.
