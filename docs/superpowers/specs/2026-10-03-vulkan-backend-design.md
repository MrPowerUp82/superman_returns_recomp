# Backend Vulkan — arquitetura e primeiro marco

## Objetivo aprovado

Implementar um backend Vulkan real no Windows, preservando o renderer Direct3D 12
como referência e criando código de renderização reutilizável em uma futura versão
Android. O usuário aprovou a migração gradual e pediu um branch separado:
`codex/vulkan-backend`.

Vulkan não é uma garantia de aumento de FPS. A comparação de desempenho será feita
com cenas equivalentes, na mesma GPU e configuração, depois da equivalência visual.
O launcher local existente permanece disponível durante o desenvolvimento.

## Estado atual e limites

- `Renderer` em `port/src/native_renderer/native_renderer.*` mistura interpretação
  guest, recursos D3D12, gravação de comandos e apresentação.
- `NativeGraphicsSystem` cria diretamente um `D3D12Provider` e um presenter do SDK;
  também atende MMIO, interrupções, fences guest e vblank.
- O tradutor runtime produz DXIL e usa ferramentas em processos separados.
- O ZIP fornecido contém uma referência de inicialização Vulkan e buffers, sem
  apresentação ou renderização do jogo. Seu README não define capacidades existentes
  neste repositório. Reaproveitar código exige conferir comportamento e licença.
- O runtime ReXGlue instalado e o código recompilado são uma base Windows x64.
  Um backend Vulkan portátil não torna esse runtime compatível com Android ARM64.
- As mudanças locais do launcher não serão descartadas ou misturadas em commits
  de implementação Vulkan por simples inclusão automática no stage.

## Decisão de arquitetura

Migrar por marcos verificáveis, mantendo os hooks e a interpretação do estado guest
comuns aos dois backends. A tradução de comandos guest não deve ser reimplementada
independentemente em Vulkan. Separar código ao introduzir cada operação compartilhada,
sem mover o renderer inteiro para uma interface genérica de uma só vez.

As fronteiras finais são:

1. **Frontend guest:** hooks XDK, estado PM4, captura de memória, endianness, formatos
   de textura e normalização de primitivas. Não expõe handles D3D12 ou Vulkan.
2. **Backend gráfico:** recursos, uploads, bindings, pipelines, draw, clear, resolve,
   barreiras e submissão. Os backends mantêm seus próprios handles e caches.
3. **Apresentação da plataforma:** janela/surface, tamanho, swapchain e modo de tela.
   A adaptação Win32 fica separada do código Vulkan comum; Android terá uma adaptação
   futura usando sua própria janela nativa.
4. **Contrato guest de sincronização:** permanece atendido pelo sistema gráfico,
   independentemente da API. A conclusão de submissões host controla a vida útil de
   recursos host; não substitui indiscriminadamente fences e interrupções guest.
5. **Shaders:** HLSL emitido a partir do jogo é comum; DXIL e SPIR-V têm compilação,
   metadados, mapeamento de bindings e caches distintos.

O backend Vulkan deve carregar o loader dinamicamente. Os headers e ferramentas
de desenvolvimento não devem exigir a instalação do Vulkan SDK no PC do jogador.
Não introduzir dependência em WPF, COM, HWND ou Direct3D no núcleo Vulkan portátil.

## Marcos e critérios de saída

### M1 — Fundação Vulkan com renderização verificável

Este é o primeiro subprojeto e o escopo da primeira implementação. Construir uma
biblioteca Vulkan própria e um executável de smoke test no Windows. Ele deve:

- enumerar GPUs, consultar capabilities e selecionar explicitamente uma GPU;
- criar instance, device, filas, surface Win32 e swapchain;
- carregar shaders SPIR-V de um triângulo de teste criado pelo projeto;
- executar upload de vértices, clear, draw e apresentação de quadros;
- tratar resize, janela minimizada, swapchain desatualizada e encerramento;
- usar fences/semaphores corretamente e destruir recursos após sua conclusão;
- registrar GPU, versão Vulkan, capabilities exigidas e falhas compreensíveis;
- compilar como alvo opcional sem alterar o caminho padrão D3D12 do jogo.

Escolha inicial: Vulkan 1.1, render passes convencionais e sincronização binária.
Não exigir dynamic rendering, timeline semaphores ou descriptor indexing neste marco.
Features adicionais da renderização do jogo serão levantadas no marco de bindings.
Não assumir disponibilidade no Android a partir do suporte da RTX 2060.

O código comum ficará em `port/src/graphics/vulkan/`; adaptação Win32 e smoke test
ficarão em arquivos separados. A target CMake do núcleo não depende do SDK ReXGlue.
Um alvo independente permitirá testar a fundação sem o jogo, além da integração
opcional pelo CMake principal. As dependências serão fixadas por revisão/versão.

Sucesso de M1 significa triângulo apresentado corretamente e testes de ciclo de vida
passando. Não significa que Superman Returns já renderiza via Vulkan. O launcher
continuará indicando que Vulkan está indisponível para o jogo.

### M2 — Frontend compartilhado e shaders do jogo

Definir estruturas neutras para operações concretas, reaproveitar a leitura de estado
guest e produzir SPIR-V do tradutor existente. Separar cache por backend, versão do
emissor, opções de compilação e bindings. Validar constantes, vertex fetch, sampler,
coordenadas, orientação do viewport e clipping com capturas reais.

O plano desse marco será escrito depois de M1: os bindings exigidos pelo corpus
devem orientar as capabilities e os fallbacks, em vez de importar exigências desktop
para todos os dispositivos móveis.

### M3 — Renderização integrada do jogo

Implementar texturas/untile/formatos, alvos color/depth, pipelines, blend/stencil,
draw indexed/inline, primitive restart, resolves e apresentação. Migrar grupos de
operações com testes e comparação D3D12/Vulkan; preservar as correções de vídeos,
USHORT2 do HUD e separadores da arena de War World.

Adicionar `sr_native_api=d3d12|vulkan` somente quando houver seleção funcional no
jogo. Seleção explícita com API indisponível deve apresentar erro claro; não pode
iniciar D3D12 silenciosamente e registrar Vulkan como ativo. O padrão continua D3D12
até a validação de paridade.

### M4 — Launcher, distribuição e desempenho

Expor Vulkan no launcher quando M3 atender aos critérios de renderização. Enumerar
e selecionar GPUs com identidade apropriada à API, sem reutilizar índices DXGI como
índices Vulkan. Distribuir ferramentas necessárias ao cache SPIR-V sem shaders
derivados do jogo. Medir frametimes CPU/GPU em cenas repetíveis da cidade e War World,
mantendo mesma GPU, escala, filtros e limite de quadros.

## Verificação

M1 exige testes unitários das decisões de capability, seleção de GPU e escolhas de
swapchain, mais smoke test real na RTX 2060. Com validation layers disponíveis, o
smoke test deve terminar sem erros de validação. Ausência das layers é registrada e
não impede execução normal; erro de validação presente impede considerar o marco
validado. Exercitar apresentação, resize, minimização/restauração e encerramento.

Rodar também a build D3D12 e os testes nativos existentes para confirmar que a
fundação opcional não alterou o renderer atual. GPU sem Vulkan, versão insuficiente
e surface sem apresentação suportada devem produzir falhas explícitas.

Na integração do jogo, validar cutscenes, HUD/minimapa e objetivos dos meteoros,
cidade e arena de War World com slot 1. Conferir estados intermediários quando uma
imagem final não explicar diferenças. Registrar separadamente compilação de shaders,
renderização estável e custo de apresentação nas medições de desempenho.

## Android futuro

O núcleo Vulkan e estruturas guest neutras poderão ser reutilizados. APK, launcher
Android, entrada touch, áudio, filesystem, tradução de shaders no dispositivo e
runtime ARM64 não pertencem a este marco. O suporte a dispositivos Android e sua
versão mínima será decidido após levantamento de features e formatos exigidos pelo
jogo, não pelo triângulo de teste.

## Fontes técnicas

- DXC e SPIR-V: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst
- Consulta de features: https://docs.vulkan.org/spec/latest/chapters/features.html
- Plataformas Vulkan: https://docs.vulkan.org/guide/latest/platforms.html

## Próximo passo

Revisão desta especificação pelo usuário, seguida do plano de implementação de M1.
Cada marco posterior recebe um plano específico e critérios de paridade antes de
ser habilitado para usuários.
