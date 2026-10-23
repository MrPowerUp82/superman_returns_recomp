# Auditoria dos recursos mostrados no fork — 23/10/2026

## Pedido e evidências

O usuário pediu os recursos mostrados em dois prints do fork privado: 60 FPS reais, sombras e janelas corretas, RT shadows, shadow softness, character shadows, RT reflections, NVIDIA DLSS e um menu de opções no estilo do jogo. A orientação anterior de adiar áudio permanece vigente.

As imagens foram preservadas em `artifacts/parity-reference-20261023/fork_claims.png` e `graphics_menu.png`. O texto do autor é um relato externo; o menu mostra rótulos e valores, mas as imagens sozinhas não demonstram funcionamento de RT/DLSS, desempenho ou a implementação usada. Não foi obtido código do fork.

Antes desta auditoria, foi executado `build.cmd` com SDK de tracking On, saída `port/out/build/win-amd64-write-tracking` e launcher Off. Exit 0, log em `logs/parity_reference_20261023_build.log`. Essa saída recompilou os fontes existentes; nenhum novo código de renderer foi adicionado nesta auditoria. EXE resultante SHA256 `270EEDF86FB8B134629E71B3756B97D52AF01FB2F216C2EC14F2691F4A6AA7BE`.

HEAD continua `bc286c5`, incluindo o flush XMA de `c2d7517`. A modificação de implementação presente é a instrumentação de constantes autorizada na etapa anterior. Os relatórios locais anteriores permanecem não commitados. A máquina enumera Intel UHD e NVIDIA RTX 2060; essa enumeração não substitui consultas de capacidade DXR, Vulkan RT ou DLSS no adaptador efetivamente selecionado.

## Lacunas verificadas no código

| Recurso | Evidência local | Consequência |
| --- | --- | --- |
| Renderer nativo | Caminhos D3D12 e Vulkan já existem | Reaproveitar buffers, shaders traduzidos e capturas é possível |
| Bypass das rotinas Xbox D3D | `native_hooks.cpp` documenta e implementa chamadas às rotinas originais antes do encaminhamento | O backend gráfico nativo não implica que toda preparação XDK foi eliminada |
| Passes da cena | `game_profile.h:298–303`: shadow, opaque, end-tiling, sorted, upscale e HUD são todos `-1` | Ainda falta identificação dos pontos corretos para inserir efeitos e separar HUD |
| Câmera | `projection_regs.inc` vazio; `kInverseViewProjectionName=nullptr` | `CaptureCamera()` depende de uma tabela que ainda não foi populada |
| Alguns efeitos existentes | `Renderer::Init` desliga opções dependentes de passes desconhecidos | A presença de um cvar não comprova que o efeito esteja ativo neste jogo |
| RT | Busca em `port/src` não encontrou Streamline/NGX, estruturas de aceleração, DispatchRays, TraceRay ou pipeline Vulkan RT | Integração de geometria e iluminação ainda precisa ser construída |
| DLSS | Não foram encontrados buffers/contrato de motion vectors, jitter e integração DLSS | Não há caminho DLSS pronto para conectar ao menu |
| UI | Configurações atuais estão em `launcher/` e cvars; `OnCreateDialogs` no app é apenas exemplo comentado | O menu guest no estilo do print exige mapear a UI do jogo ou desenhar uma integração própria |
| Texturas empilhadas | D3D12 ainda rejeita `decoded.depth != 1` para 2D; heaps traduzidos são 2D/3D/Cube | Precisa verificar semântica e seleção de camada antes de escolher o tipo de SRV |
| Vulkan | `ValidateNativeApi()` ainda restringe escala, MSAA e qualidade de sombras e rejeita alguns efeitos | Não assumir paridade dos novos recursos entre APIs sem implementação e validação separadas |

`DrawPacket` já preserva streams, índices, atributos, shaders, constantes e texturas. Isso é uma base de dados de rasterização, não uma representação já validada de malhas em espaço do mundo com identidade estável, poses anteriores, skinning e materiais para RT. O código de câmera existente é uma base potencial, não prova de câmera temporal disponível.

Referências de código: `port/src/native_renderer/{native_hooks.cpp,game_profile.h,projection_regs.inc,native_renderer.cpp}`, `port/src/graphics/guest/render_packet.h`, `port/src/graphics/backend_selection.cpp`, `launcher/LauncherSettings.cs` e `.tools/xenosrecomp/src/XenosRecomp/shader_common.h`.

## Medições que permanecem válidas

Não foi rodado outro gameplay nesta auditoria. Resultados completos e limitações estão em `validation-stacked-xma-limiter-2026-10-23.md` e `d3d12-worker-profile-2026-10-23.md`.

- Não foi reproduzido platô de 33,3 ms com CPU/worker/GPU ociosos. O limitador interno relatado pelo autor não foi confirmado nem descartado quando mascarado pelo custo atual.
- Última rodada instrumentada: worker da timeline 64,71 ms; GPU real 6,90 ms; intervalo médio 74,2 ms. O perfil acrescenta overhead; 60 FPS não foram atingidos.
- Obtenção/sanitização + cópia VS/PS somaram 6,169 ms/quadro. Essa parcela isolada não explica o custo inteiro nem basta para chegar a 16,67 ms.
- As rodadas anteriores de voo nas duas APIs registraram zero rejeições de stacked 2D e mostraram janelas iluminadas. A guarda de rejeição existe, mas sua relação com uma falha visual de sombra ainda não foi confirmada.
- Não há captura do console da mesma cena para validar fidelidade exata de sombras. As imagens anexadas mostram o menu, não uma comparação de sombras da cena.

## Requisitos externos conferidos

O guia oficial [Streamline DLSS](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md), consultado nesta auditoria, exige profundidade, vetores de movimento, cor de entrada na resolução de renderização e cor de saída; também descreve constantes da câmera, jitter, suporte por adaptador e avaliação no ponto de upscale. Exposição pode ser fornecida pelo jogo ou calculada pelo modo autoexposure. Não confundir DLSS Super Resolution com frame generation. O modo Quality do print é tratado aqui como requisito de Super Resolution.

O [guia geral do Streamline](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md) documenta caminhos D3D e Vulkan, integração de dispositivo e lifecycle. A versão a integrar deve ser fixada após escolher o caminho; não instalar automaticamente o SDK mais recente nem assumir compatibilidade por nome da GPU.

A [documentação Microsoft de DXR](https://devblogs.microsoft.com/directx/announcing-microsoft-directx-raytracing/) descreve geometria, instâncias/transforms e estruturas de aceleração. Capturar só a imagem final não fornece essa representação da cena.

## Abordagens para decisão do usuário

### A — Evoluir o renderer atual, D3D12 primeiro (recomendação)

Reaproveitar os dados já capturados e validar sua transformação em geometria de cena, materiais e estado temporal. Mapear passes/câmera/UI; corrigir recursos visuais somente onde o defeito for reproduzido; reduzir custos comprovados de CPU; construir o caminho temporal para DLSS SR e o caminho de geometria/iluminação para RT; conectar cada recurso funcional ao menu; depois implementar e validar a paridade Vulkan.

Tradeoff: preserva a base que já desenha gameplay e permite entregas comparáveis. A captura atual ainda custa CPU e pode exigir bypasses seletivos de rotinas XDK, demonstrando equivalência de estado antes de suprimi-las. A estratégia não garante antecipadamente 60 FPS.

### B — Substituir primeiro a captura por hooks de RenderWare

Localizar e redirecionar submissão de malhas, materiais, transforms, skinning, câmera e UI antes das rotinas XDK. Manter a simulação recompiled e implementar a representação host desses dados.

Tradeoff: pode eliminar trabalho anterior ao backend e expor dados de objeto mais diretamente, mas exige engenharia reversa de contratos que ainda não estão identificados. A quantidade de rotinas, layouts e caminhos dinâmicos afetados é incerta; o primeiro resultado visual comparável tende a exigir mais trabalho. Não presumir que o fork privado escolheu essa arquitetura a partir dos prints.

## Critérios para chamar um recurso de entregue

1. 60 FPS: medir quadros renderizados pelo jogo em gameplay, com frame generation separado, incluindo simulação/animação e cadência. Sem alterar constantes 1/30 por semelhança textual.
2. Sombras/janelas: localizar draws e recursos, obter comparação da mesma cena e respeitar o contrato de shaders e camadas. RT não substitui a validação da iluminação original.
3. DLSS SR: validar profundidade, vetores de câmera e objetos, jitter, disoclusões, reset de histórico e separação de HUD, com caminho Off preservado.
4. RT shadows: raios contra geometria correta, incluindo personagem/objetos relevantes e tratamento de materiais recortados; controles de softness devem mudar o efeito real.
5. RT reflections: geometria, materiais e composição corretos, incluindo casos fora da tela; validar estabilidade e custo de atualização.
6. Menu: layout inspirado no print, controle por teclado/gamepad, aplicação e persistência reais, estados de suporte baseados no dispositivo e no recurso entregue. Não marcar um recurso como On quando o backend não o executa.
7. Vulkan: aceitação própria para cada recurso portado; a presença do backend Vulkan atual não satisfaz automaticamente esses critérios.

O desenho final e a sequência detalhada dependem da escolha A/B. Nenhum commit, push, dependência de DLSS ou novo patch de RT/menu foi feito nesta auditoria. Áudio permanece adiado.

Atualização: o usuário escolheu **A, evoluir o renderer atual com D3D12 primeiro**, e aprovou o desenho em `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`. A aprovação não significa que os recursos tenham sido implementados. A execução começou pelo plano de fundação, com resultados em `docs/parity-d3d12-foundation-2026-10-23.md`.
