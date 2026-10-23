# Cache limitado de SRVs de buffer — 23/10/2026

Etapa CPU do desenho aprovado. O perfil anterior (`7c72647`, relatório `d3d12-stream-binding-profile-2026-10-23.md`) mediu 1.359 criações de SRV e 3,55 ms/quadro na região SRV. O renderer mantinha cada view somente na submissão corrente. Reter uma por BufferEntry sem limite seria inadequado: havia ~28 mil buffers vivos e o heap compartilhado tem 32.768 descritores.

## Experimento

`sr_native_buffer_srv_cache=0` é o padrão. Valores positivos habilitam um cache LRU limitado, com teto 8192 e reinício obrigatório. A comparação usa 0/4096. É D3D12; o parâmetro é ignorado no caminho Vulkan.

As leases são identificadas por uma geração única atribuída ao recurso host depois de CreateCommittedResource bem-sucedido. Reupload no mesmo recurso mantém a geração e sua view; substituir o recurso invalida a lease anterior e retira seu descritor usando a fence atual. Evicções também passam por RetireSrvIndex, protegendo draws e metadatas anteriores, inclusive quando a evicção ocorre no mesmo quadro. O índice antigo guardado em BufferEntry não prova posse da view: cada acesso On consulta a lease pela geração.

O helper nunca recicla índices por conta própria. Falha de alocação mantém o caminho de erro anterior. Uploads de vértices, formato/shape da view raw, barreiras, draws e IA permanecem com os contratos existentes. Off conserva a retirada por submissão. A inicialização publica a capacidade efetiva, e o perfil de streams publica hits entre submissões, evicções, invalidações e tamanho do cache. `srv_reused` inclui reusos dentro e entre submissões quando On.

## Verificação anterior à comparação

Seis novos testes: reuso por geração e invalidação, LRU, substituição de uma view sem perder a anterior, índices falhos/geração desconhecida, 10 mil gerações transitórias com limite 16 e composição com o recycler que impede reuso antes da fence. RED por header ausente; GREEN com **249 testes nativos, zero falhas**. CTest passou 2/2 (native + capture budget). A revisão independente verificou lifetimes, falha de criação, reupload, índices stale e pins/sort dos dumps; não apontou P1/P2. Não executou gameplay.

Build experimental passou; EXE SHA256 `4A430F3429DE0D088BACB72F82FE482ED089E62ED9B95CBAEAB3239CB4E9ED46`, runtime `BDF4B0E8D6F7421C74AFBA67460866952BA8E8E7ECEC8FC560EBF898FDC36DC1`. Logs `buffer_srv_cache_20261023_{red,native_build,native_green,build,ctest}.log`.

## Comparação

Mesma build, D3D12 1280×720, escala 1, MSAA 1, limite 60, VSync On, tracking On, shared cache Off, stacked resolves Off, perfis CPU/constantes/streams On e timeline sem detalhe. Manifestos comparados por script: hashes EXE/runtime e argumentos são iguais, salvo nomes dos arquivos e capacidade 0/4096. A camada de debug D3D12 não está disponível nesta instalação; isso continua uma limitação, sem alegar validação dela.

Referência Off `buffer_srv_20261023_off1`: New Game e chegada à cidade observados, mais 25 s de aquecimento; janela UTC 23:20:54,196–23:22:40,049, duração 105,85 s, **nove blocos completos e 1.092 quadros de timeline**. Criações de SRV 1.304,52/quadro, reusos 690,08, região SRV 4,196 ms/quadro. Cache live/hits/evicções/invalidações ficaram zero, como esperado. Sem falhas de SRV ou buffer ausente. Fonte `buffer_srv_20261023_off1_blocks.json`.

On `buffer_srv_20261023_on1`: cidade observada após New Game e aquecimento adicional de 25 s; janela UTC 23:27:06,841–23:29:06,680, **119,84 s**, 12 blocos completos e 1.483 quadros de timeline. Sem builds/testes ou trace ativo nas duas janelas.

| Medida diagnóstica | Off 0 | On 4096 |
| --- | ---: | ---: |
| Streams dinâmicos/quadro | 1.994,60 | 1.969,17 |
| SRVs criados/quadro | 1.304,52 | 18,47 |
| SRVs reusados/quadro | 690,08 | 1.950,70 |
| Hits de outras submissões/quadro | 0 | 1.250,74 |
| Evicções/quadro | 0 | 18,47 |
| Invalidações de recurso/quadro | 0 | 0 |
| Cache live min–max | 0–0 | 4096–4096 |
| Região SRV, ms/quadro | 4,196 | 1,459 |
| Região SRV, µs/stream dinâmico | 2,104 | 0,741 |
| Streams total, ms/quadro | 15,367 | 10,935 |
| Worker na timeline, ms | 77,81 | 65,50 |
| GPU real na timeline, ms | 7,07 | 6,93 |
| Intervalo médio na timeline, ms | 94,1 | 79,4 |

Redução de **98,58% nas criações** e de **65,23% no custo da região SRV** nessas janelas. O cache manteve o limite mesmo com evicções; foram validados todos os 31 blocos Off e 38 On, incluindo os fora da janela, com as partições e subtempos corretos. Não apareceram linhas de falha/removal de device. As janelas não tiveram buffers ausentes, falhas de chamadas ou alocação de SRV. A troca de recurso que invalida uma lease não ocorreu na janela; sua segurança foi verificada por testes da política e revisão do código, sem afirmar cobertura GPU desse ramo.

**Validade de cena/imagem:** a câmera de idle mudou e o HUD apareceu visualmente ao final Off; o detector existente `Test-SupermanHud`, sem alterar thresholds, retornou False nas quatro capturas JPEG das janelas. O gate de imagem existente também reprovou a comparação inicial Off/On: PSNR 19,28 dB, histograma 0,1901 e cromaticidade da região personagem 0,2731 contra limite 0,05. A câmera não está alinhada. Portanto, não há aprovação de equivalência visual, nem prova de que o cache causou a diferença de imagem. Não substituir o golden nem relaxar o gate para transformar isso em aprovação.

Os logs são usados como diagnóstico da cidade renderizada, conforme a ressalva da tarefa original, e não como benchmark de gameplay aceito para FPS. Outros subtempos também variaram; não atribuir todo o delta de worker/intervalo ao cache. As médias instrumentadas são 10,6 e 12,6 FPS; **60 FPS não foram atingidos**. A redução de criação de views e os hits são evidência direta do mecanismo, enquanto a aceitação visual/de desempenho continua pendente.

Decolagem e voo sobre a cidade foram observados com On depois da janela; capturas `buffer_srv_20261023_on1_flight{,_end}.jpg`. Não foram vistos buracos grosseiros de geometria nessas capturas, mas isso não substitui o gate. Os processos próprios foram encerrados. Relatórios completos `logs/buffer_srv_20261023_{off1,on1}_{launch,window,end,blocks}.json`, timelines e `logs/buffer_srv_cache_20261023_{comparison,image_gate,hud_gate}.json`. O gate de imagem foi executado com Windows PowerShell 5; a primeira tentativa em PowerShell 7 falhou ao compilar o helper System.Drawing, sem mudança do gate.

O incremento comprova redução do churn de views e custo menor nessa região, com os limites preservados nesta rodada. A opção continua **0 por padrão**; capturas alinhadas, execução sem profiling e mais cenários de streaming/recriação de recursos são necessários antes de promover o cache.

Não promover o cache a padrão nem afirmar 60 FPS com base em redução de criações de view. RT/DLSS/menu e fidelidade das sombras ao console continuam pendentes; áudio adiado.
