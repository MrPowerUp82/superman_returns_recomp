# FPS: mecanismo do Xenia Canary e port para o renderer nativo

## Referência e observações

Código oficial consultado em 27/10/2026, branch `canary_experimental`, commit
`72c2afa3ba2b1b232f89de71577251d6edafb1bd`:

- [graphics_system.cc](https://github.com/xenia-canary/xenia-canary/blob/72c2afa3ba2b1b232f89de71577251d6edafb1bd/src/xenia/gpu/graphics_system.cc#L148): seleção de cadência e thread de vblank.
- [MarkVblank](https://github.com/xenia-canary/xenia-canary/blob/72c2afa3ba2b1b232f89de71577251d6edafb1bd/src/xenia/gpu/graphics_system.cc#L429): contador e callback da interrupção.
- [Leitura MMIO](https://github.com/xenia-canary/xenia-canary/blob/72c2afa3ba2b1b232f89de71577251d6edafb1bd/src/xenia/gpu/graphics_system.cc#L360): leitura do estado do registrador.

Observação de código, sem alegar execução do Canary: com `vsync=false` e
`framerate_limit=0`, o Canary chama `MarkVblank()` e dorme 1 ms a cada passagem.
Com VSync ligado e limite zero, escolhe 60 vblanks/s. Também avança o registrador
`D1MODE_V_COUNTER` pela altura da imagem; não devolve uma altura constante.

O snapshot completo da árvore de `xenia-canary/game-patches` consultado foi
`fe957a050e37f5a1e3f485ca43c2e37ee2a9c92d`, 520 entradas, sem truncamento.
Não havia arquivo com `Superman` ou Title ID `454107ED` no caminho. Esse é
um resultado limitado a esse snapshot, não uma afirmação sobre patches privados
ou todas as versões. O mecanismo portado aqui vem do código do emulador.

## Diferenças corrigidas

| Situação | Antes | Agora |
| --- | --- | --- |
| Nativo sem limite e sem VSync | 240 Hz fixos, com prazo em relógio guest | Vblank seguido de espera de 1 ms, como Canary; nominal 1000 Hz |
| Nativo sem limite e com VSync | Ignorava a opção na thread de vblank | 60 Hz, conforme o fallback do Canary |
| `D1MODE_V_COUNTER` via MMIO | Retornava sempre a altura | Retorna o registrador atômico, que avança a cada vblank |
| Novo launcher / execução nativa padrão sem limite | VSync ligado por padrão | VSync desligado; escolha explícita é preservada |

Os seletores existentes 30/60/120 FPS mantêm sua unidade anterior de dois
vblanks por quadro: 60/120/240 Hz. Isso é uma tradução da interface deste
projeto; não equivale a passar esses mesmos números em `framerate_limit` do
Canary. O controle de diagnóstico `sr_native_unlocked_vblank_hz` continua
disponível, agora com padrão 1000. A espera de 1 ms não garante 1000 interrupções
efetivas por segundo: despacho e escalonamento também consomem tempo.

O relógio guest, as constantes de simulação e as esperas de vídeo não foram
alterados. O perfil anterior de callbacks a 30 Hz identificou métodos SIM/RENDER
do `AptAuxCore`, não comprovou uma trava da física. As funções `825F7780` e
`825F7828` são cronômetros, sem espera de frame. Não substituir as constantes
1/30 de animação/retícula por coincidência numérica.

## Skill REA e limites da evidência

A skill `reverse-engineer-anything` foi consultada, com os guias de artefatos
nativos e evidência. A inspeção do Canary foi feita sobre o código fonte
completo, rota para a qual a própria skill orienta usar ferramentas normais
de repositório. Na primeira etapa, o diagnóstico do REA detectou ausência dos provedores nativos,
registro MCP ausente e Node fora das versões suportadas. Nenhum provedor foi
instalado e nenhuma configuração de cliente foi alterada. Não houve
decompilação pelo REA nem Evidence IDs dessa ferramenta; não se atribuem a ela
as observações daquela etapa. Log: `logs/cpu_unlock_20261027_rea_doctor.log`.
O estado mudou na continuação abaixo: REA 6.8.0 conectado, Ghidra 12.1.4
disponível e observações de execução pelo Frida. Não foi necessário instalar
ou reconfigurar ferramentas nesta continuação.

## Verificação

- Teste de política primeiro falhou com três verificações do comportamento
  antigo: modo ilimitado não selecionado e VSync não escolhendo 60 Hz.
- Teste do launcher primeiro falhou porque o padrão ainda enviava `vsync=true`.
- Depois do port: 495 testes nativos sem falhas, CTest 3/3 e testes do launcher
  sem falhas. Valores explícitos 30/60/120 e VSync ligado continuam selecionáveis.
- Build Release do jogo e publicação local do launcher passaram.

## Como os desbloqueios funcionam em outros jogos

A pesquisa nos patches oficiais mostrou uma segunda camada além da configuração:
alguns patches substituem instruções por NOP e mudam a escolha de intervalo
do próprio jogo. Exemplo: o patch oficial de
[007: Quantum of Solace](https://github.com/xenia-canary/game-patches/blob/main/patches/415607FF%20-%20007%20Quantum%20of%20Solace.patch.toml)
troca instruções e documenta valores próprios para 30/60/sem limite. O de
[BioShock Infinite](https://github.com/xenia-canary/game-patches/blob/main/patches/5454085D%20-%20Bioshock%20Infinite.patch.toml)
também altera instrução e byte de configuração do jogo. Esses endereços e
hashes pertencem aos respectivos executáveis; não são aplicáveis ao Superman.
O [guia oficial](https://github.com/xenia-canary/game-patches/blob/main/.github/README.md)
exige portar o patch quando o hash do módulo difere.

## Resultado da candidata

`canary_unlock_20261027`, D3D12/RTX 2060, 1280×720, escala 1, MSAA 1,
sem geração de quadros, sem perfis CPU/relógio e sem timeline. O comando passa
limite zero e `vsync=false`; o log confirma:

```text
Canary pacing target=0 vsync=false unlocked=true vblank_hz=1000 scanline_counter=advancing
```

| Janela | Média de guest FPS | Menor amostra |
| --- | ---: | ---: |
| Parado na cidade | 25,8 | 17,6 |
| Movimento | 23,6 | 19,7 |

As janelas são curtas e a câmera não é determinística. As medições históricas
26–30 FPS usam outros enquadramentos e algumas incluem instrumentação; não
permitem atribuir ganho ou regressão a este port. As capturas mostram personagem,
cidade e HUD. Não há validação de simulação a 60 FPS nem equivalência visual
contra um Xbox. O mecanismo do Canary está ativo, mas a rodada não comprovou
FPS sustentado acima de 30 nem remoção de todo possível limitador interno.

A revisão também corrigiu o repasse da opção de VSync para
`NativeProviderConfig` no Vulkan; antes permanecia `true` mesmo com a opção
desligada. A política Vulkan de FIFO/Mailbox passou nos testes. O resultado
de gameplay acima é D3D12; não é uma medição de desempenho Vulkan.

VSync recebe o ciclo `RequiresRestart` ao configurar o backend nativo, pois
tanto a cadência quanto a swapchain Vulkan leem a opção na inicialização.
O usuário precisa reiniciar para aplicar a mudança; Xenos mantém seu ciclo
anterior. Isso evita anunciar uma alteração instantânea que deixaria um dos
dois componentes com o valor antigo.

Verificação final: CTest nativo 3/3 (495 casos), Vulkan 1/1 (108 casos),
runtime de rastreamento 4/4, testes do launcher incluindo persistência de VSync,
Python 169 testes e 4 subtestes, build Release e `git diff --check` passaram.
Os logs e capturas usam `logs/canary_unlock*` e `logs/bench_canary_unlock*`.

## Verificação específica do intervalo do dispositivo

A análise do código gerado identificou o setter `820F1D10`, que grava
`device+0x33A4`, e o consumidor pré-Swap `82111EC8`. O valor `0x80000000`
pula a montagem dos comandos de sincronização de apresentação. Isso é um
candidato específico, não prova de uma trava ativa em 30 FPS.

A rodada `present_interval_probe_20261027` foi rejeitada pelo detector de HUD
após 200 s. Sua captura mostra a cidade com HUD oculto; não se aceitam FPS
nem se apresenta essa rodada como teste de gameplay aprovado. O gatilho que
dependia da captura de HUD não disparou.

Foi então executado um probe separado, explicitamente de inicialização/título,
com limite de 128 amostras por rotina, sem alterar parâmetros ou resultados.
Resultado em `logs/present_interval_probe_analysis.json`:

- 128 chamadas pré-Swap: intervalo sempre `1`, gate ativo em 127 delas;
  média 0,001568 ms, máximo 0,0218 ms.
- 128 esperas de fence do Swap: média 0,000480 ms, máximo 0,0026 ms.
- O setter inicial recebeu `0`, e o consumidor posteriormente observou `1`.
  Isso não prova que todas as escritas do campo passam pelo setter.

Não foi observada espera de 33 ms nessas rotinas no trecho medido. Não se
aplicou NOP nem override do campo por falta de evidência de que isso removeria
o limite pretendido. As esperas de conclusão da GPU foram preservadas. A busca
não cobre todos os controladores externos, estados ou fases do jogo.

## Continuação REA: scheduler interno de renderização

O desbloqueio de vblank não removia o período mínimo do scheduler do próprio
jogo. A origem foi identificada no código gerado:

- `82177F90` lê o byte `+1` das tabelas `82911888/8291188C/82911890`.
- `82166178 → 827209D8 → 82720418` constrói o scheduler de renderização,
  em modo `1`, calculando aproximadamente `1e9 / rate` e gravando `object+72`.
- `82720B50` e `827206B8` só liberam callbacks quando tempo acumulado e
  crédito atingem esse período. O fallback usa yield, não Sleep(33).
- No modo `1`, `82720740` grava o tempo realmente decorrido em `+120` e
  sua conversão para segundos em `+132`. O período mínimo não é o delta
  fixo da simulação nesse modo. A classe SIM é outra, com modo `0`.

Fontes: `port/generated/default/superman_returns_recomp.117.cpp:2036`,
`.27.cpp:2129`, `.108.cpp:25053`, `.126.cpp:25252`, `.8.cpp:25862`,
`.35.cpp:26296`. O callback render `82166210` alcança o grafo render;
o callback SIM distinto é `82166328`. A exploração não prova ausência de
efeitos indiretos de todos os callbacks em todas as fases do jogo.

### Observação em execução

Foi recuperada a tabela `PPCFuncMappings` do PE, no offset `0x327B800`.
Todas as 33.445 entradas coincidem com a ordem e os endereços guest do código
gerado; os ponteiros host pertencem a seções executáveis. Isso permitiu
observar uma única entrada de Swap para obter o argumento `base` e retirar
imediatamente esse hook. As leituras seguintes não interceptaram APIs do Windows.

O REA observou 55 snapshots do objeto render `05AE7830`: vtable `82005B98`,
modo `1`, rate `30`, período `33333334 ns`. As três tabelas continham
`60,30,30,60`. O campo `+120` e o delta `+132` variavam com o tempo decorrido.
O campo `+168`, inicialmente nomeado `interpolation_168` no script, é uma
estatística de frequência medida; esse nome do probe estava incorreto.

Evidência de execução: `ev_4481d1999194bfc699e776980a13c60e1b51819ff65a6c4269c40215f85fe954`,
bundle completo `logs/rea_scheduler_20261027_evidence.json`.
O provider Frida não fornece identidade criptográfica automática do artefato.
A associação usa PID, caminho do módulo observado e hash do arquivo local:
`E72732AEC592EA4F26865956078067260B80996807FD319033D1365B9999371F`
(executável anterior ao novo hook). Não houve decompilação Ghidra nesta etapa.

Uma captura anterior interceptando cinco APIs NtWait/NtDelay interferiu na
execução e foi **rejeitada**. Seus tempos não estabelecem um gargalo do jogo.
O script foi descarregado e retirado de `tools/`; a sessão foi fechada e o
processo de diagnóstico foi encerrado. Registro preservado em
`logs/rea_waits_20261027_evidence.json` e `rea_waits_20261027_game.log`.

A observação posterior sem hooks consultou GetThreadTimes: em 3,001 s,
a thread dos quadros usou 2,96875 s de CPU e o worker 1,890625 s
(0,989 e 0,630 núcleo). Evidência
`ev_7b5fd02c8c4bac7faa53220f0d2d33055f7874edc4eea3a6486b8fbe67cb3393`,
bundle `logs/rea_cpu_20261027_evidence.json`. Nas janelas de gameplay,
a consulta externa de tempos confirmou 0,955/0,952 núcleo na thread dos
quadros e 0,646/0,585 no worker. CPU ocupada não distingue processamento
útil de polling. Os 25,3/23,8 FPS dessa rodada são anteriores ao novo hook,
não constituem comparação A/B.

Amostragem externa limitada da thread dos quadros: 391 contextos, identidade
do processo conferida, zero falhas de ResumeThread. Não houve PC atribuído
às rotinas do scheduler nessa janela; 207 PCs do executável permaneceram
sem atribuição a uma entrada guest. A identificação usa o intervalo exato
de unwind do PE, não o nome mais próximo. É uma amostra intrusiva e parcial,
sem base para prometer um ganho de FPS. Arquivos `logs/rea_scheduler_20261027_pc*`.

### Correção do período de renderização

`guest_render_scheduler.cpp` executa o construtor original uma vez e só
altera `object+72` antes de o chamador publicar o novo objeto. Exige o
tipo, modo, rate e período originais confirmados. Falha de leitura ou
layout diferente preserva o comportamento original. Só atua no backend nativo.

- FPS `0`: piso de `1 ms`, alinhado ao vblank ilimitado nominal de 1000 Hz
  e ao maior alvo de FPS suportado. Não há mais o gate de 30 Hz.
- FPS `30`: preserva o período original, sem escrita.
- FPS `60/120`: `ceil(1e9/FPS)`, respeitando os limites de configuração.
- `sr_native_guest_render_scheduler=false`: preserva esse scheduler original,
  permitindo comparação com o mesmo executável e a mesma política de vblank.

Preservam-se os outros bytes do objeto, as tabelas de configuração, os peers
SIM/core, os relógios, as esperas de GPU e a atualização normal de `+120/+132`.
Essa preservação é da escrita direta e do algoritmo: o novo período também
altera indiretamente a semente de tempo no início/reinício e a subtração/limite
do crédito `+112`. Não se afirma que todo delta produzido permaneça idêntico.
Isso remove um gate de renderização confirmado; não muda o tick fixo do peer
SIM nem demonstra sozinho simulação correta em todos os estados acima de 30 FPS.

Os testes primeiro falharam em seis verificações com a política antiga.
Após a correção, CTest nativo 3/3 passou (498 casos): limites explícitos,
preservação dos demais bytes e rejeição de classes/modos/períodos desconhecidos.
O helper externo `tools/analysis/sample_thread_pc.ps1` também passou a
checagem sintática e um exercício de 1 s com processo descartável separado:
33 contextos, zero falhas de retomada. Ele não é usado em benchmarks limpos.

A tentativa `rea_scheduler_title_ab` foi rejeitada como comparação: contar
11 amostras não garantiu tela de título estável, e houve longas interrupções
durante a inicialização em ambas as rodadas. As médias desse JSON não são
ganho nem regressão. Com o gate alterado, o contador de Swap chegou a
179,2 / 207,8 / 179,7 FPS durante a inicialização; isso demonstra que esse
contador ultrapassa 30, mas não mede imagens únicas ou gameplay a 60 FPS.

### Validação dos consumidores e escolha do piso

Com o protótipo de 1 ns, foram observadas oito chamadas do consumidor
`82720740`. Em todas elas, `delta_ns` resultou de
`accumulated_before + last_after - last_before`; a conversão para segundos
conferiu com tolerância de `1e-7`, o acumulador voltou a zero e o crédito
seguiu a subtração/limite do código original. Deltas observados: 92, 24, 25,
20, 24, 19, 18 e 15 ms. Isso não cobre a primeira chamada de startup nem
todos os caminhos de reinício. Evidência
`ev_3896ccb2ba172e9870e01e71a9b41722d89c185552da2696d89476243d493ed9`,
bundle `logs/rea_scheduler_fixed_20261027_evidence.json`.

A rodada de gameplay do protótipo mediu 12,0/14,2 FPS, com câmera/estado de
tutorial diferentes. Ela não demonstrou ganho; motivou uma alternância no
mesmo processo antes de manter a mudança. O script do benchmark pode deixar
o jogo pausado ao insistir nos seis comandos de skip, e o HUD some durante
idle. Na rodada seguinte, a pausa foi observada e retomada explicitamente.
O movimento sintético de 30 s já havia expirado antes das alternâncias.

Foram alternados cinco períodos na thread de Swap, sem reiniciar, com
5 s por etapa e o primeiro segundo descartado:

| Período / etapa | Entradas de Swap por segundo |
| --- | ---: |
| Original 33,333334 ms, A | 16,52 |
| Protótipo 1 ns | 15,66 |
| Piso 1 ms, A | 18,33 |
| Original 33,333334 ms, B | 18,30 |
| Piso 1 ms, B | 18,50 |

O campo foi restaurado ao terminar e a sessão fechada. Evidência
`ev_52870fcaa8e6c68ef3eaff017b9fc045317365b1f23775e12ec868a400f8c211`,
bundle `logs/rea_period_ab_20261027_evidence.json`. São janelas curtas,
instrumentadas, com animação/idle de câmera; não são um benchmark limpo nem
prova estatística de ganho. A captura mostra Superman junto a uma parede,
diferente das antigas vistas abertas da cidade. As médias agregadas do
wrapper de benchmark também não são aceitas: o teste final de HUD falhou.
O mínimo de delta serializado como `null` nesse probe não é usado como
evidência de correção temporal; a validação de deltas vem do consumidor acima.

A política final usa 1 ms, em vez do protótipo de 1 ns. É uma escolha
conservadora de agendamento, coerente com o alvo máximo e o vblank nativo;
não se atribui a ela um ganho comprovado de CPU/FPS. A regressão do piso
falhou primeiro em uma verificação, antes da alteração da política.
