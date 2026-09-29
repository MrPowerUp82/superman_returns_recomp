# Renderizador próprio: plano de migração

Status: integração iniciada em 2026-09-28. **Ainda não existe um renderizador próprio completo no projeto.** O modo padrão continua usando o plugin Xenos do ReXGlue; o modo `trace` usa um processador de comandos do projeto e delega os draws ao backend Xenos D3D12.

## Objetivo

Substituir a tradução genérica da GPU Xbox 360 por um caminho de renderização específico para *Superman Returns*, mantendo apresentação, profundidade, HUD e efeitos corretos. A meta de desempenho continua sendo 30 FPS estáveis no i5-13420H com Intel UHD, medidos no mesmo save, parado e em voo.

## O que a referência do Dante realmente faz

O `renderer=native` do Hell's Gate Recomp v0.7.0 usa `rex::graphics::vulkan::VulkanGraphicsSystem` dentro do processo. O [código da fábrica](https://github.com/florinp93/hells-gate-recomp/blob/main/src/native_renderer/dante_graphics_system.cpp) instancia o backend Vulkan do ReXGlue; a [lista de fontes `dante_gpu`](https://github.com/florinp93/hells-gate-recomp/blob/main/CMakeLists.txt) inclui o decodificador de comandos, tradutor de shaders e cache de render targets Xenos. Portanto, esse modo elimina o carregamento do plugin e muda a API gráfica, mas continua interpretando o fluxo da GPU Xbox 360. O `NativePresenter` alternativo apresenta a imagem produzida pelo backend; ele também não substitui os draws do jogo.

Essa implementação ajuda a localizar a interface de integração, mas copiá-la não satisfaz a meta de eliminar a emulação de GPU.

## Ponto de entrada neste jogo

- `SupermanReturnsApp::OnPreSetup` hoje define `gpu_plugin = "xenos"` quando não há outro sistema gráfico.
- O SDK v0.10.0 permite injetar um `rex::system::IGraphicsSystem` em `RuntimeConfig::graphics`. Essa interface precisa fornecer apresentação, MMIO, inicialização do ring buffer, interrupções, armazenamento de shaders e encerramento.
- O jogo chama `VdInitializeRingBuffer` e escreve comandos no ring; `sub_82112050` chama `VdGetSystemCommandBuffer` e `VdSwap`. O código recompilado de `sub_82112050` confirma esse caminho. Um hook apenas em `VdSwap` não recebe geometria, texturas ou estado suficientes para renderizar a cena.
- O decodificador de comandos do SDK chama `CommandProcessor::IssueDraw`, `IssueCopy` e `IssueSwap`. Uma implementação independente pode reutilizar a decodificação do ring temporariamente, mas só deixa de emular Xenos quando esses comandos são convertidos para recursos, pipelines e shaders próprios, sem os caches e o tradutor genéricos.

## Ordem de implementação

1. **Captura de referência.** Com o build atual, registrar uma cena de título e duas cenas reproduzíveis do início de um jogo novo. Guardar FPS, capturas de tela, contagem de draws, hashes dos shaders, formatos dos render targets, cópias, clears, consultas de oclusão e operações de readback. Instrumentar `IssueDraw`/`IssueCopy` na árvore de fontes do SDK, pois a interface pública `IGraphicsSystem` não expõe esses eventos. Toda captura deve ser opcional e ficar em `logs/`.
2. **Classificação dos passes.** Relacionar draws e shaders às funções do jogo: geometria, sombras, céu, transparência, efeitos, pós-processamento e HUD. Confirmar com RenderDoc e com experimentos que desliguem um passe por vez. Não assumir que um shader de título representa o mundo aberto.
3. **Backend próprio com fallback.** Implementar um `IGraphicsSystem` do projeto. Ele deve ler o ring buffer, manter as semânticas de memória/interrupts necessárias e emitir draws para D3D12 ou Vulkan. Começar por clears, triângulos e apresentação; depois adicionar texturas, profundidade, blend, resolve e efeitos. Os comandos ainda não convertidos devem seguir por um caminho de fallback ou impedir a ativação do modo, para evitar quadros parcialmente renderizados sem aviso.
4. **Shaders do jogo.** Substituir shaders Xenos recorrentes por implementações host verificadas contra a captura. Gerar testes de imagem para cada passe e medir o custo. O conjunto final precisa cobrir título, menus, gameplay, HUD e cenas especiais.
5. **Troca do padrão.** Ativar o renderer próprio por padrão só depois de equivalência visual, estabilidade e medições no notebook de referência. Manter o caminho Xenos selecionável para diagnosticar regressões.

## Critérios de aceite

- Nenhum draw da cena medida passa pelo tradutor genérico de shaders nem pelo cache de render targets Xenos quando o renderer próprio está ativo.
- Título, menus e 10 minutos de mundo aberto preservam imagem e áudio, sem crashes ou travamentos.
- O benchmark mede média ≥ 30 FPS e mínimo ≥ 27 FPS, parado e em voo, durante janelas de 20 s no i5-13420H + Intel UHD.
- A comparação informa a API gráfica, driver, resolução interna, configuração de profundidade e cena. Um simples executável Vulkan em processo não deve ser anunciado como renderer próprio.

## Dependências locais verificadas

A ISO local foi lida com `xdvdfs`; `game/default.xex` tem SHA-256 `C8F243ACD99DE9A91F5AE4F409721C0E954E3D5EB96861419D3DA07B8106DB2B`. ReXGlue v0.10.0 gerou o código com sucesso. `game/`, `.tools/`, `port/generated/default/` e `logs/` são ignorados pelo Git.

Uma execução curta do executável atual, com `--sr_skip_intro=true --dump_shaders=<diretório>`, gerou 123 dumps de microcódigo distintos (64 vertex e 59 pixel) antes de 35 s. O log registrou várias falhas de criação de pipeline e FPS entre 0,6 e 11 durante a inicialização; esses números **não** são um benchmark estável. Os dumps locais ficaram em `logs/shaders/` e não devem ser publicados com o repositório.

`python tools/gpu_shader_inventory.py logs/shaders logs/shader_inventory.csv` gera um CSV com estágio, hash, tamanho do microcódigo e quantidade de variantes traduzidas. O CSV local serve para acompanhar a cobertura da migração sem incluir os shaders no Git.

## Primeiro marco implementado: captura no processador de comandos

`port/src/native_renderer/sr_graphics_system.cpp` injeta um `IGraphicsSystem` próprio via `RuntimeConfig::graphics` quando `sr_renderer=trace`. O processador intercepta `IssueDraw`, `IssueCopy` e `IssueSwap`, grava estado e hashes em CSV, e depois usa o D3D12 Xenos para produzir a imagem. Os arquivos de GPU do SDK v0.10.0 são compilados dentro do processo a partir de um checkout local em `.tools/rexglue-sdk-source`; `tools/setup_gpu_source.ps1` prepara esse checkout e verifica a revisão exata. O binário sem o checkout ainda compila, mas não oferece o modo de captura.

Com `tools/capture_gpu_trace.ps1 -Name title180 -StartFrame 180 -Frames 120`, a captura local registrou 268.088 chamadas de draw aceitas, 3.120 cópias e 120 swaps, agrupados em 140 combinações de shaders e estado. Esse intervalo era **o menu sobre a cidade**, não gameplay. O CSV fica em `logs/gpu_trace_title180.csv` e o resumo em `logs/gpu_passes_title180.csv`, ambos ignorados pelo Git.

O comando `tools/capture_gpu_trace.ps1 -Name gameplay120 -Gameplay -Frames 120` aguarda as barras azul e vermelha do HUD e então dispara a gravação. Uma captura local da rua inicial registrou 463.401 draws aceitos, 3.140 cópias e 120 swaps, em 169 combinações de shaders e estado. A combinação VS `E4C1704E825A22E9`, PS `BE398F0A17FF758A`, modo EDRAM 4 somou 67.306 draws. O CSV, o resumo e a imagem de referência ficam em `logs/` e não são distribuídos. O próximo trabalho é identificar o efeito visual dessa combinação e substituí-la por uma execução host específica, com comparação de imagem e FPS.

Uma captura RenderDoc do menu com cidade ao fundo (feita antes da correção da navegação em `tools/capture_frame.ps1`) registrou 19.054 ações em três quadros e 218,17 ms de tempo somado de GPU: 188,45 ms (86,4%) em draws, 17,41 ms (8,0%) em cópias/resolves e 11,90 ms (5,5%) em dispatches. O relatório local está em `logs/gpu_profile_native_baseline.txt`. Esses valores servem para ordenar a investigação do menu, não para calcular FPS ou caracterizar o custo do gameplay.

A captura RenderDoc corrigida, já na rua (`logs/rdc/gameplay_reference_capture.rdc`), registrou 18.516 ações e 220,13 ms somados em três quadros: 190,16 ms (86,4%) em 11.826 draws, 17,43 ms (7,9%) em 6.147 cópias/resolves e 12,13 ms (5,5%) em 286 dispatches. O relatório local está em `logs/gpu_profile_gameplay.txt`. A soma é de eventos de GPU de três quadros e não é um FPS diretamente observável. Os draws são o alvo principal do renderer específico.

O benchmark inicialmente media o menu por engano. A automação agora pressiona Start e depois A, verifica o HUD e inicia um jogo novo. Uma execução de referência no chão mostrou 15,1 FPS médios parado e 14,4 FPS andando para a frente, em janelas de 20 s. Ao desligar em diagnóstico a combinação de shaders acima, as mesmas janelas deram 17,0 e 16,3 FPS médios; porém as imagens terminaram em posições diferentes. Essa comparação não prova ganho em cenas idênticas e **o modo diagnóstico remove desenhos, não os substitui por renderização própria**.

Uma captura pontual na posição inicial, com o passe desligado, manteve a imagem principal aparentemente intacta e registrou 553 draws pulados naquele quadro. Ainda é necessário comparar alvos intermediários e cenas com sombras, transparência e câmera diferentes; a imagem final de um único ponto não permite classificar esse passe como redundante.

## Investigação do passe de vegetação

Na captura RenderDoc de gameplay, draws desse grupo escrevem em um alvo intermediário `R16G16_SNORM` de 640×1024. A visualização do primeiro canal mostra silhuetas de copas de árvores; portanto, o passe tem um efeito intermediário mesmo quando a comparação da imagem final não o evidencia. Em um draw examinado, o teste de profundidade está desligado e a mistura de cor usa `MIN` com fatores `ONE`. Esses estados tornam a operação de cor independente da ordem **para esse draw**, mas um agrupamento só poderá cruzar outros draws após verificar seus estados e as fronteiras de cópia/clear.

O modo `trace` agora aceita `sr_gpu_pass_probe_path`, `sr_gpu_probe_vs_hash` e `sr_gpu_probe_ps_hash`. Uma captura de um quadro com o par VS `E4C1704E825A22E9` / PS `BE398F0A17FF758A` registrou 520 draws selecionados: um buffer de vértices compartilhado, 213 faixas de índices, 248 conjuntos de constantes `c0`–`c3` e sete conjuntos `c4`–`c10`. O registrador de mistura foi idêntico nos 520 draws; 516 usaram o mesmo valor de controle de profundidade. As quatro exceções exigem análise separada.

Sem cruzar as cópias, o maior trecho contém 459 draws do par selecionado e 213 faixas de índices. Isso sugere uma oportunidade de instanciamento, mas **não é ainda uma implementação nem uma estimativa de FPS**: o shader nativo precisará receber as constantes de cada instância, usar a geometria e a textura corretas e escrever no mesmo alvo com semântica equivalente. `python tools/gpu_pass_probe_report.py logs/foliage_probe2.csv` reproduz o resumo da amostra local. `tools/rdc_inspect_pass.py` e `tools/rdc_dump_target.py` inspecionam o alvo na captura RenderDoc.
