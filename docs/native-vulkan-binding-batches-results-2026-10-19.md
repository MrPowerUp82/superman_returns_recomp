# Resultado: preparação das ligações em blocos — 19/10/2026

Continuação: [replay compacto, validação e medição sem perfil](native-vulkan-compact-replay-results-2026-10-19.md).

**60 FPS reais ainda não foram atingidos.** A preparação agora faz menos
aquisições e alocações, mas o teste no jogo não demonstra um ganho de FPS
conclusivo. O caminho global permanece opcional.

## Implementação

`GlobalDrawBindings` reúne constantes, streams e tabelas de até 128 draws em
uma alocação do arena. O cabeçalho aponta para os chunks e cada registro
mantém os offsets absolutos esperados pelos shaders. A ABI não mudou.

As ligações de buffers, imagens e samplers são adquiridas uma vez por owner
distinto em cada submissão. O cache é limpo ao mudar de submissão ou retirar
a submissão atual. O heap mantém os owners vivos até o fence. Reutilizar um
ResourceId para outro buffer/textura não reutiliza indevidamente o slot antigo.

Foram adicionados contadores de páginas, aquisições e alocações ao perfil de
120 quadros, para conferir a redução de trabalho também no jogo.

## Teste isolado, sem validação durante a medição

Mesmo fixture na RTX 2060: 3000 draws indexados, IA na posição 7, offsets e
base vertex alternados; cinco execuções antes e cinco depois. Todos os pixels
foram conferidos após cada submissão.

| Medida | Antes | Depois |
| --- | ---: | ---: |
| Aquisições de recursos no heap | 18005 | 37 |
| Alocações transientes contadas depois | não instrumentado | 26 |
| Páginas | 4 | 24 |
| Mediana de preparação/gravação dentro de Record | 18,6914 ms | 15,7894 ms |
| Mediana incluindo montagem do fixture | 23,2939 ms | 20,7913 ms |

A redução indicativa de Record é 15,5%; incluindo a montagem, 10,7%. O fixture
alterna dois packets quentes; não representa a variedade do gameplay. Não
converter esses percentuais em promessa de FPS. A montagem inclui criação
de recursos/pipelines do teste, e não corresponde à captura do guest.

Logs: `logs/native60_binding_batches_{baseline,green}_perf.log`.
O teste de orçamento de aquisições falhou antes da otimização, com 18005,
e passou depois, com 37; `logs/native60_binding_batches_red.log`.
O teste completo agora também impõe o orçamento de alocações.

## No jogo

Mesmas opções de lançamento: Vulkan nativo, global=1, escala 1, MSAA 1,
LSFG desligado, limite solicitado 60, janela lógica 1280×720 e swapchain
físico 1600×900. Executáveis preservados:

- Antes: `554A5E04C5F1AF51B075349D1AA01C3544AE83939C61DC5C9AAEF61545A273D5`.
- Depois: `03AA7CFF4732315F357BA2867642A2DDF8218D5218C7E7B7AD89D1C405DB8BA8`.

| Rodada | FPS parado | FPS em movimento | Mínimo em movimento |
| --- | ---: | ---: | ---: |
| Antes | 8,41 | 10,04 | 8,3 |
| Depois | 7,27 | 10,67 | 9,3 |

Cada timer de medição durou nominalmente 20 s. Leitura dos logs pode incluir
amostras posteriores ao timer; 11–12 amostras foram registradas por janela.
Os FPS são do guest, não contagens de apresentações repetidas. Sem builds
ou testes concorrentes durante as janelas.

Houve grande variação na janela parada nova: 5,4 a 10,3 FPS. A câmera de
inatividade orbita; os percursos chegaram a paredes diferentes. Não houve
replay determinístico ou prova de equivalência de pixels entre as rodadas.
Portanto, nem a alta de cerca de 6% em movimento nem a queda parado provam
ganho ou regressão causal. A referência antiga de aproximadamente 13,5 FPS
continua registrada e preservada, mas também não é uma cena equivalente.

Timelines recortadas pelo helper, analisadas sem somar estágios sobrepostos:

| Estágio em movimento | Antes | Depois |
| --- | ---: | ---: |
| Game, incluindo trabalho não atribuído exclusivamente à simulação | 99,54 ms | 94,11 ms |
| Frontend | 45,45 ms | 42,51 ms |
| Worker | 56,23 ms | 54,46 ms |
| Record | 83,43 ms | 71,97 ms |
| GPU real | 14,26 ms | 13,63 ms |

As diferenças de carga limitam a interpretação dos tempos. As janelas de
perfil próximas ao percurso novo mostram cerca de 3000 aquisições por quadro
e 50–56 alocações. A redução estrutural foi observada, mas os demais custos
continuam acima dos 16,67 ms necessários para 60 FPS.

Evidência: `logs/native60_binding_{before,after}_20261019_*`, respectivas
timelines e `logs/native60_binding_{before,after}_{idle,forward}_timeline.json`.
Personagem e HUD foram observados em movimento; a inspeção não substitui a
prova automatizada de pixels nem valida todas as cenas do jogo.

## Correção e revisão

- Build do jogo concluído: `logs/native60_binding_batches_game_build.log`.
- Build de todos os alvos Vulkan concluído: `logs/native60_binding_batches_final_tests_build.log`.
- 108 testes Vulkan, zero falhas: `logs/native60_binding_batches_final_unit.log`.
- Prova GPU final com validação ativa e zero erros:
  `logs/native60_binding_batches_final_gpu.log`.
- Comparação integral de pixels em 2D/3D/cube, IA, índices e offsets. Novos
  casos substituem textura e buffer sob o mesmo ID em 258 draws, atravessando
  limites de página, tanto indexados quanto não indexados. A prova existente
  de duas versões pendentes no heap também passou.
- Revisão independente focada em owners, retirada, offsets, alinhamento,
  memória não coerente e falhas parciais não encontrou achados acionáveis.

A primeira tentativa de exigir validação não encontrou as camadas locais;
a execução foi repetida com `VK_LAYER_PATH=.tools/vulkan-validation/Bin` e
confirmou `validation_active=1 validation_errors=0`. Medições de desempenho
ficaram separadas e sem essas camadas.

## Próxima frente indicada pelos perfis

No replay próximo ao percurso, `decode` ficou em aproximadamente 32–33 ms e
`arena_setup` em 13–14 ms por quadro. Mesmo zerar a emissão Vulkan, que custa
aproximadamente 3–4 ms nesses perfis, não entrega 60 FPS. É necessário reduzir
a materialização de DrawPacket/estados e suas cópias entre captura e replay,
além de preparar a gravação paralela. O pool de packets já foi experimentado
anteriormente; não deve ser apresentado novamente como solução inédita.

Nenhuma publicação ou commit foi realizado nesta etapa. Os executáveis
anterior e novo foram preservados para comparações posteriores.
