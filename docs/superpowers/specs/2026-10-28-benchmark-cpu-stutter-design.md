# CPU e stuttering no benchmark — design

## Intenção e escopo escolhido

O usuário quer reduzir o trabalho de CPU e as travadas no benchmark embutido do
sobrevoo da cidade. Escolheu diagnóstico seguido de correções pontuais, com
comparação antes/depois. A prioridade é regularidade dos quadros, preservando
qualidade visual e comportamento do jogo; percentual de CPU isolado não é sucesso.

Este trabalho inclui diagnóstico, uma otimização por vez, testes e validação na
máquina atual. Não inclui publicação de uma nova release. O desenho precisa de
aprovação antes da implementação; depois será escrito o plano de execução.

## Evidências existentes e limites

- Máquina atual: Intel Core i7-10750H, Intel UHD e NVIDIA RTX 2060, identificados
  via Win32_Processor/Win32_VideoController. Confirmar pelo log a GPU realmente
  selecionada em cada rodada; não presumir a seleção pelo nome do adaptador.
- `docs/renderer-gangg111-hotspots-2026-10-28.md` e
  `docs/perf-hunt-game-xenia-2026-10-28.md` documentam otimizações já presentes:
  scratch CPU para upload, tracking de texturas/buffers, cópias SEH e invalidação
  dirigida de slots. Os tempos recentes desses relatórios vieram de outra máquina.
- Os números de 60/108 FPS das capturas são relatos externos, sem build disponível
  para comparação equivalente. São hipóteses de investigação, não metas comprovadas.
- O benchmark já exporta JSON/CSV, mede 60 s após 5 s de aquecimento, detecta FMV e
  neutraliza entrada. Os tempos exportados são intervalos de presents guest;
  sozinhos não demonstram o ritmo efetivamente exibido pelo host.
- O voo varia entre aberturas: a documentação encontrou ±4,5% na média. Três rodadas
  reduzem a dependência de uma única execução, mas não eliminam essa variação.

## Abordagens consideradas

1. **Diagnóstico do benchmark e correções pontuais (escolhida).** Localiza os picos,
   distingue trabalho de CPU de esperas e valida cada mudança separadamente. Custo:
   várias rodadas e coleta antes de escolher a otimização.
2. **Reimplementar diretamente a submissão RenderWare.** Perfis antigos apontam
   funções caras por draw, mas a engenharia reversa é maior e não comprova que elas
   causam as travadas atuais. Pode virar trabalho futuro se a medição justificar.
3. **Ajustar somente limite de FPS/cadência.** Pode melhorar apresentação, mas não
   basta para demonstrar redução de trabalho de CPU. Só investigar sincronização
   se houver evidência de que participa dos picos.

## Protocolo proposto

As escolhas numéricas abaixo são propostas do agente, sujeitas à revisão.

### Configuração e isolamento

- Nativo, Direct3D 12, RTX 2060, 1280×720, escala 100%, sombras originais, MSAA 1,
  FXAA e geração de quadros desligados. VSync desligado e FPS sem limite para
  revelar trabalho efetivo. Registrar todos os argumentos e a configuração final;
  preferências persistidas não podem mudar silenciosamente os controles.
- Preservar os binários de referência com hashes e usar saída separada para a
  variante. Sem compilação, testes ou outro benchmark concorrente com a medição.
  Registrar alimentação, processos relevantes e condições térmicas disponíveis.
- Usar a duração normal de 5 + 60 s, sem câmera, simulação ou cena especiais.
  Começar com `--sr_benchmark=true --sr_benchmark_exit=true`, janela visível para
  conferir o sintoma. Modo headless só se uma comparação curta confirmar equivalência;
  não misturar modos na mesma comparação antes/depois.
- Preservar caches existentes. Fazer testes frios em cópias/locais isolados após
  identificar todos os caches controlados pelo aplicativo. Registrar separadamente
  bibliotecas de shaders, cache de tradução e PSOs; não chamar o cache do driver
  de frio sem poder demonstrá-lo. Não apagar dados do usuário nem arquivos do jogo.

### Coleta

1. Guardar pelo menos uma rodada inicial com caches do aplicativo frios e três
   válidas aquecidas, documentando o que foi aquecido. Aquecer antes da série,
   sem selecionar apenas as melhores rodadas. FMV, interrupção da cena, falha de
   salvar ou configurações divergentes invalidam a rodada e ficam registrados.
2. Baseline e aceitação com perfis detalhados desligados. Registrar FPS médio,
   1%/0,1% lows, p50/p95/p99, máximo e contagens de quadros acima de 33,3/50/100 ms,
   normalizadas por minuto. Não cortar os picos por parecerem outliers.
3. Rodadas diagnósticas separadas: reutilizar `SR_FRAME_TIMELINE`, detalhes do
   front-end, perfis de worker/constantes/streams e perfil guest, habilitando só o
   necessário para a hipótese. Correlacionar fases/frames com o CSV do benchmark;
   não alinhar números de quadro distintos por suposição. Ancorar em IDs/timestamps
   comuns e documentar a conversão ou a incerteza.
4. Coletar CPU acumulada por thread com `sample_thread_cpu.ps1`, identificando IDs
   de jogo, worker e demais threads pelo log. O contador do SO tem granularidade
   de amostra: serve para médias, não para atribuir uma travada de um único quadro.
   CPU ocupada por quadro = tempo de CPU na janela / quadros da mesma janela;
   separar isso do tempo decorrido em fences, locks e filas. Não somar regiões
   aninhadas nem converter resíduo da timeline em tempo puro de lógica guest.
5. Verificar apresentação real usando instrumentação existente; se insuficiente,
   acrescentar captura mínima de timestamps de apresentação/fence, desligável.
   Relatar separadamente cadência guest e host e limitações da observação.

## Escolha e construção da correção

Agrupar picos por compilação de shaders/PSOs, streaming/IO, hash e invalidação,
submissão/constantes, contenção de locks, espera de GPU ou cadência. Só escolher
o primeiro alvo depois de observar sua correlação e custo. Um pico de GPU não
será apresentado como gargalo de CPU.

Priorizar reduzir trabalho redundante ou contenção em código que já existe.
Cada mudança deve ter fallback para A/B quando viável, preservar a validade dos
recursos e incluir teste da política/invariante realmente modificada. Nenhuma
substituição de lógica guest sem evidência de comportamento equivalente.

A comparação com Xenia Canary usa fontes oficiais fixadas por SHA. REA é reservado
a perguntas sobre o executável/comportamento não respondidas pelo fonte. Verificar
capacidades da sessão quando essa etapa for necessária, reaproveitar evidências e
registrar observação, inferência e limites. Não instalar provedores nem presumir
que as limitações antigas do host continuam iguais.

## Critérios de aceitação propostos

- Comparar três rodadas válidas aquecidas de cada versão; intercalar referência
  e variante com caches equivalentes quando tecnicamente possível. Arquivar todas.
- Meta inicial: p99 de tempo de quadro pelo menos 15% menor e CPU ocupada por
  quadro no estágio alvo pelo menos 10% menor, nas medianas das três rodadas.
  Exigir melhora de cauda em pelo menos duas de três comparações pareadas.
- 1%/0,1% lows e contagens de picos devem corroborar a melhora, sem regressão
  consistente acima de 5% nos lows nem piora consistente de FPS médio acima de 3%.
  Se a variação entre rodadas cobrir o ganho, ampliar para cinco pares antes de
  concluir. Se continuar inconclusivo, relatar como inconclusivo.
- Comparação fria separada: nenhuma regressão recorrente de compilação/streaming;
  repetir se uma única amostra sugerir regressão. Não esconder custo frio aquecendo
  mais tempo ou excluir sua medição do relatório.
- Nenhuma redução de resolução, draws, sombras, efeitos ou precisão; sem novos
  artefatos, falhas de shaders, crash, perda da cena ou mudança de simulação.
  Conferência visual em início/meio/fim, considerando a variação natural de câmera.
- Passar testes relevantes ao código tocado, incluindo benchmark/nativo e memória
  se alterada. Se tocar compartilhados, smoke de Vulkan; se só D3D12, deixar o
  escopo de validação explícito. Uma mudança sem ganho comprovado não vira padrão.
- 60 FPS constantes não são promessa: relatar separadamente o limite restante.

## Entregáveis e decisões deixadas à implementação

Relatório com hashes, configurações, caches, arquivos brutos, fases, tabelas antes/
depois, causa comprovada e limitações; correção revisável e testes pertinentes se
houver alvo com ganho demonstrável. Arquivos gerados em logs/artifacts ignorados.

O implementador escolhe o primeiro hotspot após medir, a menor instrumentação
adicional necessária, o formato do resumo e a estrutura da mudança. Uma
reimplementação ampla de RenderWare ou alteração do comportamento/qualidade exige
revisão do escopo, em vez de expansão silenciosa.
