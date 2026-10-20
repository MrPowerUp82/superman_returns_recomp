# Vulkan nativo: resultado no jogo em 19/10/2026

Continuação posterior: [preparação em blocos e novas medições](native-vulkan-binding-batches-results-2026-10-19.md).

**A meta de 60 quadros reais por segundo continua pendente.** A primeira
comparação de gameplay com o contrato global não mostrou ganho perceptível.
O modo global continua opcional; o contrato portátil segue como padrão.

## Rodadas reais

Mesmo executável SHA-256
`69AB86073E9B2BA584D7F4CFB2BF7E9DE8591F801164EE7B9B2D1B0BDFA79014`,
Vulkan nativo, escala 1, MSAA 1, LSFG desligado, limite solicitado 60.
Janela lógica 1280×720, swapchain físico 1600×900 neste Windows.
Bibliotecas locais de 241 shaders para cada ABI.

| Contrato | FPS parado | FPS em movimento | Mínimo das amostras em movimento |
| --- | ---: | ---: | ---: |
| Portátil, `GLOBAL=0` | 10,15 | 10,06 | 9,0 |
| Global, `GLOBAL=1` | 8,88 | 10,01 | 8,5 |

Cada janela nominal de medição durou 20 segundos. Os valores são a média dos
registros de guest FPS, aproximadamente a cada 2 segundos. Não são FPS de
apresentação: o presenter também repete imagens, e o total apresentado não
representa quadros reais novos. Não houve compilação ou testes nessas janelas.

Limites: a câmera de inatividade orbita o personagem; ela já altera a imagem
durante as janelas paradas. Os percursos terminaram junto a paredes diferentes.
Não houve replay determinístico nem equivalência integral de pixels entre as
duas rodadas. Portanto, a diferença pequena de FPS não estabelece regressão
causal ou ganho; ela mostra que o ganho de aproximadamente 2× do fixture não
se traduziu em melhora perceptível nessas execuções reais. As rodadas antigas
de aproximadamente 13,5 FPS também não são uma comparação controlada com hoje.

HUD, personagem e cidade foram observados em gameplay nos dois contratos.
A cidade escura aparece também no portátil; essa inspeção não substitui um
teste de pixels de todas as combinações do jogo. Capturas do helper, quando
a janela estava oculta, às vezes mostraram a janela que a cobria; foram
descartadas. A janela do jogo foi ativada para conferir o gameplay.

Timelines das janelas em movimento:

| Estágio | Portátil | Global |
| --- | ---: | ---: |
| Worker | 59,12 ms | 56,99 ms |
| Record Vulkan | 79,58 ms | 83,74 ms |
| GPU real | 13,93 ms | 14,86 ms |

São tempos de parede atribuídos pelos instrumentos existentes. As etapas se
sobrepõem e não devem ser somadas. O orçamento de 60 FPS é 16,67 ms por estágio
serial. O residual de game e a amostragem de threads incluem esperas; não
equivalem a tempo exclusivo de simulação ou instruções de CPU.

Evidências em `logs/native60_global_20261019_bench_{on,off}_*` e
`logs/timeline_native60_global_20261019_bench_{on,off}_{idle,forward}.{csv,txt}`.
Os arquivos `*_launch.json` preservam hash e argumentos exatos.

## Entrada reproduzível para o benchmark

Os toques curtos de teclado da automação eram perdidos antes de o guest consultar
o controle. `SR_BENCH_INPUT=<arquivo local>` agora instala um driver sintético
pela API do SDK. Ele compartilha o usuário 0, sem substituir o input normal.
Sem a variável, não há driver adicional nem leitura de arquivo.

Um comando contém quatro inteiros decimais:

```text
id_crescente duracao_ms mascara_botoes analogico_esquerdo_y
1 500 16 0
2 500 4096 0
3 24000 0 32767
```

16 é Start; 4096 é A. A duração começa quando o comando novo é recebido pelo
guest e fica limitada a 30 segundos. O estado volta a neutro automaticamente,
mesmo que o arquivo permaneça igual. Comandos repetidos, incompletos ou fora
dos limites não alteram o estado. A leitura ocorre no máximo 10 vezes/segundo.
O arquivo pode ser atualizado via PowerShell `Set-Content -Encoding ascii`.

Testes observaram falha antes da implementação, inclusive separador inválido,
e depois passaram: `logs/native60_benchmark_input_{red,separator_red,green}.log`.
Resultado: 186 testes nativos, zero falhas. Revisão adicional não encontrou
achados acionáveis de integração, endian ou vida útil.

## Amostragem para localizar o custo

Uma sonda local temporária amostrou o endereço de instrução das threads. Cada
thread é suspensa brevemente para copiar o contexto e retomada antes de escrita,
alocação ou resolução de símbolos. Essas execuções são diagnósticas, separadas
das janelas de FPS; não contam como benchmark de ganho.

No caminho portátil, as observações do recorder incluem BuildBindings (9,1%),
DescriptorStore::Shared (7,8%), a rotina de cópia `rep movsb` (10,5%) e espera
NtWaitForAlertByThreadId (13,0%). No global, incluem GlobalSlotTable::Acquire
(11,5%), `rep movsb` (10,7%) e NtWaitForAlertByThreadId (8,4%). São proporções
das observações de parede da thread, inclusive quando ela está esperando,
sem desempilhamento completo; não são percentuais exclusivos de ciclos.

O global teve 960 observações por thread durante 15,014 segundos, zero falhas
de captura. Contadores nativos mediram aproximadamente 0,950 core na principal
e 0,882 no recorder nessa janela. O mapa do executável resolve funções do jogo;
offsets de DLL são preservados. A rotina de cópia e as duas esperas citadas
foram conferidas por disassembly das DLLs locais.

Fontes e resultados da sonda ficam em `build/native60_cpu_sample.cpp`,
`build/analyze_native60_cpu_sample.py`, `logs/native60_20261019_*cpu_sample*`.
O executável medido e seu mapa foram preservados em
`artifacts/native-renderer-performance/benchmark_2026-10-19/`.

## Correção da substituição de slots

A revisão identificou que cada eviction reiniciava a busca no slot zero.
Se uma tabela cheia contém recursos vivos mas já concluídos, substituir todos
os N recursos em uma nova submissão fazia 1+2+...+N verificações. Com 65.536
buffers, isso pode ultrapassar 2 bilhões de candidatos examinados.

O teste determinístico conserva os donos antigos vivos, substitui 256 slots,
verifica que cada slot é usado uma única vez, que nenhum pendente é sobrescrito
e que a busca cresce linearmente. O teste falhou com o algoritmo anterior.
O cursor de eviction agora continua do ponto seguinte, inclusive após dar a
volta na tabela. Não há evidência de saturação nas rodadas de FPS acima;
esta correção não é apresentada como a causa ou a solução dos 10 FPS.

Validação: `logs/native60_global_eviction_red.log` acusa duas falhas no limite
de candidatos; `logs/native60_global_eviction_green.log` aprova 108 testes.
`logs/native60_global_eviction_gpu.log` aprova os pixels e as duas versões
pendentes, com validation ativa e zero erros. A revisão adicional não encontrou
achados acionáveis. A garantia é linear no número de candidatos examinados
numa substituição completa; a árvore de cache ainda tem operações O(log N).

O build final do jogo também terminou com sucesso, conforme
`logs/native60_eviction_final_game_build.log`. SHA-256 após a correção:
`554A5E04C5F1AF51B075349D1AA01C3544AE83939C61DC5C9AAEF61545A273D5`.
Esse binário está no snapshot como `verified_eviction_fixed.exe`; os FPS
acima pertencem ao binário anterior, preservado como `measured_69AB8607.exe`.

## Consequência para a estratégia

O fixture alterna dois packets quentes; o jogo tem milhares de draws, versões
de buffers e estados variados. O preparador global ainda faz buscas e escreve
8 KiB de bancos por draw. Trocar somente o contrato de descritores não resolveu
o custo de construir e reproduzir esses estados.

O próximo passo estrutural precisa preparar os recursos e metadados por lote,
reduzindo o trabalho repetido entre captura, replay e recorder, com prova de
ganho em carga representativa e preservação de passes, resolves e aliases.
O sampler e os novos controles permitem validar essa hipótese no jogo.
Nenhum commit, pacote público ou publicação foi realizado nesta rodada.
