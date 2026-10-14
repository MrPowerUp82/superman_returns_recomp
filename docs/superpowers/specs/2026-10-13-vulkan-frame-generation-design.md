# Frame generation opcional no Vulkan e no launcher

Estado: desenho aprovado pelo usuário em 2026-10-13; implementação ainda não iniciada.

## Pedido e resultado esperado

Integrar geração de quadros diretamente ao render nativo Vulkan do Superman
Returns, com controle de ativar/desativar no launcher. Não usar o aplicativo
Lossless Scaling externo. Não trocar a API do jogo para D3D12/Xenos.

A primeira versão gera um intermediário por par de quadros reais (2x), usando
o backend LSFG-VK já vendorizado e a Lossless.dll instalada pelo usuário.
Novas configurações começam desligadas; a preferência escolhida é persistida.
O botão do launcher afeta a próxima execução, como as demais opções atuais.
Ativar FG não altera simulação, limite real de 30 FPS ou qualidade do renderer.
Não anunciar 30 FPS reais a partir de 15 reais + 15 intermediários.

## Situação verificada

- `backend_selection.cpp` rejeita `sr_lsfg` no Vulkan atualmente.
- `lsfg/presentation.cpp` implementa somente apresentação D3D12, com imports
  externos, cópias de formato, esperas e dois Presents por quadro novo.
- O backend vendorizado já oferece `BorrowedDevice`, `openLocalContext`,
  `sourceImage`, `destinationImage` e `recordFrame`: não é necessário importar
  recursos D3D12 nem criar outro dispositivo para o caminho Vulkan.
- `vk::CommandBuffer` ainda possui apenas construção de buffer próprio; falta
  uma vista não proprietária para registrar os passes num command buffer do jogo.
- O dispositivo atual habilita funcionalidades do renderer, mas não habilita
  timelineSemaphore. O contexto local LSFG cria semáforos timeline.
- O mailbox do presenter transporta imagem/gamma, mas não o serial do swap real.
  Ponteiros de imagem e repaints da UI não identificam um novo quadro real.
- Launcher já tem INI persistente, escolha Vulkan/D3D12 e testes de argumentos.
- DLL local disponível: `C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\Lossless.dll`,
  versão 3.2.2.0, 7.521.280 bytes. Não copiar/redistribuir a DLL na distribuição.

## Alternativas e escolha

1. Aplicativo externo: configuração mais curta, mas não atende à escolha do
   usuário por integração ao Vulkan. Não será aplicado.
2. Ponte Vulkan/D3D12: reaproveitaria o presenter antigo, acrescentando outra
   API, imports e sincronização externa. Não será usada no render Vulkan.
3. Backend no mesmo VkDevice/VkQueue, com imagens locais e passes registrados
   na fila do jogo: escolha desta proposta. Preserva a API e evita cópias CPU.

## Launcher e contrato de configuração

- Checkbox `Geração de quadros (2x)` com estado salvo no INI e restauração nos
  controles. Desativar gera explicitamente `--sr_lsfg=false`.
- Ativar envia `--sr_lsfg=true`, `--sr_lsfg_mode=performance` e caminho válido
  para a DLL em um único argumento, inclusive com espaços/Unicode.
- Campo de DLL com seleção de arquivo, exibido junto à opção, preenchido pela
  instalação existente quando encontrada. Persistir o caminho; não requisitar
  download nem alterar a instalação do Lossless Scaling.
- Não mostrar dados técnicos do backend na interface. A descrição pode dizer
  que o intermediário melhora fluidez e não aumenta FPS da simulação.
- Manter o comportamento D3D12 existente. Configuração sem DLL e FG ligado tem
  erro legível antes de lançar o jogo. FG desligado não depende da DLL.
- O executável distribuído precisa conter o backend opcional (`SR_LSFG=ON`);
  compilações sem ele continuam funcionais e diagnosticam solicitação inválida.

## Integração Vulkan

Uma sessão de FG do presenter cria o backend com os handles emprestados do
Context existente. A sessão só é construída quando a opção está ligada.
O backend nunca destrói VkInstance/VkDevice/VkQueue emprestados.

Na criação do dispositivo, consultar/habilitar timelineSemaphore somente
quando FG for solicitado e suportado. Na API 1.1, habilitar também a extensão
KHR correspondente e resolver corretamente seus entry points. Sem suporte,
diagnosticar que FG não foi ativado; não desativar validações do renderer para
aceitar uma configuração que o dispositivo não consegue executar.

Adicionar uma vista explicitamente não proprietária de VkCommandBuffer no
wrapper vendorizado. A vista só registra comandos: não aloca, reseta, encerra,
submete ou libera buffers/pools pertencentes ao FrameLoop. Recursos do backend
e mudanças vendorizadas preservam os avisos/licença GPL-3.0-or-later existentes.

Transportar o serial de swap real do callback GameFrame até Publish/mailbox.
Executar a atualização do histórico uma única vez por serial aceito; repaints,
quadros duplicados ou uma imagem reutilizada não avançam o histórico.

O frame de entrada canônico aplica a composição/gamma/formato atuais antes da
interpolação, em uma imagem local RGBA8. É criado uma vez por quadro real, não
por draw. O original a apresentar é preservado sem passar pela interpolação.
O backend recebe esse frame por comandos GPU no mesmo dispositivo. Manter
layouts e barreiras explícitos e não registrar mudanças de layout também num
segundo ImageState que discorde do estado real.

No primeiro quadro e depois de reset, copiar a imagem inicial para ambos os
slots RGB e aquecer todos os slots temporais de features, seguindo o requisito
de warmup já validado em LSFG. Apresentar somente o original durante warmup.
Quando chegar outro serial, gerar um intermediário usando os dois frames reais.

Apresentar intermediário seguido de original com cadência baseada no intervalo
de frames reais. Esperas de pacing/fence/acquire ficam fora de gpu_mutex, para
não bloquear a gravação do próximo frame do jogo. A fila continua serializada
no registro/submissão/present, sem novo dono mutável de recursos do renderer.
As imagens necessárias ao par ficam retidas até os fences correspondentes.
Não usar WaitIdle a cada par nem esperar geração inteira pela CPU.

## Reset, erro e ciclo de vida

- Reiniciar o histórico ao mudar extent/formato, recriar swapchain/surface,
  recuperar conexão e ao iniciar outra execução com a opção alterada.
- Parar o produtor, retirar submissions e somente então destruir imagens,
  contexto/instance LSFG, antes do Context Vulkan emprestado.
- Falha ao carregar DLL/inicializar FG antes de registrar trabalho permite
  continuar com originais no mesmo Vulkan, com diagnóstico explícito.
- Falha depois de comandos parciais exige retirar/descartar o trabalho com
  segurança. Não reutilizar command buffer inválido como se fosse original.
- Device lost segue o caminho de erro existente. Nunca cair em Xenos/D3D12.

## Medição e aceitação

Separar contadores de frames reais, intermediários e apresentações repetidas,
tempo GPU de FG, resets/falhas e FPS efetivamente apresentados. Contar apenas
apresentações bem sucedidas; o FPS real existente continua sendo o da simulação.
Não inferir FPS apresentados apenas multiplicando o contador real por dois.

Verificações obrigatórias:

1. Testes de launcher: defaults desligados, ida/volta do INI, on/off, caminho
   Unicode/espaços em um argumento, DLL ausente e API preservada.
2. Testes de política/cadência: warmup, serial repetido, ordem intermediário e
   original, nenhum avanço do histórico em repaint, reset e contadores.
3. Testes de funcionalidades do dispositivo e ownership: FG off preserva os
   pedidos antigos e não cria o backend; vista de command buffer não libera
   recursos do caller; dispositivo emprestado não é destruído pelo backend.
4. Fixture GPU com DLL real e validation layers: quadro inicial, par sintético
   com movimento, original preservado, saída intermediária distinta de simples
   repetição, layouts/lifetimes e reset de dimensões. Zero erros de validação.
5. Builds do jogo/launcher com e sem backend opcional. Testes nativos/Vulkan
   existentes e contratos de pixels passam.
6. Gameplay Vulkan FG desligado/ligado/desligado com a mesma qualidade e cena;
   medir FPS reais/apresentados, custo e imagem. Conferir HUD, menus, movimento,
   resize/alt-tab e saída. Não exigir equivalência de pixels do intermediário,
   mas preservar o original e documentar artefatos observados.

Concluído somente quando a opção do launcher lança o Vulkan com o caminho
implementado, gera/apresenta intermediários reais, funciona também desligada
e as verificações acima fornecem evidência. Nenhuma promessa de 30 FPS reais.

## Referência técnica

[Khronos: VkPhysicalDeviceTimelineSemaphoreFeatures](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceTimelineSemaphoreFeatures.html)
descreve a consulta e habilitação via pNext em VkDeviceCreateInfo. A feature
é fornecida por Vulkan 1.2 ou VK_KHR_timeline_semaphore.

Código local de referência: `docs/lsfg-integration.md`,
`port/src/lsfg/presentation.cpp`, `port/src/graphics/vulkan/platform/native_provider.cpp`,
`port/src/graphics/vulkan/frame_loop.cpp`, `port/third_party/lsfg-vk/`,
`launcher/LauncherSettings.cs`, `tests/launcher/Program.cs`.
