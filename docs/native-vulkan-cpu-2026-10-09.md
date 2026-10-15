# CPU do renderer nativo Vulkan — 2026-10-09

Esta frente corrige trabalho redundante na captura da CPU. Não altera resolução,
shaders, ordem dos draws, precisão, política de hash/backoff ou sincronização GPU.
Os resultados locais ainda não demonstram eliminação completa do gargalo nem
30 FPS estáveis.

## Continuação da meta de 30 FPS

Depois de fechar um diálogo do Firewall que invalidava os comandos do benchmark,
as medições válidas de gameplay foram:

| Executável / experimento | Parado | Andando |
| --- | ---: | ---: |
| Baseline salvo, `cpu30_before_paired1` | 14,5 FPS | 14,5 FPS |
| Arenas e captura congelada, `cpu30_reviewed1` | 12,4 FPS | 12,3 FPS |
| Preferência por memória com cache, `cpu30_cached1` | 13,6 FPS | 12,5 FPS |
| ThinLTO, `cpu30_thin_lto1` | 13,6 FPS | 13,7 FPS |
| ThinLTO + yield + cópia XDK, `cpu30_thin_combined1` | 12,6 FPS | 13,4 FPS |
| ThinLTO sem profiling/timeline, `cpu30_thin_clean1` | 14,2 FPS | 13,9 FPS |
| ThinLTO + PM4 em bloco, sem profiling, `cpu30_pm4_clean1` | 13,2 FPS | 13,3 FPS |
| ThinLTO + PM4 + overlays idênticos, `cpu30_overlay_clean1` | 15,2 FPS | 14,6 FPS |
| Cache exato VS/PS ligado, `cpu30_bank_clean1` | 11,2 FPS | 11,6 FPS |
| Mesmo executável, cache desligado, `cpu30_bank_off_clean1` | 12,4 FPS | 11,8 FPS |

As cenas produzem contagens de draws diferentes. A candidata reduz replay por
pacote e praticamente elimina a cópia na entrega ao sink, mas esses pares não
demonstram ganho de FPS. O gate da imagem inicial entre baseline e candidata
passou (39,59 dB, histograma 0,00631). A meta continua pendente.

A preferência por memória com cache foi removida: em duas comparações isoladas,
o `BuildBindings` custou 1,079/0,981 us por draw com memória normal e
1,693/1,409 us com cache. O preenchimento contínuo dos metadados vazios também
foi removido: com 3.000 payloads distintos, custou 2,354 us contra 2,291 us do
caminho normal. O diagnóstico pode ser reproduzido com
`build/tests-vulkan/sr_vulkan_resources_test.exe --bench-bindings`.

A opção experimental `SR_THIN_LTO=ON` aplica ThinLTO apenas a builds Release
Windows/Clang, incluindo código recompilado e módulos nativos/Vulkan. Continua
desligada por padrão; não altera ISA nem política de ponto flutuante.
Os 166 testes nativos e 8.224 comparações de contexto PPC/memória XDK passaram
compilados com ThinLTO. O build completo passou e o gate inicial passou
(35,14 dB, histograma 0,00654), mas as medições acima não demonstram ganho
de FPS. Os flags de yield e cópia XDK continuam desligados por padrão.

O usuário confirmou notebook na tomada e modo desempenho. A execução sem
instrumentação continuou perto de 14 FPS; isso não sustenta atribuir a diferença
até 30 FPS ao profiling nem à alimentação por bateria.

### Captura de pilhas e próximo ajuste

`cpu30_stack1` forneceu 7.424 pilhas, 116 por thread, em 12 segundos de gameplay
com HUD válido. É uma amostragem externa e intrusiva com `StackWalk64`; os FPS
dessa execução não são um benchmark limpo. O helper retoma cada thread após
a leitura e limita a profundidade a 64 frames. Nomes privados próximos no MAP
e offsets de DLLs são aproximações, não atribuição exata de função.

A thread do jogo apareceu em captura/PM4 e processamento gráfico do guest;
replay também apareceu no parser PM4, e o recorder em `Draw`/`BuildBindings`.
O CRT confirmou instruções de `memset` em várias amostras do worker. A alternativa
de inicializar a arena por draw melhorou apenas ~0,3 us/draw no protótipo e não
foi incorporada.

O ajuste em avaliação agrupa escritas sequenciais dos bancos ALU: preserva
registradores brutos, marcas `written` e o número exato de incrementos de versão.
O teste compara bancos completos e limites com escritas individuais, incluindo
NaN/infinito e valores repetidos. O diagnóstico isolado do parser caiu de
2,00 para 0,71 ns/palavra; razão contra conversão direta 2,85 → 1,02, orçamento
≤ 2. Os 168 testes nativos passaram no ajuste final, incluindo com ThinLTO;
88 testes Vulkan e três contratos de pixels passaram. O build completo passou
(SHA-256 `D1477DF5514CA57650E4A5C81D0C7F4CDF136882CB0C700C7F425A06F275761E`).
O gate inicial passou (40,37 dB, histograma 0,00444), mas 13,2/13,3 FPS no jogo
não demonstra ganho de FPS nem cumprimento da meta. Com as mesmas instruções
SSE4.1 do módulo nativo, o orçamento isolado mede 0,255 ns/palavra e razão 1,09.
Reprodução do orçamento: `build/tests-native/sr_pm4_budget.exe` em Release.

`FindUnambiguousCapture` agora prova a igualdade de bytes de cada interseção
parcial mais nova antes de emprestar um banco completo. Preserva a ordem e os
bytes das capturas. Sobreposições diferentes/inacessíveis ainda levam ao caminho
escalar. A regressão falhou primeiro (0 vs 768 bytes), depois os 169 testes nativos,
88 Vulkan e três contratos de pixels passaram. Cobertura inclui múltiplos overlays,
captura completa mais nova, interseção recortada, offsets inválidos e overflow.
Review sem problemas concretos. O orçamento de replay congelado segue sem
alocações. No diagnóstico com 12 ranges irrelevantes e um sufixo idêntico, leitura
do banco: 0,3–0,5 us em bloco contra 2,5–3,1 us forçando o caminho escalar, com
checksums iguais. Esses tempos não substituem a medição do jogo.

O build da candidata passou, SHA-256
`72686519AFD69D702DCC853B5B2DB87B0144DEC4A4FE15B8E33B177607596336`;
gate inicial passou (37,96 dB, histograma 0,00948). A medição 15,2/14,6 FPS ainda
não cumpre a meta, e precisa de repetição antes de atribuir uma melhoria estável.
Durante gameplay, contadores Windows registraram frequência nominal 2592 MHz,
ProcessorPerformance 99–100 e uso 76–81%. Isso sozinho não identifica causa de
limitação de energia/temperatura e nenhuma configuração de energia foi alterada.

O cache ThinLTO fica no diretório de build e somente na opção Release experimental;
reduz o custo dos próximos links sem alterar flags de instrução/ponto flutuante.
O próximo diagnóstico conta versões VS/PS iguais entre draws com profiling ligado,
para avaliar reutilização exata dos bancos antes de mudar uploads/descritores.

## Máquina e reprodução

Intel Core i7-10750H, NVIDIA RTX 2060. Esta é uma máquina diferente da Intel UHD
dos perfis de 08/10; aqueles números não são uma referência comparável aqui.
Executável Release, janela 1280x720, escala nativa 1, MSAA 1, FXAA desligado,
VSync ligado, limite de 30 FPS. Cada janela parado/andando dura 20 segundos.
Sem builds durante as janelas medidas.

Comando usado (o padrão do wrapper aponta para uma pasta `win-amd64-dist`
inexistente neste checkout, por isso o executável é explícito):

```powershell
tools/bench/bench_api.ps1 -Api vulkan -Name cpu_fix_final2 `
  -Exe "$PWD/port/out/build/win-amd64-release/superman_returns.exe" `
  -Profile -Timeline -TimelineDetail
```

SHA-256 anterior: `12a967ac46772b423bf9ab27332fe3814b4dfd9130c53f865e310c8fdafdcb27`.
SHA-256 final: `1bbf70f43af2c77921684012f7a7e108914acc346fe2009a45bc1b1f4a525ae5`.
O anterior fica localmente em `port/out/build/win-amd64-release/cpu_fix_before.exe`.

## Mudanças e evidência isolada

- **PM4:** o ring primário é lido diretamente da arena, que fica estável até
  terminar o scan. Dependências continuam sendo congeladas no momento da leitura,
  com spans válidos durante indiretos aninhados; só depois são anexadas à arena na
  mesma ordem. Falhas preservam o rollback. Evita a cópia do ring em cada comando.
  No teste de um ring de 4 KiB sem dependências: 4.151 → 64 bytes alocados.
  O crescimento da arena continua amortizado; o teste de 32 comandos com
  dependências mede 5.221 bytes, contra 12.160 com reservas de tamanho exato.
- **Leituras seguras:** `CheckedGuestReads` reutiliza buffers após `Reset`,
  mantendo `ReadProcessMemory` e um slot independente por leitura bem-sucedida.
  Falhas não sobrescrevem spans válidos. O pool retém no máximo 32 slots de
  64 KiB (2 MiB por instância); buffers grandes são liberados. O teste aquecido
  de 100 comandos, 32 leituras de 128 bytes por comando, caiu de 582.400 para
  zero bytes de alocação.
- **Busca de textura:** cada slot guarda a última chave completa de seis words
  e o ponteiro para a entrada da `std::map`. Isso reutiliza somente a busca,
  sem pular validação de escrita/conteúdo. Inserções preservam os nós; a poda
  invalida os ponteiros antes de apagar entradas. O frontend não pode ser
  copiado/movido. Microbenchmark com 1.500 entradas e chaves mudando a cada
  32 draws: mapa 2,32–2,48 µs/draw, slot 0,27 µs/draw, checksum idêntico.
  `SR_NATIVE_TEXTURE_SLOT_CACHE=0` permite A/B no mesmo binário.

A hipótese de montar constantes em RAM e copiar para o upload foi descartada:
com 3.000 pacotes/alocações Vulkan distintos, o teste mediu 1,86–2,06 µs/draw
direto contra 2,09–2,19 via RAM. Nenhuma mudança desse experimento foi retida.

## Gameplay

| Execução | Parado | Andando | Estado |
| --- | ---: | ---: | --- |
| `cpu_fix_baseline` | 11,8 | 12,6 | Executável anterior |
| `cpu_fix_pm4` | 14,1 | 13,6 | Somente PM4 |
| `cpu_fix_final1` | 14,5 | 13,7 | PM4 + leituras reutilizadas |
| `cpu_fix_cache1` | 12,0 | 12,9 | Cache por slot; antes do ajuste da reserva |
| `cpu_fix_slot_off` | 9,8 | 13,5 | Binário final, cache por slot desligado |
| `cpu_fix_final2` | 13,5 | 14,0 | Binário final, três mudanças ligadas |

Sanidade D3D12 no mesmo binário (`cpu_fix_d3d12`): gameplay/HUD e movimento
concluídos, 14,8 FPS parado e 15,3 andando. Não houve A/B D3D12 nesta frente,
portanto esse teste não estabelece ganho nem ausência de regressão de FPS.
O gate cruzado do screenshot D3D12 contra a referência Vulkan não passou
(28,73 dB; histograma 0,2023). Essa comparação entre APIs/cenas não substitui
uma referência D3D12 anterior; não foi estabelecida paridade visual D3D12 por A/B.

Os logs, timelines e screenshots ficam em `logs/` (ignorados pelo Git), com
esses nomes. A câmera, trânsito e carga variam. Não atribuir a diferença bruta
de FPS inteira às mudanças nem comparar cenas mais leves com mais pesadas.

Nos últimos 300 quadros de gameplay, antes e em `cpu_fix_final1`, `fp_primary`
cai de 1,41 para 0,07 ms/quadro, e `fp_read` de 1,44 para 1,11 ms/quadro.
Esses tempos são aninhados; não podem ser somados para prever o FPS.
A GPU mede 11,31 e 10,22 ms/quadro, enquanto o frontend mede 39,82 e 34,06 ms,
o worker 57,09 e 54,65 ms, e a gravação 56,46 e 51,41 ms. Há trabalho de CPU
restante nesses estágios. `game_guest` é o residual da timeline, não um perfil
que prove qual função da lógica do jogo ou qual espera é responsável por ele.

No binário final, últimos 300 quadros, cache por slot desligado/ligado:
`fe_textures` 11,64/7,53 ms, frontend 35,94/31,00 ms, PM4 7,17/6,70 ms,
worker 56,31/57,44 ms, gravação 53,13/51,99 ms, GPU 10,95/10,16 ms.
O custo de textura cai enquanto o worker permanece na mesma ordem de grandeza;
isso confirma que ainda há um gargalo além das buscas e cópias removidas.
As execuções não reproduzem uma cena determinística; nenhuma porcentagem
geral de ganho é estabelecida por esse par.

## Verificação

Build Release concluída. 158 testes nativos e o orçamento de alocação passam;
88 testes Vulkan passam. O contrato GPU de produção e os 16 fixtures de recursos,
incluindo `hold-lifetime`, passam. Validation layers estão ausentes nesta máquina;
esses resultados são dos checks/readbacks dos fixtures, não das layers.

Os gates de imagem contra `cpu_fix_baseline` passaram para `cpu_fix_final1`
(37,27 dB; histograma 0,00797) e `cpu_fix_cache1` (38,56 dB; 0,00346), incluindo
as regiões. A versão final `cpu_fix_final2` também passou (39,87 dB; 0,00644).
Isso cobre as cenas registradas, não todo o jogo nem igualdade
pixel a pixel de duas execuções com cenas diferentes.

Revisão independente conferiu spans recursivos, ordenação, rollback, falhas de
leitura e lifetime dos ponteiros. Os dois achados (reservas exatas e cópia/move
do frontend) foram corrigidos e receberam regressões que falharam antes da correção.

### Cache experimental dos bancos de constantes

A contagem de versões no replay encontrou cerca de 30% de repetição VS e 60% PS.
`cpu30_bank_profile1` mediu 14,8/12,1 FPS; nos últimos 300 quadros, game 76,53 ms,
guest 42,81 ms e frontend 33,72 ms. Esse perfil ainda excede o orçamento de 33,33 ms.

`SR_VULKAN_CONSTANT_BANK_CACHE=1` testa reutilização com comparação exata de
4096 bytes, restrita ao mesmo chunk e submissão. Reinicia em BeginSubmission,
retém reservas de 12 KB e reconstrói shared; apenas os offsets VS/PS podem apontar
para slices anteriores imutáveis. Fica desligado por padrão. Os 90 testes Vulkan,
três readbacks do contrato shader e fixtures GameRenderer/merged passaram.
O contrato restaura separadamente depth/color consumidos pelo shader antes de
reusar ambos os bancos. Validation layers indisponíveis. Revisão não encontrou
problemas concretos de offsets/lifetime; a cobertura de misses consumidos foi
acrescentada após o apontamento do revisor.

No diagnóstico de 3000 draws, com 30% VS/60% PS repetidos, o caminho normal custou
3,348/3,416 us por draw, versus 3,132/3,207 us com cache: economia pequena,
~0,6 ms por 3000 draws. Executável ECF4B8D033309527816F5058162C3C8556C6FE7800136D0683DF6A026B1B856A.
Gameplay: 11,2/11,6 FPS ligado e 12,4/11,8 FPS desligado no mesmo executável.
Não há ganho de FPS demonstrado; o cache permanece desligado por padrão.
Gate inicial passou (26,57 dB, histograma 0,01428), com variação de cena.
Nenhuma alegação de 30 FPS.

O diagnóstico `SR_NATIVE_GUEST_CPU_PROFILE=1` cronometra funções
selecionadas pela amostra de pilhas: 823055A8, 82783948, 82305B88, 82316958,
82319108, 826092B8 e 821078C0. Os originais são sempre executados. Contadores
por thread reportam tempo inclusivo e tempo descontando apenas filhos também
rastreados. Este último não é tempo exclusivo de instruções guest: funções
não rastreadas podem estar incluídas. A versão atual também mede sete callbacks
filhos e desconta os escopos externos de captura nativa, sem contar capturas
aninhadas duas vezes. Os tempos continuam incluindo esperas e desagendamento.
Desligado por padrão.

### Experimento de localização de CTR — 2026-10-10

O relatório fornecido pelo usuário recomenda reduzir tráfego de PPCContext.
Existe um registro anterior em `performance-design.md`: ativar as cinco opções
de localização juntas impediu a geração de quadros. Portanto, testar cada opção
isoladamente antes de combinar opções; manter LR e não voláteis no contexto.

Referência limpa `cpu30_ctr_baseline`: 11,6 FPS parado, 11,1 FPS em movimento,
executável SHA256 062847A178CDF5C9C35A6129CA7FE40BC6EBEC77B4646E587D6F1663E2AE83BB.
O benchmark concluiu; o gate precisou ser executado com Windows PowerShell 5,
pois PowerShell 7 não compilou o helper System.Drawing. Referência registrada.

Primeiro caso: somente `ctr_as_local=true`. Os 33.229 corpos de funções gerados
coincidem com a referência ao desfazer exclusivamente a declaração local e
os acessos a CTR (143 unidades alteradas). Dois loops reais, de limpeza e cópia,
passaram 8.192 comparações com memória e contextos iniciais variados, cobrindo
32 alinhamentos: memória completa e contexto completo, exceto o CTR volátil,
coincidiram. Esse teste não prova equivalência de todas as funções do jogo.
Revisão não encontrou contratos de hooks ou despacho indireto incompatíveis.
Build passou. Gameplay localizado `cpu30_ctr_local`: 12,7/12,8 FPS; gate passou
(35,3 dB, histograma 0,010 e todas as regiões). SHA256 do caso:
75BA180812A73DE92F441B29663884C7EF46F5BC5F8703693622A8F703CAB316.
Repetições limpas: referência 12,3/11,8 e 11,7/11,2 FPS; CTR localizado
13,2/12,8 FPS. As três referências passaram no gate. O segundo caso CTR falhou
apenas na cromaticidade da região do personagem (0,053 > 0,05), apesar de
37,1 dB globais e histograma 0,006. A inspeção mostrou uma dobra diferente da
capa, com a face interna vermelha visível. A comparação entre as duas capturas
do mesmo executável CTR também falhou nessa região (0,076), mostrando variação
entre execuções, mas não estabelecendo aprovação visual completa do candidato.
Não relaxar os critérios nem declarar todos os gates aprovados. Os 30 FPS
continuam não atingidos; CTR voltou a false no experimento seguinte.
Backups e evidências: `logs/cpu30_registers/` (arquivos locais ignorados pelo Git).

Bloqueador confirmado para `non_argument_as_local=true` isoladamente: o gerador
localiza r12 tanto no chamador quanto nos helpers, sem passar o valor entre as
cópias locais. `__savegprlr_29` salvaria zero como LR; `__savevmx_110`, chamado
por `sub_828AADF0` com r12=r1-48, calcularia o primeiro endereço a partir de r12
zero (0xFFFFFEE0), acessando a região errada. O gerador só elimina helpers
`__save`/`__rest` quando `non_volatile_as_local=true`, opção que também precisa
de validação de setjmp/longjmp e SEH. Portanto, não habilitar non_argument
isoladamente neste SDK. Isso identifica uma incompatibilidade concreta; não
prova que ela foi a única causa da ausência de quadros no experimento antigo.

Segundo caso: somente `cr_as_local=true`, CTR e demais flags false. A revisão
direcionada não encontrou um contrato implícito concreto equivalente ao r12,
mas não prova segurança de todos os funclets/rotinas. Os 33.229 corpos gerados
coincidem ao desfazer somente declarações/acessos CR locais. Duas rotinas reais
de clamp e limpeza de bit passaram 32.768 comparações, cobrindo valores de
entrada, 32 alinhamentos e flags XER/CR iniciais variadas. Memória completa,
registradores gerais e CR2–4 não voláteis coincidiram; CR0/1/5/6/7 voláteis
foram excluídos da comparação por serem intencionalmente não materializados
na saída. Build completo passou. Gameplay limpo `cpu30_cr_local`: 11,7 FPS
parado e 11,5 FPS em movimento, sem ganho útil sobre as referências.
Gate passou (33,1 dB, histograma 0,024 e todas as regiões). SHA256:
2B45BA06CB01A100E20108FF96453FD195B23C918857499DB6125814147AF104.
CR local não é selecionado como otimização de desempenho. Os 30 FPS continuam
não atingidos.

### Diagnóstico de captura e CPU por thread — 2026-10-10

`cpu30_capture_cpu1`, executável CR local acima, profiler de funções/captura e
timeline habilitados: 13,1/12,1 FPS, resultado instrumentado excluído de
aceitação de desempenho. A captura nativa externa representou 44,43% do tempo
decorrido da thread principal em 23 janelas de aproximadamente dois segundos.
Após descontar captura/filhos rastreados, 822F5290 ficou em 10,72%, 8230F8E0
em 7,99% e 82783948 em 7,41%. Estes valores ainda incluem descendentes não
rastreados e esperas; não são tempo exclusivo de instruções PPC.

O diagnóstico separado `tools/analysis/sample_thread_cpu.ps1` lê o tempo de
CPU acumulado do Windows sem suspender threads. Identifica a criação da thread
para não atribuir deltas a IDs reutilizados; limite de 512 threads. Smoke do
script corrigido produziu 24 intervalos. No gameplay entre 00:51:20 e 00:52:05,
42 intervalos completos totalizaram 43,27 segundos: thread principal 24420
consumiu 42,078 segundos de CPU (0,972 core), proprietário Vulkan 16272
consumiu 38,547 segundos (0,891 core), replay 2076 consumiu 30,156 segundos
(0,697 core). Portanto, há trabalho real de CPU nas threads críticas; não há
evidência de que o gargalo principal seja apenas desagendamento. O próximo
alvo é o custo de captura nativa, juntamente com replay/gravação Vulkan.
CSV e log: `logs/cpu30_registers/capture_cpu_threads.csv` e
`logs/bench_cpu30_capture_cpu1.log`. Nenhuma alteração de qualidade ou limite
de quadros. CR/CTR locais não foram selecionados como padrão.

### Continuação em 2026-10-13

Cache TLS128entradas do cálculo puro TextureResourceId, comparação dos6words,
opt-in, foi testado e removido:91testes Vulkan0falhas;10000fetches aleatórios,
colisões/single-word changes; revisão sem problemas. Isolado.999/1.058us por
32slots/draw contra.311/.415us; checksums iguais. Porém no mesmo executável
EA316DB47807EDF49868258C1B64FFD5FE96138250F0BE71DC3B091BF7EF03EE:
OFF11.4/11.4FPS, ON11.0/10.9. Ambos gates passam26.4/.017 e26.6/.017.
Sem ganho demonstrado; código experimental e teste retirados. Caminho anterior
recompilado, SHA9A9ED5E346AED1FBFC7EAE1F38B660D80B3866C15899214C0D500648A70353F0.

Diagnóstico cpu30_oct13_cpu_split, instrumentado e excluído de aceitação:
12.3/10.8FPS; gate26.4dB/.018 passa. Sampler agora separa user_ms/kernel_ms
(smoke24intervalos). Em44.23s completos de gameplay, thread principal21192:
39.33s user,3.34s kernel,.965core; gravação Vulkan18908:35.69s user,2.89s
kernel,.872core; replay13132:28.86s user,1.33s kernel,.683core. Thread21036:
17.42s user,20.70s kernel,.862core. Não inferir seu trabalho pela linha de
inicialização do worker: ela identifica quem o criou, não seu papel em todas
as amostras. Vulkan reporta bindings16–20ms e descriptors12–18ms por quadro,
com2433–2707hits e141–267misses de descriptors; uploads de texturas quase zero.
A afinidade por CPU guest está ignorada por padrão no SDK; não há evidência de
pinning nos primeiros6CPUs. Plano Windows permite100% CPU em AC, sem alterar
configurações. Próxima verificação: backoff existente de yields repetidos,
opt-in e comparação limpa no mesmo executável.30FPS ainda não atingidos.

Resultado final desta rodada: nenhum ganho estável selecionado. Hash a cada uso
para buffers pequenos mediu 8,5/10,1 FPS, contra 12,6/12,4 desligado; permanece
OFF por padrão. Chamadas diretas ao original, apenas em 9.571 funções inteiras
com grafo fechado, sem hooks/FP/MMIO, passaram auditoria inversa de fontes,
5 testes do gerador e 32.768 comparações de helpers reais (contexto completo,
memória e MXCSR). Gameplay candidato: 12,8/13,0 e 14,5/13,8 FPS; referência:
14,0/13,4 e 12,5/13,4 FPS. Todos esses gates de imagem passaram. A rodada
baseline2 foi descartada por concorrência com a restauração de fontes, e foi
refeita sem esse trabalho. Não selecionar a mudança pelo melhor caso isolado.
Código gerado restaurado byte a byte, com timestamps atualizados para futuros
builds; executável normal restaurado ao SHA 2C608CD986A9ED634D76B02EAA6EE022A7AD90010060FF40A949E3A249653E6E.
A ferramenta experimental não foi ligada ao CMake. Os 30 FPS seguem pendentes.

### Inicialização da arena de replay (2026-10-13)

O diagnóstico `cpu30_replay_detail` separa captura congelada, parsing PM4,
decode e criação da arena, que antes ficava fora do contador replay. Em dois
intervalos de gameplay: captura 0,41/0,50 ms, PM4 2,99/3,54 ms, decode
24,21/29,55 ms, arena 24,31/29,92 ms. O tempo de arena inclui alocação e
inicialização; não atribuir todo esse intervalo exclusivamente aos stores.
O sync-only consumidor nativo não executa desenhos do backend de emulação.

No desenho de 13/10, a arena deixava sem inicialização VS, PS e mirrored_registers
(12 KiB por draw): DecodeCapturedPacket atribuía os três arrays integralmente
antes de usá-los/publicá-los. Os defaults de ConstantSnapshot/DrawPacket
continuam zerados para outros consumidores; shared/device/vertex/fetch e os
demais campos mantêm a inicialização anterior. A capacidade é reservada com o
vetor vazio antes de emplace, sem mover payloads parcialmente inicializados.
Erros precoces não publicam nem leem os arrays. Revisão read-only sem bugs;
172 testes nativos, 90 testes Vulkan e três variantes de pixels passaram,
validation_errors=0. O teste novo usa a arena real, bancos não zero/NaN e
falha precoce, comparando o resultado completo com o caminho inicializado.

Esse desenho histórico foi refinado em 14/10: o sufixo de `shared` passou a
ser preenchido apenas pelo decoder, e o bloco `mirrored_registers` foi
substituído pelos quatro parâmetros de polygon offset realmente consumidos.
As medições de 13/10 abaixo correspondem ao layout anterior.

Teste isolado de 3.008 draws/frame: arena + preenchimento 5,835/5,719 ms
antes, 4,440/4,195 ms depois; checksums iguais. Não usar esse teste para
prever FPS. Comparações sem instrumentação, mesmo executável:

| Rodada | Arena reduzida | FPS parado / movimento | Imagem |
| --- | --- | --- | --- |
| on1 | sim | 15,4 / 14,6 | passa |
| off1 | não | 12,5 / 12,6 | passa |
| on2 | sim | 14,8 / 13,8 | falha na cromaticidade do personagem |
| off2 | não | 12,2 / 12,9 | passa |
| on3 | sim | 13,7 / 13,9 | passa |

on2 apresenta câmera/capa diferentes da referência; isso não prova a causa da
falha. Critérios de imagem não foram afrouxados e a falha não foi omitida.
Diagnóstico ligado `cpu30_replay_storage_profile`: arena 15,46/15,10 ms,
decode 28,82/28,16 ms; gate passa. Este diagnóstico fica fora da aceitação
de FPS. O ganho observado é parcial: gravação Vulkan ainda 60–63 ms, com
bindings 17–18 ms e descriptors 14–15 ms; a meta de 33,33 ms permanece aberta.

SR_NATIVE_REPLAY_STORAGE=0 mantém o antigo caminho para A/B; o novo padrão
é ligado somente no storage usado pelo decoder Vulkan. Nenhuma alteração
de qualidade, ordenação, shaders, callbacks de escrita ou código gerado.
Logs de build e validação final: `cpu30_replay_storage_default_build.txt`
e `cpu30_replay_storage_default_run.txt`.

Validação final sem SR_NATIVE_REPLAY_STORAGE, sem instrumentação:
`cpu30_replay_storage_default` 15,1/14,6 FPS; imagem passa 26,4 dB,
histograma 0,015, personagem chroma 0,003. Build concluído; configuração
1280x720/scale1/MSAA1/vsync/limite30/qualidade original mantida.
O trabalho não está concluído: 30 FPS ainda não foram atingidos.

### Correção do cache de descritores (2026-10-14)

`UINT64_MAX` era simultaneamente o marcador de consulta não realizada e o ID
válido da textura dummy 2D. Isso impedia cachear draws repetidos nesse caso.
Flags explícitos de primeira consulta corrigem texturas e buffers, preservando
as políticas existentes. No diagnóstico de 3.000 draws dummy idênticos: 3.000
misses / 103–106 µs por draw passam a um miss / 4,7–4,9 µs. É um ganho desse caso
isolado; a série de gameplay não demonstra ganho estável de FPS. O atalho extra
de último binding foi testado e removido. Final 14,9/14,8 FPS, imagem aprovada,
91 testes Vulkan, 177 nativos, orçamento de alocações e contratos de pixels
passaram. Evidências e hashes em
[native-vulkan-descriptor-cpu-2026-10-14.md](native-vulkan-descriptor-cpu-2026-10-14.md).

### Inicialização do sufixo do replay (2026-10-14)

Storage de replay deixa de zerar 3,5 KiB de metadados que o decoder sobrescreve
por inteiro antes de publicar cada draw. Construtores comuns permanecem zerados.
No diagnóstico de 3.008 draws, arena mais decode passou de 39,1–42,2 ms para
38,0–38,9 ms, com checksum idêntico. Quatro rodadas limpas de gameplay não
demonstraram ganho estável de FPS. Passaram 178 testes nativos, 91 Vulkan,
orçamento de alocações, contratos de pixels e gates de imagem. Evidências e
limitações em
[native-vulkan-replay-shared-cpu-2026-10-14.md](native-vulkan-replay-shared-cpu-2026-10-14.md).

### Compactação dos registradores de polygon offset (2026-10-14)

O DrawPacket copiava 1.024 registradores PM4, mas só quatro tinham consumidor.
Guardar apenas os bits de `0x2380..0x2383` mantém o cálculo de polygon offset
e reduz o payload de 25.952 para 21.872 bytes, cerca de 11,7 MiB por batch de
3.008 draws. Três pares alternados do diagnóstico de replay completo mediram
39,0–42,4 ms antes e 37,2–37,5 ms depois, com checksum idêntico. Gameplay
não demonstrou ganho estável de FPS. O executável final foi validado com
181 testes nativos, 91 Vulkan, orçamento de alocações, pixels e imagens.
Evidências, hashes e limitações em
[native-vulkan-polygon-offset-cpu-2026-10-14.md](native-vulkan-polygon-offset-cpu-2026-10-14.md).

### Cache por versão VS/PS — descartado (2026-10-14)

O cache por identidade/versões, limitado a submissão e chunk, passou os testes
e reduziu o upload isolado em cerca de 2–7%. No perfil, bindings marcou
11,0–12,8 ms ligado e 15,9–16,0 ms desligado, com cargas diferentes. Nas quatro
rodadas limpas, ON ficou abaixo das respectivas referências em FPS. O ganho
local não sustentou manter o experimento; fontes e executável anteriores foram
restaurados, sem perder os ajustes aceitos. Restaurado 13,6/14,4 FPS, 181 testes
nativos, 91 Vulkan, alocações, pixels e imagem passaram. Evidências e snapshot
experimental em
[native-vulkan-versioned-banks-cpu-2026-10-14.md](native-vulkan-versioned-banks-cpu-2026-10-14.md).
