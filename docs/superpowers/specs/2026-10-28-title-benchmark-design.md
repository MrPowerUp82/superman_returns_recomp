# Benchmark da tela de título — design (28/10/2026)

## Intenção

Um benchmark embutido no estilo do GTA V: abre direto numa cena do jogo, sem menu e sem nenhuma interação, renderiza sozinho, mede e mostra o resultado no fim. A cena é o sobrevoo do Superman pela cidade que o jogo já roda atrás do logo na tela de título.

Por quê: hoje o benchmark (`tools/bench/*.ps1`) abre o jogo normal, passa os menus apertando teclas pelo Windows (SendInput/HID), entra no gameplay e faz o Superman andar. Isso é lento, frágil (foco de janela, diálogos, cinemática de abertura) e prende o PC durante a rodada. O usuário quer abrir e ver o jogo renderizando sozinho.

## Objetivos

- Uma entrada que abre direto no benchmark: opção `--sr_benchmark=true` no jogo e um botão "Benchmark" no launcher que inicia o jogo com as configurações atuais mais essa opção.
- Pular os vídeos de introdução (já existe `sr_skip_intro`) e ficar na tela de título sem nenhuma entrada do jogador.
- Esconder toda a interface do título (logo, "PRESS START", copyright, linhas da moldura), mostrando só a cena 3D.
- Medir uma janela fixa da cena e, no fim, mostrar os resultados na tela do jogo e salvá-los em arquivo.
- Funcionar nas duas APIs nativas (D3D12 e Vulkan).

## Fora do escopo

- Benchmark de gameplay pilotado, replay de quadros gravados, câmera própria.
- Comparação entre rodadas dentro do jogo (o arquivo serve para isso fora).
- Qualquer automação por HID/SendInput.

## Não pode acontecer (falha mesmo que "funcione")

- Logo, texto ou moldura do menu aparecendo durante a medição.
- Medir o vídeo de demonstração: com o título parado, depois de ~78 s o jogo troca a cena por um vídeo de gameplay (FPS cravado em 30,0). Medido uma vez em 28/10, Intel UHD, D3D12. A janela de medição tem que terminar antes e detectar se ele começou.
- O menu avançar durante a medição (alguém aperta uma tecla ou o controle).
- Mudar a simulação, o limitador ou a qualidade do jogo só por estar em benchmark: mede-se com as configurações que o usuário escolheu.

## Fatos levantados (28/10)

- `--sr_skip_intro=true` leva direto à tela de título (`port/src/skip_intro.cpp`).
- O quadro do título é a cena completa da cidade (~938 draws no alvo principal 1280×720, 66% da GPU) seguida da interface. Trace de um quadro (`sr_native_trace_swap`): o último draw da cena é o filtro final VS `815DF1B3CCBFAA4E` / PS `B397B05F4CE5D5BB`; tudo que vem depois é interface: sprites do logo (VS `69523DC2B02555D3` / PS `40746066C58DA8D5`), fonte (VS `73208454D8616B19` / PS `F047F7F01CA481C6`) e um par VS `FCB6D23E7B941855` / PS `9841B42F263B95DE` (provável moldura). Pular só o PS do logo (`sr_native_skip_ps`) removeu o logo e deixou "PRESS START" e a linha superior, o que confirma que há vários shaders de interface.
- O mesmo ponto de corte (logo depois do filtro final) já foi estudado para o gameplay em `docs/pre-hud-boundary-evidence-2026-10-25.md`.
- Na rodada de 110 s, o título apareceu ~2 s após o primeiro log de FPS e o vídeo começou ~78 s depois. O FPS na cena variou de 37 a 122 conforme o voo.
- Não confirmado: se o voo repete exatamente o mesmo caminho a cada abertura, e se o tempo até o vídeo é medido em segundos ou em quadros (em GPU rápida ele pode chegar antes, se for por quadros). As funções Apt `SetToggleAttractMode`/`GetToggleAttractMode` (strings em `0x8201B840`/`0x8201B858`) devem controlar o vídeo; o ponto de registro não foi achado.
- O jogo já tem diálogos ImGui no host (`GraphicsOptionsDialog`, `superman_returns_app.h` `OnCreateDialogs`), utilizáveis para a tela de resultados.

## Como funciona (decidido)

1. **Entrada.** `--sr_benchmark=true` liga o modo e implica `sr_skip_intro`. O botão do launcher passa as configurações atuais mais essa opção.
2. **Espera da cena.** O modo considera que a cena começou no primeiro quadro em que o filtro final da cena (PS `B397B05F4CE5D5BB`) é desenhado depois do boot. Aquecimento de 5 s, depois medição.
3. **Sem interface.** Em cada quadro, depois do draw do filtro final, todos os draws seguintes do quadro são descartados até a apresentação. Só vale no modo benchmark.
4. **Janela de medição.** 60 s após o aquecimento (termina ~67 s após a cena começar, antes dos ~78 s do vídeo). Se durante a janela houver um quadro sem o filtro final (vídeo, carregamento), a rodada é marcada inválida e encerrada ali.
5. **Sem entrada do jogador.** Durante todo o modo, a entrada de controle e teclado que chega ao jogo é neutra.
6. **Resultado.** Ao fim: tela de resultados (ImGui) sobre a cena e arquivo salvo. Métricas a partir dos intervalos entre apresentações: FPS médio, mínimo, 1% low e 0,1% low, tempo de quadro p50/p95/p99 e máximo, número de quadros, duração, e "válido/inválido" com o motivo. Junto: API, GPU, resolução, escala, qualidade de sombra, MSAA, VSync, limite de FPS e versão do build.

## Decisões minhas (podem ser revistas)

- **Duração:** 5 s de aquecimento e 60 s medidos.
- **Arquivo:** `benchmarks/benchmark-AAAAMMDD-HHMMSS.json` ao lado do executável, com os tempos de cada quadro num `.csv` de mesmo nome.
- **Tela de resultados:** fica aberta até o usuário fechar (botão ou Esc/Enter), e então o jogo encerra. `--sr_benchmark_exit=true` fecha sozinho logo após salvar, para scripts, com código de saída 0 se válido e diferente de 0 se inválido.
- **Configurações:** o botão do launcher usa as configurações escolhidas, sem forçar VSync desligado nem limite; elas aparecem no resultado.
- **Áudio:** fica como está (a música do título toca).

## Deixado para quem construir

- Onde cortar os draws de interface em cada API: D3D12 no worker (`PrepareDraw`/draws do `Renderer`); no Vulkan, no front-end de captura ou no replay, onde for mais simples, desde que o quadro apresentado fique igual ao corte do D3D12.
- Como neutralizar a entrada (reaproveitar o caminho de entrada interno do jogo, como o `SR_BENCH_INPUT` usado por `tools/bench/bench_replay.ps1`, ou filtrar na leitura do controle).
- Como medir os intervalos de apresentação (a timeline `SR_FRAME_TIMELINE` e o log `guest fps` já existem como referência).
- Layout da tela de resultados.

## Verificação esperada

- Três aberturas seguidas na mesma máquina: nenhuma interface visível nos quadros medidos (dump de quadro no começo, meio e fim), nenhuma rodada inválida e FPS médio dentro de ±3% entre elas. Se o voo não se repetir e a variação passar disso, relatar com números antes de aceitar.
- Uma rodada com o título deixado parado até o vídeo, para provar que a detecção marca inválido se a janela for estendida além dos ~78 s.
- Se houver acesso a uma GPU dedicada (RTX 2060), conferir que o vídeo não chega antes do fim da janela.
