# Otimização de desempenho — design

Status: aprovado em 2026-09-28. Implementação ainda não iniciada.

## Resumo do entendimento

- Otimizar o recomp com dois presets: **Qualidade** (visual atual, padrão) e **Desempenho** (hardware fraco).
- Meta: **30 FPS estáveis no notebook de referência** (i5-13420H + Intel UHD, sem GPU dedicada) com o preset Desempenho. Em PCs com GPU dedicada, o preset Qualidade também deve melhorar.
- Cada alavanca é uma opção própria (resolução interna, pós-processamento, sombras, distância de desenho) além do seletor de preset, exposta no menu F4 e salva no `.toml`, como no recomp de Dante's Inferno.
- Alavancas permitidas: qualquer perda visual no preset Desempenho e otimizações de CPU.
- Ordem: configurações e patches no jogo primeiro, medindo cada passo; renderizador nativo só como última etapa, se ainda faltar para os 30 FPS.
- Fora de escopo: launcher separado; renderizador nativo antes de medir as alavancas simples.

## Ponto de partida (medido)

- Mundo aberto, preset atual (`rtv` + `depth_float24_convert_in_pixel_shader`): ~8–16 FPS conforme a cena.
- GPU 3D em ~99%; 7 threads de CPU a 100% (~8 núcleos), provavelmente espera ativa.
- A resolução do runtime só aumenta (`draw_resolution_scale` ≥ 1).

## Premissas

1. O preset Qualidade fica visualmente idêntico ao atual; só otimizações de CPU se aplicam a ele.
2. Algumas opções (resolução interna) podem exigir reiniciar o jogo; as outras se aplicam na hora quando for barato.
3. Protocolo de medição: mesmo ponto do save, média e mínimo de FPS em 20 s, parado e voando. "30 estáveis" = média ≥ 30 e mínimo ≥ 27 nos dois cenários.
4. Os testes abrem e fecham o jogo automaticamente, com aviso prévio ao usuário.
5. Cada patch no jogo depende de engenharia reversa e só entra se for localizado e confirmado; os inviáveis são documentados.

## Riscos

- Não se sabe se as alavancas de configuração bastam para 30 FPS: a emulação da EDRAM tem custos fixos que não dependem da resolução.
- Um patch de resolução pode desalinhar o HUD ou efeitos em espaço de tela.
- Não está provado que reduzir a espera ativa da CPU libera clock para a GPU integrada.

## Design

### Configuração

`port/src/sr_settings.{h,cpp}` declara cvars `REXCVAR_DEFINE_*` na categoria "Superman Returns" (o runtime as mostra no F4 e persiste no `.toml`).

| Opção | Valores | Padrão |
| --- | --- | --- |
| `sr_preset` | `quality`, `performance`, `custom` | `quality` |
| `sr_render_scale` | `100`, `75`, `50` | `100` |
| `sr_post_effects` | `on`, `off` | `on` |
| `sr_shadows` | `high`, `low`, `off` | `high` |
| `sr_draw_distance` | `100`–`25` | `100` |

A lista final só inclui opções com patch confirmado. O preset preenche apenas as opções que ainda estão no padrão (`GetFlagSource == kDefault`), então uma mudança individual prevalece. Os padrões de GPU passados hoje pelo `run.cmd` (`rtv` e a correção do céu) vão para o `OnPreSetup` com a mesma regra. Cada alavanca fica em `port/src/perf_*.cpp`, com seus hooks registrados no manifesto.

### Etapa 0 — medição

- `tools/bench.ps1`: fecha o jogo aberto, abre com a configuração testada, aperta Start na tela de título (carrega o save), espera 25 s e mede 20 s parado e 20 s voando. Grava em `logs/bench_results.csv` (configuração, cenário, FPS médio e mínimo, núcleos, % de GPU) e salva screenshots. Roda num processo só, com timeout em cada passo e log de progresso.
- Pular os vídeos de abertura, se houver patch simples.
- RenderDoc (integração já existe no runtime) para ver o custo de cada passagem de GPU; se a captura falhar, medir desligando passagens uma a uma.
- `wpr` com build com símbolos (`-gcodeview`) para ver quais threads estão em espera ativa.
- Saída: tabela de custo por passagem e FPS de referência.

### Etapa 1 — ganhos sem perda visual

- Opções de registrador no manifesto (`cr_as_local`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `non_argument_as_local`) e `[entrypoint.rexcrt]` com `memcpy = 0x8279EC60` e `memset = 0x8279F290`. `non_volatile_as_local` e `skip_lr` ficam desligados (exigem setjmp/longjmp mapeados; quebram código que lê LR).
- Trocar espera ativa por espera real ou `yield` onde o `wpr` apontar.
- Medir o custo de `depth_float24_convert_in_pixel_shader`.

### Etapa 2 — patches no jogo

Para cada alavanca: localizar → patch → medir → expor como opção.

- Resolução interna: achar a criação dos buffers 1280×720 e reduzir as dimensões e o viewport.
- Pós-processamento: identificar as passagens de bloom/brilho no RenderDoc e pular as chamadas.
- Sombras: reduzir o shadow map ou pular a passagem.
- Distância de desenho/densidade: parâmetros de LOD e de spawn (a mais incerta, fica por último).

Todo hook confere o estado esperado antes de agir e não faz nada se ele não bater. Todas as opções ficam no padrão do preset Qualidade, então um patch problemático só afeta quem o liga.

### Etapa 3 — decisão sobre o renderizador nativo

Com a tabela de FPS do preset Desempenho em mãos: se chegou a 30, fechar e documentar; se não, abrir outro ciclo de design para um renderizador nativo, como em Dante's Inferno e AC6.

### Testes em cada mudança

1. Codegen com zero `REX_FATAL` e nenhum aviso novo.
2. Benchmark nos dois cenários, comparado com o resultado anterior.
3. Screenshots; o preset Qualidade deve ficar igual à referência.
4. 10 minutos no mundo aberto sem crash nem travamento (regressão do XMA).
5. Cada opção funciona via F4, `.toml` e linha de comando; o preset respeita mudanças manuais.

### Entregáveis

`sr_settings.*`, um `perf_*.cpp` por alavanca viável, `tools/bench.ps1`, `logs/bench_results.csv`, seção de desempenho no README (presets, opções, números medidos, alavancas inviáveis) e `run.cmd` simplificado. Um commit por etapa.

## Registro de decisões

| # | Decisão | Alternativas | Motivo |
| --- | --- | --- | --- |
| 1 | Os dois tipos de hardware, com preset Desempenho | só o notebook; só PCs em geral | atende o notebook sem sacrificar o visual padrão |
| 2 | Meta de 30 FPS estáveis no notebook | 20–25 FPS; melhora incremental | escolha do usuário; igual ao original |
| 3 | Qualquer perda visual, cada uma como opção | perdas fixas | escolha do usuário, seguindo o modelo de Dante's |
| 4 | Configuração por cvars (F4 + `.toml`) | launcher separado; só arquivo | o runtime já oferece; sem duplicação |
| 5 | Renderizador nativo só na última etapa | nunca; começar já | maior ganho, mas semanas de trabalho e mais risco |
| 6 | Abordagem A (patches no jogo), com B (fork do SDK) como plano B para resolução | B; C | sem fork para manter; caminho que funcionou em Dante's e AC6 |
| 7 | Qualidade = visual atual + só otimizações de CPU | mudar o padrão também | nenhuma regressão para quem tem GPU boa |
| 8 | Perfilar com RenderDoc e `wpr` | só eliminação | direciona o esforço para a passagem mais cara |
