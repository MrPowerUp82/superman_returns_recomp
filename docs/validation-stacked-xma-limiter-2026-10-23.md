# Validação: texturas empilhadas, XMA e cadência — 23/10/2026

## Ambiente e procedimento

- Checkout inicial limpo, HEAD `bc286c5b05123709feddc21fdbbcdc8a5358f669`, contendo `c2d7517`.
- `build.cmd` executado primeiro e concluído com código 0, incluindo o launcher.
- Intel Core i7-10750H, NVIDIA RTX 2060; o log D3D12 confirma a seleção da RTX 2060.
- EXE Release: `port/out/build/win-amd64-release/superman_returns.exe`.
- SHA-256: `C1FC299694D25F4BE7115A7C5BBD9CC7674C2CA3C7FE67A11F3220B9B1BF75A8`.
- Native, 1280×720, escala 1, VSync, MSAA 1, FXAA desligado, LSFG desligado.
- Lidos o histórico, o handoff, a documentação XMA e a investigação D3D12 anterior antes de qualquer alteração. Nenhum código do jogo, SDK, shader ou benchmark foi alterado nesta sessão.

Os quatro comandos pedidos de `bench_api.ps1` foram executados com o EXE Release: `stk_d3d12`, `stk_vk`, `lim60` e `lim30`. Os quatro terminaram com código 0. Nas timelines foi acrescentado apenas `-WindowMetadata logs/<nome>_windows.json` para registrar as janelas.

O benchmark automático entra em New Game e mede parado/andando, sem percurso de voo. Foram feitas duas rodadas complementares, com `SR_BENCH_INPUT` e mouse desabilitado, confirmadas visualmente acima dos prédios de Midtown. Seus argumentos, hash, PID e início estão em `logs/stk_flight_<api>_launch.json`. A primeira tentativa de lançamento ocorreu antes de o tutorial aceitar a ação; uma segunda pressão longa de Y produziu a decolagem nas duas APIs. Houve 20 s de comando de movimento após a captura diante da torre.

## A — Texturas empilhadas

| Log | Linhas `texture decode failed (stacked 2D array)` |
| --- | ---: |
| `logs/bench_stk_d3d12.log` | 0 |
| `logs/bench_stk_vk.log` | 0 |
| `logs/stk_flight_d3d12.log` | 0 |
| `logs/stk_flight_vk.log` | 0 |

Não existem as aproximadamente dez linhas solicitadas com `fetch`/`fmt` nesses logs: a mensagem não ocorreu. A contagem zero do Vulkan isoladamente não comprova suporte completo, porque os backends têm diagnósticos diferentes. O dado decisivo aqui é que o D3D12 também não registrou a rejeição, inclusive no voo observado. Isso não exclui ocorrência em outras cenas ou recursos que não chegam a esse caminho.

### Imagens comparáveis

Mesma esquina inicial, com câmera sem mouse nas rodadas complementares. Janelas iluminadas aparecem nas duas APIs. As imagens diante da mesma torre após a decolagem também mostram iluminação em fachadas nas duas APIs. O instante de animação e a câmera automática variam; não são imagens idênticas nem um gate de equivalência.

| Cena | D3D12 | Vulkan |
| --- | --- | --- |
| Esquina inicial | [captura](../logs/stk_flight_d3d12_ground.jpg) | [captura](../logs/stk_flight_vk_ground.jpg) |
| Diante da torre, em voo | [captura](../logs/stk_flight_d3d12_hover.jpg) | [captura](../logs/stk_flight_vk_hover.jpg) |
| Acima dos prédios | [captura](../logs/stk_flight_d3d12_city.jpg) | [captura](../logs/stk_flight_vk_city.jpg) |

Há diferenças de brilho/efeitos, particularmente nas capturas do benchmark legado. Não foi confirmada fidelidade das sombras ao console: falta uma referência do console da mesma cena e a identificação dos draws e recursos responsáveis. Áreas escuras entre prédios não bastam para validar shadow maps.

### Contrato de shader/SRV

- `port/src/native_renderer/native_renderer.cpp:3518`: a guarda que rejeita `decoded.depth != 1`, exceto cube/volume, realmente existe.
- `.tools/xenosrecomp/src/XenosRecomp/shader_common.h:209`: os heaps são `Texture2D`, `Texture3D` e `TextureCube`, sem heap `Texture2DArray`.
- O DXIL real `2EFEE77C92B2F443.ps.dxil` foi inspecionado com `dxc -dumpbin`. A tabela declara `g_Texture2DDescriptorHeap` como `2d` em `t0`, e `g_TextureCubeDescriptorHeap` como `cube` em `t0,space2`. Evidência: `logs/stk_shader_2EFEE77C92B2F443_dxil.txt`.
- `port/src/graphics/vulkan/resources.cpp`, `PlanTextureUpload` e `UploadTexture`: a alocação/copias preservam as camadas, mas o recurso 2D usa `VK_IMAGE_VIEW_TYPE_2D`, com uma camada exposta pela view.
- `port/src/graphics/vulkan/descriptor_sets.cpp`, `DescriptorStore::Shared`: exige view 2D/3D/cube compatível com o heap correspondente.
- `tests/vulkan/resources_integration.cpp`, `CheckStackedResolve`: verifica preservação das quatro camadas após resolve na camada 3, com view de fetch 2D de uma camada. Não testa amostragem arbitrária das camadas como array.

**Decisão:** não implementar o patch condicional. A condição “D3D12 rejeita e Vulkan não” não foi observada. Trocar somente o SRV por `Texture2DArray` também não respeitaria o contrato atual do shader. Se uma textura empilhada for identificada, primeiro rastrear seu fetch e o uso das coordenadas no shader; um suporte a array terá que coordenar declaração, índice da camada, bindings/SRV e cache de shaders, com opção desligada por padrão. A alternativa de view 2D de uma camada precisa igualmente de prova da semântica guest e não foi aplicada como substituto presumido.

Resultados auxiliares de FPS do benchmark A: D3D12 16,6 parado / 16,3 movimento; Vulkan 11,5 / 10,9. Uma tentativa de Y ocorreu no final da janela de movimento D3D12; essa janela não é um baseline puro de caminhada. As rodadas A não demonstram ganho causal entre APIs.

## B — XMA após a decolagem

A rodada complementar D3D12 permaneceu em gameplay aéreo de **12:28:08,205 a 12:31:28,068**, horário de São Paulo: **199,86 s**, após decolagem visualmente confirmada. Marcadores: `logs/xma_flight_start.txt` e `logs/xma_flight_end.txt`. Imagem final: `logs/xma_after_3min.jpg`.

Em `logs/stk_flight_d3d12.log`:

- `XMA fix: flushed final block`: **0 linhas**.
- `XMA fix: stopped waiting`: **10 linhas**. O código limita esse log às primeiras dez ocorrências; não representa o total de esperas abandonadas.
- Uma linha `no final-block flush`:

```text
[2026-10-23 12:27:27.745] XMA fix: no final-block flush for context FFCA8000 (stream 0362A1E8, sound 0362A1A0): input pending
```

- Último resumo, arredondado pelo próprio log a cada 100 recusas:

```text
[2026-10-23 12:31:28.693] XMA fix: 0 flushes, 95200 declined: input pending=95200
```

Não houve confirmação auditiva do corte ou da continuidade da música. O usuário confirmou que não acompanharia o áudio e orientou registrar somente o que for comprovável pelos logs. Logo, o resultado é **inconclusivo quanto à música**, e o fix de flush continua sem validação funcional em gameplay. `SR_XMA_FLUSH=0` não foi comparado, pois o gatilho condicional “a música cortou” não foi confirmado. Não há dados para atribuir um corte a outra causa.

As duas rodadas A também deram zero flushes e dez linhas de desistência cada: último resumo D3D12 124.800 recusas; Vulkan 54.000. No voo complementar Vulkan: zero flushes, dez linhas de desistência, último resumo 297.100 recusas. Todos os resumos apontam `input pending`. Contagens e linhas originais completas: `logs/validation_2026-10-23_counts.json`.

Próximo passo se o corte for reproduzido por escuta: comparar ON/OFF no mesmo percurso e registrar, no contexto sem progresso, validade/offsets de entrada e saída, espaço do ring, `error_status`, carry/subframes e estado do segmento. `input pending` sozinho não distingue erro de decoder, saída sem espaço, concorrência ou entrada ainda válida. Não remover essa proteção com base apenas no contador.

## C — Limites 60 e 30

Os logs confirmam:

```text
lim60: native graphics: frame rate limit 60 (guest vblank 120 Hz)
lim30: native graphics: frame rate limit 30 (guest vblank 60 Hz)
```

Relatórios originais pedidos, preservados:

- [timeline_lim60.txt](../logs/timeline_lim60.txt): últimos 1.100 quadros, intervalo médio **91,6 ms**, game **78,45 ms**, worker **88,23 ms**, GPU **7,28 ms**.
- [timeline_lim30.txt](../logs/timeline_lim30.txt): últimos 1.100 quadros, intervalo médio **88,3 ms**, game **77,08 ms**, worker **84,54 ms**, GPU **7,22 ms**.

Esses relatórios padrão podem incluir entrada/carregamento. Além disso, o wrapper usa orçamento de 33,33 ms no relatório padrão mesmo com `-FpsLimit 60`; isso afeta a coluna de folga, não os tempos medidos. Foram produzidos recortes com orçamento correto e somente quadros dentro das janelas de gameplay.

### Janelas de gameplay

As janelas UTC do benchmark foram relacionadas ao relógio steady/QPC com `logs/lim<limite>_clock_anchor.json`. Foram excluídos 250 ms de cada borda e os quadros que cruzam as bordas. A relação dos relógios é aproximada; o recorte independente dos últimos 120 quadros abaixo corrobora a conclusão sem essa conversão.

| Limite | Janela | Quadros | Intervalo médio (ms) | game (ms) | front_wait (ms) | worker (ms) | GPU real (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 60 | parado | 266 | 95,85 | 75,60 | 20,24 | 94,25 | 6,95 |
| 60 | andando | 182 | 91,52 | 75,68 | 15,89 | 88,96 | 7,49 |
| 30 | parado | 237 | 94,27 | 78,35 | 15,93 | 92,55 | 7,36 |
| 30 | andando | 169 | 92,24 | 80,15 | 11,98 | 89,97 | 7,80 |

Arquivos completos: `logs/timeline_lim{60,30}_{idle,forward}.{csv,json,txt}`. As janelas têm a duração real registrada nos JSONs; o contador de GPU do benchmark pode alongar a janela nominal de 20 s.

Nos últimos 120 quadros: limite 60 → **82,62 ms**; limite 30 → **87,81 ms**. Evidências: `logs/timeline_lim60_tail120.json` e `logs/timeline_lim30_tail120.json`.

As amostras periódicas do benchmark deram 10,6 / 11,0 FPS com limite 60 e 10,4 / 10,9 FPS com limite 30, parado/andando. Média de amostras periódicas e inverso do intervalo médio são estatísticas diferentes.

**Conclusão:** não houve platô em 33,33 ms com game/worker/GPU ociosos. Há um worker com tempo medido próximo ao intervalo e GPU com grande folga. O campo `busy` é tempo de parede instrumentado, não contagem de ciclos CPU; o `game` também pode conter esperas guest não separadas. Estes dados não excluem um limitador que fique oculto pelo custo atual, mas não justificam procurar/patchar constantes ou loops de 30 FPS nesta etapa. Conforme a condição solicitada, não foi feita busca adicional em `0x82110A30`/`0x824680D0`, nem alteração das constantes 1/30.

Próximo passo: perfil separado do worker/captura, com custo do perfil explícito, para localizar operações que dominam esses ~90 ms. Não tratar folga de GPU como garantia de 60 FPS.

Continuação executada após o usuário adiar áudio: [perfil do worker e comparação diagnóstica do rastreamento](d3d12-worker-profile-2026-10-23.md). O worker consumiu 0,955 núcleo; no mesmo EXE/DLL experimental, Off/On mediu hash de 35,58/0,36 ms e worker de 95,23/60,74 ms. O gate de imagem reprovou capturas sem alinhamento; Off permanece padrão e 60 FPS não foram atingidos.

## Resultado

| Item | Resultado medido | Causa confirmada? | Próximo passo |
| --- | --- | --- | --- |
| A | Zero rejeições empilhadas nas duas APIs, inclusive voo; janelas iluminadas visíveis em ambas | Guarda existe no código, mas não foi ligada ao defeito observado; sombras sem validação de console | Identificar draw/fetch e referência visual antes de mudar contrato de shader/SRV |
| B | 199,86 s após decolagem; zero flushes; último resumo 95.200 recusas por input pending | Flush não exercitado; corte de música sem confirmação auditiva | Escuta + comparação ON/OFF se houver corte; instrumentação do contexto se necessário |
| C | Intervalo andando 91,52 ms com 60 / 92,24 ms com 30; worker ~89–90 ms, GPU ~7–8 ms | Platô de 33,3 ms não reproduzido | Localizar custo do worker em medição separada; nenhum patch de limitador |

Nenhum commit ou alteração de implementação foi feito. Os logs e imagens são artefatos locais em `logs/`.
