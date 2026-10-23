# Recursos do fork no renderer existente — desenho proposto

Status: arquitetura A e desenho aprovados pelo usuário em 23/10/2026 (resposta “sim”). Não é uma declaração de recursos implementados.

## Intenção e escopo

Alcançar recursos funcionais equivalentes aos prints fornecidos: 60 FPS reais, sombras e janelas corretas, RT shadows com suavidade e sombras do personagem, RT reflections, DLSS Super Resolution com modo Quality e menu gráfico dentro do jogo com aparência semelhante à referência. Implementar primeiro no D3D12, depois portar e validar no Vulkan. A simulação recompiled e o renderer atual continuam como base.

Áudio/XAudio2 permanece adiado por decisão anterior do usuário. O flush XMA já existente deve ser preservado. Não obter/copiar código privado nem tratar alegações do autor como diagnóstico comprovado. Não há autorização adicional necessária para diagnósticos, builds e patches reversíveis dentro do desenho aprovado; publicar releases, merge ou push não faz parte desta entrega.

## Abordagens e decisão

1. **Evoluir o renderer existente, D3D12 primeiro — escolhida pelo usuário.** Reaproveita buffers, shaders, capturas e apresentação atuais. Exige corrigir custos de captura e estabelecer dados de cena/temporais, mas permite comparar cada mudança com uma base que já desenha gameplay.
2. **Substituir primeiro a captura por hooks diretos no RenderWare — não escolhida.** Pode expor objetos e remover trabalho XDK, porém depende de contratos ainda desconhecidos de malhas, materiais e skinning. Não iniciar uma reescrita geral. Hooks pontuais para identificar câmera, passes, materiais ou ciclo de vida de objetos continuam possíveis no caminho escolhido, com evidência de endereço/contrato.

## Estado confirmado

HEAD `bc286c5`, contendo `c2d7517`. Há instrumentação local autorizada em `native_renderer.cpp` e relatórios não commitados, que devem ser preservados. A build mais recente passou; a auditoria e os hashes estão em `docs/parity-reference-audit-2026-10-23.md`.

Última rodada diagnóstica: worker da timeline 64,71 ms, GPU real 6,90 ms, intervalo 74,2 ms. Obtenção/sanitização+cópia VS/PS soma 6,169 ms. Não há platô de 33,3 ms reproduzido nem 60 FPS validados. Passes de cena/sombras/HUD estão em `-1`, tabela de câmera vazia, RT/DLSS ausentes. A guarda que rejeita stacked 2D existe, mas não foi exercitada nos voos anteriores. Janelas iluminadas apareceram nas duas APIs. Esses dados impedem atribuir todos os defeitos a um limitador ou textura específica.

## Restrições e princípios de implementação

- Antes de mudar comportamento, apresentar os dados que justificam a mudança. A aprovação deste desenho autoriza executar as etapas sem pedir confirmação para cada build, teste ou escolha rotineira.
- Isolar otimizações e recursos atrás de opções experimentais inicialmente Off; preservar o caminho Off para comparação e máquinas sem suporte.
- Não reduzir silenciosamente qualidade, pular draws, reutilizar dados por endereço sem validade, alterar constantes 1/30 sem prova, nem contar frame generation como quadros reais.
- Não conectar rótulos On a recursos que não estão sendo executados. Separar configuração solicitada, capacidade da máquina e estado efetivamente ativo.
- Medir perfil detalhado e desempenho final em rodadas distintas; registrar EXE/DLL, adaptador, resolução, opções, aquecimento, janela e cena. Não compilar/testar durante medição de gameplay. Informar progresso de testes longos pelo menos a cada minuto.
- Preservar capturas imutáveis e vida útil até o fence. Recursos temporais/RT não podem apontar para memória guest reciclada ou allocations expiradas.

## Arquitetura

### Captura e semântica de quadro

Manter `native_hooks`, o frontend e os bancos PM4 como origem dos draws rasterizados. Identificar no jogo as fronteiras da cena, sombras, transparência, pós-processamento, HUD e menu usando disassembly, shader catalog, binds/clears/resolves e capturas existentes. Não classificar um passe apenas pelo tamanho da textura ou hash isolado. Registrar evidência das rotinas e do significado das constantes antes de preencher `game_profile.h`/`projection_regs.inc`.

Introduzir metadados comuns por quadro: número do guest swap, câmera atual/anterior sem jitter, jitter aplicado, viewport, convenção de profundidade, superfícies de cor/depth da cena, ponto de upscale, separação do HUD e motivo de reset. D3D12 deve poder consumir esses metadados diretamente; o contrato deve ser serializável na captura/replay usada pelo Vulkan, sem refazer a aquisição em cada backend.

### Dados de geometria e materiais

Adaptar streams, índices, constantes e shaders já capturados a uma representação de malhas/instâncias validada. A identidade inclui geração do recurso, intervalo, formato/topologia e versão de conteúdo; shader/transform/material têm identidade própria. Mesma posição de memória não significa mesmo objeto.

Validar transforms, instancing, skinning, cape/partículas e materiais recortados nas famílias relevantes de shader. Guardar pose anterior para objetos animados e atualizar geometria de RT de acordo com a deformação. A extração deve ser desacoplada da criação de BLAS/TLAS para permitir testes com dados capturados e reutilização no Vulkan.

Geometria fora da tela é requisito de reflexos e sombras: usar submissões de caster e um cache de instâncias residentes com remoção por ciclo de vida/streaming confirmado. Se draws visíveis/casters não fornecerem cobertura suficiente, acrescentar observação pontual das estruturas de cena residentes, sem substituir todo o frontend. Não manter objetos descarregados indefinidamente nem apresentar um cache incompleto como cena RT completa. A etapa de extração deve relatar cobertura e faltas por classe/célula.

A versão inicial de extração pode suportar um subconjunto para validar a técnica; isso é um marco intermediário, não a entrega de sombras/reflexos equivalentes. A aceitação inclui cidade, Superman/capa e objetos dinâmicos que apareçam nos percursos de teste.

### Capacidades, estado e recursos host

Criar componentes pequenos sob `port/src/graphics/` para estado temporal, cena extraída e capacidades, com implementações D3D12 próprias e posterior Vulkan. Evitar concentrar toda a nova lógica no arquivo atual do renderer. Nomes concretos e divisão de arquivos são escolhas do implementador, preservando essas responsabilidades.

Consultar DXR e requisitos de shader/dispositivo no adaptador escolhido; consultar DLSS pelo SDK, sem presumir suporte apenas por a máquina conter uma RTX. Inicializar recursos somente quando habilitados e suportados. Registrar falhas e estado efetivo; continuar no caminho raster/Off quando a inicialização não puder completar. Recriação de swapchain, resize, mudança de opção e shutdown respeitam fences e invalidam o histórico apropriado.

### Sombras originais e texturas empilhadas

Identificar um defeito reproduzível e os draws/fetches/resolve slices responsáveis. Preservar camadas e subrecursos no armazenamento; escolher o contrato de amostragem a partir do uso real. Os heaps traduzidos atuais declaram Texture2D/3D/Cube: não criar um SRV Texture2DArray e ligá-lo a uma declaração Texture2D.

Se o shader realmente selecionar camadas, estender contrato de shader, tradução/reflection, binding e SRV juntos. Se consumir uma camada 2D por binding, criar a view correta dessa camada, seguindo o contrato observado. Testar mips, swizzles, resolves e invalidação. Só chamar de fiel ao console com referência comparável; a ausência dessa referência não bloqueia RT/DLSS, mas mantém essa afirmação pendente.

### DLSS Super Resolution

Usar uma versão fixada do NVIDIA Streamline, escolhida e verificada no preflight com o driver/adaptador locais. Encapsular lifecycle/integração em um adaptador D3D12, incluindo a inicialização exigida antes do dispositivo quando aplicável. SDK ausente não deve quebrar a build padrão sem DLSS. Registrar versão, licença redistribuível e hashes dos binários usados; não copiar DLLs de outro jogo.

Fornecer cor da cena antes do HUD, depth com convenção validada, motion vectors e constantes de câmera/jitter. Usar exposição da cena quando identificada, ou o modo autoexposure documentado. Motion vectors devem incluir câmera e objetos: para famílias com deformação, executar as transformações/deformações atuais e anteriores em um passe de velocidade ou variante de shader validada. Movimento somente de câmera não satisfaz a aceitação em Superman/veículos.

Aplicar jitter de forma coerente à rasterização da cena, mantendo matrizes não jittered nos dados temporais. Resolver upscale antes do HUD, restaurar estado D3D12 após avaliar o SDK e manter a UI em resolução de saída. Resetar histórico em load, teleport/camera cut, resolução/modo e invalidação de identidade. Começar com Off/Quality; outros modos podem ser adicionados após esse contrato funcionar.

### RT shadows e sombras do personagem

Criar BLAS para geometria estática com reuso por conteúdo/geração, atualizações para geometria deformada e TLAS por estado das instâncias. A escolha entre pipeline DXR e inline ray queries é do implementador após consultar a capacidade real e fixar os shaders de teste; ambos devem consumir o mesmo contrato de cena.

Mapear direção/parâmetros da luz que o jogo usa. Produzir visibilidade por raios e filtrar com depth/normais/histórico validados. Integrar a visibilidade no termo de iluminação correspondente das famílias traduzidas, evitando escurecer indiscriminadamente HUD, emissivos, bloom e iluminação indireta. Shader families não mapeadas mantêm a sombra original e aparecem no relatório de cobertura.

`Character Shadows` controla a participação do personagem como caster, sem remover o personagem da renderização normal. `Shadow Softness` modifica a distribuição/filtragem dos raios, com valores Off/Low/High definidos no plano de implementação. RT Off mantém o caminho original corrigido. Viés, transparência/alpha test, luz por trás e contato precisam de testes visuais específicos.

### RT reflections

Traçar contra a cena residente, incluindo geometria fora da tela. Obter normais, material/roughness e dados de shading da geometria/famílias verificadas. Integrar o resultado no termo de reflexão especular correspondente, sem somar duas vezes o ambiente já aplicado pelo jogo. Usar a reflexão original como fallback para misses/classes ainda não cobertas e registrar essa cobertura.

Acumulação e denoise devem rejeitar histórico em disoclusões, objetos mudando e camera cuts. Aceitar uma primeira imagem estática como experimento, não como conclusão. Validar movimento, streaming, reflexos de personagem/veículos e troca On/Off, incluindo custo de CPU, GPU e VRAM.

### Opções e menu dentro do jogo

Uma configuração host será compartilhada por cvars, menu e launcher, persistindo opções sem sobrescrever valores concorrentes arbitrariamente. Mudanças são solicitadas pela UI e aplicadas pelo renderer em fronteira segura de quadro; reportar pedido pendente, aplicado ou falho.

Mapear a pausa e a entrada/submenu gráfico do frontend guest. A tela de destino será dentro do jogo, com a composição azul/translúcida, título `GRAPHICS OPTIONS`, rótulos do print, seleção esquerda/direita e retorno B/Esc. Aproveitar o fundo/pausa existentes e assets do usuário quando disponíveis. O implementador pode renderizar o painel no host; deve integrá-lo à pausa/entrada do jogo, sem exigir o overlay de debug F3. Não inventar endereços de hooks para forçar essa integração.

O menu só oferece On para recursos implementados e suportados na API/dispositivo atual, e mostra o estado efetivo. Navegação não pode acionar gameplay por baixo. B/Esc volta à pausa anterior sem despausar indevidamente. DLSS altera resolução/recursos na fronteira segura e reseta histórico. A composição do menu/HUD fica após DLSS e não recebe sombras/reflexos da cena.

## Sequência e critérios de saída

| Etapa | Entrega verificável | Dependência/critério |
| --- | --- | --- |
| 1. Base D3D12 | Benchmark reproduzível, quadro anotado com passes/câmera/superfícies e capacidades do adaptador | Evidências ligadas a draw/rotina/constante; manter dúvidas explicitamente abertas |
| 2. CPU e 60 FPS | Reduzir gargalos comprovados, começando por separar sanitização de escrita VS/PS e pelo restante de slots/streams | Cada patch com comparação e validade de estado; procurar limitador guest apenas quando houver dados de espera/cadência que o justifiquem |
| 3. Correção visual raster | Corrigir sombras/janelas/subrecursos somente onde reproduzido | Contratos de shader/SRV compatíveis e imagens alinhadas; fidelidade ao console depende da referência |
| 4. Contratos temporal e de cena | Motion/depth/câmera em buffers de inspeção; geometria/instâncias/material com cobertura | Testar câmera, animação, identidade, geração e streaming; alimentar tanto DLSS quanto RT |
| 5. DLSS SR D3D12 | Off/Quality funcionais antes do HUD | SDK confirma execução; imagens estáticas e em movimento sem falhas de reprojeção/HUD; dados temporais aprovados |
| 6. RT shadows D3D12 | Sombras RT, softness e caster do personagem | Casos de contato, alpha test, objetos fora da tela e troca Off/On; registrar cobertura e custo |
| 7. RT reflections D3D12 | Reflexos com materiais e geometria fora da tela | Movimento, disoclusões, streaming, fallback e composição sem duplicação |
| 8. Menu | Tela integrada ao pause com os controles funcionais do print | Gamepad/teclado, suporte real, aplicação/persistência e retorno; pode ser desenvolvida ao longo das etapas 5–7 |
| 9. Vulkan | Portar os contratos/recursos e o estado efetivo para Vulkan | Testes próprios de sincronização, lifetimes, shaders e capacidades; não relaxar validação do backend só para aceitar opções |

As etapas são marcos, não autorização para abandonar as seguintes. Um bloqueio de referência de console é localizado; não bloqueia trabalho independente. A meta de CPU deve continuar sendo perseguida mesmo se o primeiro patch não atingir 60 FPS. Caso os dados provem necessidade de mudança estrutural maior que a evolução acordada, apresentar essa evidência antes de trocar arquitetura.

## Validação e definição de conclusão

Escolha proposta pelo implementador: teste final em New Game, após aquecimento, com três percursos de ao menos 60 s (parado, andando e voo pela cidade). Fixar configurações, resolução, adaptador e cena; medir D3D12 720p primeiro na RTX 2060 local. Medir também a matriz RT Off/On e DLSS Off/Quality, publicando o custo combinado; não presumir 60 FPS com todos os efeitos ativos.

**Preset obrigatório de 60 FPS proposto para aprovação:** D3D12, RTX 2060 local, saída e renderização 1280×720, escala 1, MSAA 1, efeitos raster originais preservados, RT shadows/reflections Off, DLSS Off, frame generation Off e limite 60. Fixar VSync On e registrar o refresh/apresentação; se a medição exigir VSync Off para separar pacing, apresentar essa segunda rodada separadamente. Esse primeiro critério isola a meta de CPU que hoje impede 60 FPS. A matriz com RT/DLSS continuará obrigatória como validação funcional e de custo, mas esta proposta não declara 60 FPS com RT ativo como condição já alcançada ou garantida. Se o usuário exigir 60 com RT/DLSS Quality juntos, esse será um critério adicional explícito antes do plano de implementação.

Meta proposta para declarar 60 FPS sustentados no preset de aceitação: média de ao menos 59,5 quadros reais/s e p95 de intervalo até 17,5 ms nas janelas aquecidas, com p99, quedas e eventuais stalls publicados. Essas tolerâncias são uma escolha técnica proposta, não uma exigência já dada pelo usuário. Comparar deslocamento, animação e tempo de simulação por duração real; não aceitar slow motion, lógica acelerada ou frames duplicados como ganho.

Usar testes unitários para regras puras de identidade/versão, reset de histórico, seleção de camada, formatos/endianness e aplicação de settings. Usar testes de integração/capturas para shaders, barriers/fences, SDK e RT. Não criar testes que apenas espelhem uma chamada de API ou um rótulo. Compilar e testar o escopo alterado; reexecutar Vulkan/Off quando mudar contratos compartilhados.

Comparações de imagem exigem mesma cena/câmera/instante ou inspeção específica dos buffers; um gate reprovado por desalinhamento não aprova nem prova regressão. Manter logs brutos, hashes, capturas e relatório de pendências. Nenhuma mudança é promovida a padrão pela existência de um ganho isolado ou porque o fork afirma tê-la.

Conclusão do escopo: funcionalidades acima operando e verificadas em D3D12 e Vulkan, menu integrado e desempenho real relatado sem mascarar limitações. Se a meta de 60 FPS ou a fidelidade ao console ainda não estiver validada, registrar explicitamente a pendência; não chamar o conjunto de equivalente ao fork.

## Decisões delegadas e pontos ainda a descobrir

O usuário decidiu a arquitetura e a ordem das APIs. O implementador decide nomes de módulos/cvars, integração Streamline compatível, organização dos recursos host, escolha DXR/inline, ferramentas de captura e políticas de cache dentro dos contratos acima. As opções de amostragem/denoise e orçamento de memória devem ser fixadas no plano com medição e fallback; não são novas perguntas de produto.

Ainda precisam ser descobertos no código/dados: endereços e contratos de passes/câmera/UI, convenção de profundidade e jitter, identificação de luz/materiais, identidade/vida útil de instâncias e cobertura fora da tela. Descoberta e validação desses fatos fazem parte das etapas 1/4; não são suposições permitidas ao escrever hooks. Referência visual do console continua ausente; só solicitar quando se tornar necessária para aquela comparação, sem interromper outros marcos.

Referências: `docs/parity-reference-audit-2026-10-23.md`, `docs/d3d12-worker-profile-2026-10-23.md`, `docs/validation-stacked-xma-limiter-2026-10-23.md` e os links oficiais Microsoft/NVIDIA já registrados na auditoria.

## Revisão de viabilidade do desenho

Uma revisão independente de planejamento identificou uma decisão de produto material: qual preset deve obrigatoriamente atingir 60 FPS. O preset raster acima foi adicionado explicitamente e aprovado pelo usuário, mantendo RT/DLSS e Vulkan no escopo. As demais escolhas operacionais foram delegadas ao implementador.
