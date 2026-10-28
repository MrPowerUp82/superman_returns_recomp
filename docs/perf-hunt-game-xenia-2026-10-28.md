# Caça de otimizações: jogo + Xenia Canary — 28/10/2026

Notebook de trabalho (i5-13420H + Intel UHD), D3D12 nativo, 1280×720, limite 60, build do main após `45314f7`. Uma rodada por configuração; números de cena idle/forward do `bench_api.ps1`.

## Ferramenta REA

O REA 6.8.0 conectado não decompila PowerPC estático neste host: raw-binary não aceita `powerpc`, o formato `gamecube-dol` só roda com Ghidra em POSIX, e o Ghidra do REA no Windows aceita apenas PE nativo (ELF PPC rejeitado). IDA/Kuna não estão configurados e não têm trilha PowerPC. As funções do jogo foram lidas no C++ recompilado (`port/generated/default`), que é a tradução exata por instrução.

## Onde o quadro vai

Timeline sem perfis (`xdkab0`): intervalo 23,1 ms (43 FPS), thread do jogo 16,4 ms (limitante), worker 9,9 ms. Com perfis (`guestcpu`): jogo 21,1 ms = guest 15,1 + front-end 6,0; GPU real 25,8 ms (98%).

### GPU (o limite neste notebook)

Agrupamento novo por render target (`sr_native_gpu_pass_timing`, abaixo) e perfil de um quadro (`sr_native_profile_frame_at_s=80`, 25,96 ms):

| Alvo / shader | ms/quadro | Parte |
| --- | ---: | ---: |
| Cena principal 1280×720 f12 + depth | 15,8–18,5 | 72% |
| Shadow map 640×640 f4 (1.049 draws) | 2,0–2,6 | 8% |
| Saída 1280×720 f0 | 1,4–1,9 | 7% |
| VS `2C45BFFE76D77398` / PS `8A6DD1D52752FAD6` (iluminação do mundo), 680 draws | 7,66 | 30% |
| VS `29C1ACD9F4373197` / PS `91111A326A89010A`, 184 draws, 23 µs/draw | 4,30 | 17% |
| Draws inline (`drawinline`, 152) | 4,33 | 17% |
| Resolves (30) | 2,06 | 8% |

A cena principal já tem 1280×720 no próprio jogo (o RT guest é desse tamanho), então não é escala nossa. No DXIL do PS `8A6D…`, os `GetDimensions` de `tfetch2D` já são deduplicados (5 para 27 samples). Não foi achado desperdício barato no HLSL traduzido. O custo é a iluminação do jogo numa iGPU.

### Thread do jogo

`SR_NATIVE_GUEST_CPU_PROFILE=1`, por quadro (~76 quadros por janela de 2 s; inclui o custo do hook, ~20–40 ns por chamada):

| Função | Chamadas/quadro | Inclusivo | Próprio rastreado |
| --- | ---: | ---: | ---: |
| `82783948` (raiz da submissão de render) | ~90 | ~10,7 ms | ~2,2 ms |
| `82316958` | ~1.300 | ~8,4 ms | ~0,6 ms |
| `822F5290` (≈ um por draw) | ~1.200 | ~7,4 ms | ~3,1 ms |
| `8230F8E0` | ~470–550 | ~3,4–4,3 ms | ~1,5–2,0 ms |
| `823055A8` | ~410 | ~1,8 ms | ~1,35 ms |
| XDK `820FBBF8` + `820FC000` + `821078C0` | ~7.400 | — | ~2,1 ms |
| `82319108` / `826092B8` (folhas) | ~13.000 / ~1.800 | — | ~0,6 / ~0,45 ms |

`82319108` é uma folha de ~50 ns que lê uma flag e copia um vetor; `826092B8` aplica um bloco de estados do RenderWare (bitmask de estados sujos). Nas folhas, a maior parte do tempo medido é o próprio hook.

## Pistas testadas e descartadas

- **Occlusion queries (inspirado no `occlusion_query_enable` do Canary):** o renderer responde toda query como visível. Contador temporário: menos de 4.096 queries em ~4.000 quadros. O jogo praticamente não usa occlusion culling; respostas reais não cortariam draws. Contador removido.
- **Bypass do XDK (`SR_NATIVE_XDK_DRAW_SUBMISSION=1`):** jogo 16,4 → 17,6 ms, FPS 43,3 → 42,5 (variação de cena). Sem ganho mensurável; continua opt-in.
- **Cadência de vblank do Canary:** já portada (`xenia-canary-fps-unlock-2026-10-27.md`).

## Candidatos restantes (por custo/benefício)

1. **Front-end `fe_streams` (2,8 ms):** `fb_refresh` 2,3 ms + `fb_hash` 1,35 ms são o hash de conteúdo dos buffers rastreados, um fallback de correção. Trocar por tracking de escrita (como já feito nas texturas) é o maior ganho de CPU ainda isolado, mas muda a política de validação dos buffers e precisa de decisão do usuário.
2. **GPU em iGPU:** só opções de qualidade (sombra, efeitos) ou trabalho no XenosRecomp guiado por um perfilador de GPU (PIX/Intel GPA) no PS `8A6D…` e no par `29C1…`/`9111…`.
3. **Reimplementação nativa de `822F5290`** (render por draw do RenderWare, ~3 ms próprios): engenharia reversa grande; sem decompilação estática pelo REA aqui.
4. **Medir na RTX 2060:** lá a GPU sai do caminho e o limite passa a ser a thread do jogo (16–21 ms).

## Ferramenta nova

`sr_native_gpu_pass_timing=true` agora agrupa o tempo de GPU pela assinatura do render target (`p<n>[WxH f<formato> +ds]`), porque o perfil do jogo não tem papéis de passe (todo draw era `p0`). O buffer de timestamps passou de 64 para 512 por quadro. Desligado por padrão.

## Hash de buffers do front-end trocado por tracking (experimento, opt-in)

`RefreshTrackedBuffer` re-hasheia uma vez por quadro cada buffer rastreado de até 32 KiB, para pegar escritas por aliases virtuais. Implementado (`SR_NATIVE_BUFFER_TRACKING=1`): depois de 4 hashes seguidos sem mudança (limiar dobra a cada perda, até 256), o buffer ganha um watch do runtime de tracking; um `Sample` antes e depois do hash prova os bytes, e nos quadros seguintes um `Sample` igual ao baseline substitui o hash. Política em `buffer_tracking_policy.h` com testes.

A/B no mesmo executável, timeline detalhada, uma rodada cada:

| Medida (ms/quadro) | Off | On |
| --- | ---: | ---: |
| `fb_hash` | 1,14 | 0,76 |
| `fb_refresh` (inclui hash e Sample) | 2,07 | 2,45 |
| `fe_streams` | 2,31 | 2,66 |
| Thread do jogo | 17,54 | 18,29 |

On: ~3,2 milhões de provas reusadas (~800 por quadro), 35 mil watches, 25 mil perdas. O `Sample` custa ~1 µs, mais que o hash de ~0,5 µs que ele evita: cada chamada pega o `global_critical_region` (o mesmo lock das threads do jogo) mais o mutex do estado e buscas em `std::map`. Resultado: pior com o `Sample` travado. Resolvido abaixo com uma view sem lock no runtime.

### View sem lock no runtime (ligado por padrão)

`tools/rexglue-write-tracking` (copiado para o SDK pelo `build.cmd`) passou a espelhar as épocas por página num array estático `uint64_t`, mais um contador de domínio, exportados por `RexGetMemoryTrackingFastView` (ABI opcional; um runtime antigo continua no `Sample` travado). As épocas só crescem e toda perda de cobertura (fault de escrita, produtor do host, alocação/liberação, mudança de proteção, unwatch) grava uma época nova em cada página afetada antes de ter efeito; o domínio cresce em start/stop, reset e perda total. A prova continua sendo criada com `Sample` travado antes e depois do hash; a checagem por quadro vira "domínio igual e maior época das páginas do intervalo igual à do baseline", só com leituras atômicas.

Testes: `test_tracking_fast_view.cpp` (épocas de páginas dentro/fora do intervalo, produtor do host, liberação, perda de domínio, reset), 523 testes nativos e os 4 testes `sr_memory_tracking` passando.

| Medida (ms/quadro) | Off, rodada 1 | Off, rodada 2 | On |
| --- | ---: | ---: | ---: |
| `fb_hash` | 1,27 | 1,13 | 0,67 |
| `fb_refresh` | 2,21 | 2,06 | 1,69 |
| Front-end | 5,73 | 5,33 | 4,76 |
| Thread do jogo | 18,94 | 17,86 | 17,61 |

Ganho de ~0,4–0,6 ms no front-end. O "forward" de 6,8 FPS da primeira rodada Off veio do encerramento (amostras 12,7 e 0,9 FPS no fim) e não se repetiu. Churn ainda alto: ~33 mil watches e ~23 mil perdas na rodada, porque qualquer invalidação aposenta a alocação inteira no runtime até ser realocada. Ligado por padrão; `SR_NATIVE_BUFFER_TRACKING=0` volta ao hash. Sem gate de imagem nesta etapa: observar geometria animada (capa, tecidos) no jogo.
