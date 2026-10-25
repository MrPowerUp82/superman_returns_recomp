# GRAPHICS OPTIONS e pausa guest

Base: `adddef2`; implementação local sem commit. Referência visual: painel azul
translúcido fornecido pelo usuário. Este documento distingue evidência estática,
implementação e validação de runtime; não declara RT/DLSS completos.

## Contrato comprovado na imagem carregada

`logs/rt_menu_loaded_image_2410.bin`, base guest 0x82000000, tamanho 0x9E0000,
SHA-256 `629983cbecf6a607a5b1fceea9a4f7078cf554adc667dc0bb5c042c48de7d214`.
Strings: 0x8201ADE0 PauseGame, 0x8201ADEC UnPauseGame, 0x8201ADF8 IsPaused.
`sub_822AD978` registra esses nomes com callbacks 0x822A0F38, 0x822A0F70,
0x822A0FA8. Fontes recompiled: `.85.cpp:7294` (registro), `.41.cpp:6061`
(PauseGame), `.64.cpp:7795` (UnPauseGame), `.85.cpp:6960` (IsPaused).

PauseGame carrega contexto de BE32(0x82911460) e chama 0x8214F348(contexto,4).
UnPauseGame chama a mesma função com estado 3. IsPaused consulta 0x8214CEA0,
que é `lwz r3,72(r3); blr`, e compara o resultado com 4. A transição escreve
esse estado e executa ações adicionais: o hook chama a função original inteira
antes de observar. O host nunca escreve o estado de pausa ou chama UnPauseGame.
`ReadGraphicsGuestPause` usa um leitor checked big-endian no guest thread;
o UI thread consome somente o snapshot com sessão e contexto.

O wrapper XDK 0x828A7A38 move o output r4 para r5, passa flags zero e chama
XamInputGetState. A busca no código gerado identificou essa chamada como o
único uso direto desse import; não houve chamada a XamInputGetKeystroke.
O hook preserva o original, alimenta o menu com o estado não filtrado e zera
somente o gamepad retornado ao guest enquanto o painel captura input.
Serial de pacote muda também na entrada/saída da captura para não deixar uma
tecla presa em consumidores que consultam packet_number. Fora da pausa/menu,
o wrapper retorna imediatamente após o original.

## Interface integrada no host

Uma entrada nativa **GRAPHICS OPTIONS [G / Y]** é desenhada sobre a pausa
somente quando o estado guest verificado é 4. Ela abre por clique, G ou Y;
não se trata de uma tecla que presume pausa. O painel usa setas/D-pad/stick,
mouse e B/Esc para voltar à mesma pausa. O input permanece capturado até todos
os controles serem soltos. A entrada nativa não altera os assets EA APT nem
finge ter encontrado um índice GRAPHICS que não existe no menu original.

O painel persistente é instalado em OnCreateDialogs e destruído em OnShutdown
antes do drawer. Mudanças de contexto, saída da pausa e saída do guest invalidam
a sessão. O estado solicitado fica separado do relatório efetivo do renderer;
nenhum botão passa a On por consulta de capacidade ou teste sintético.

Os componentes de input/sessão possuem testes de retenção de teclas, troca de
contexto, leitura inválida, retorno à pausa e disponibilidade. A verificação
visual e de input no EXE integrado é uma etapa de runtime separada.

## Validação de runtime e persistência

`logs/bench_rt_menu_isolated_2410.log` e
`logs/bench_rt_depth_prefs_2410.log` passaram abertura Y, bloqueio de Start
durante o painel, fechamento B, reabertura, manutenção da mesma sessão de
pausa e retomada normal por Start. O modo explícito de teste
`SR_BENCH_GAMEPAD_ONLY=1` com `SR_BENCH_INPUT` isola teclado/mouse e usa o
controle sintético no caminho XInput real. Esses runs não validam teclado
físico nem desempenho. O teste anterior com G/Esc cobriu abertura/retorno,
mas revelou uma reabertura indevida por foco de navegação do ImGui; corrigida
para a entrada aceitar somente G, clique ou Y explícito. Y mantido ao entrar
numa nova pausa/contexto agora precisa ser solto antes de poder abrir.

Preferências ficam em `<config principal>.graphics.toml`, com cinco campos
versionados, validação integral e substituição atômica do arquivo. Nunca
salvar valores aplicados como intenção. Cvars `sr_rt_shadows`,
`sr_shadow_softness` (1 Low / 2 High), `sr_character_shadows`,
`sr_rt_reflections` e `sr_dlss` (off / quality) compartilham esse estado.
Config/ambiente/CLI explícitos prevalecem sobre o companion na inicialização.
Mudanças posteriores são mescladas por campo; habilitação interativa sem
suporte é rejeitada, preservando as outras preferências.

O fechamento normal da janela no run `rt_depth_prefs_2410` terminou às
13:54:30 e salvou softness=2 com RT/DLSS Off. O hook anterior a OnClosing
é necessário: o SDK termina o título e chama `_Exit`, sem chegar a
OnShutdown nesse caminho. O run `rt_visibility_2410` restaurou softness=2
sem override CLI, confirmando a persistência também após reinicialização.

Revisão independente estática das preferências/input não encontrou problemas
Important/Critical; não executou arquivo/GPU.

`logs/bench_rt_inline_menu_2410.log` confirma RT Shadows On/Low/High/Off
no jogo. High e Low executaram 2.241.641 invocações da função de raios;
Off registrou zero. Os estados aplicados foram publicados após o fence.
O teste de pausa Y/B/Start passou novamente, com fechamento normal às
18:55:59. A variante cobre PS8A6D+VS2C45; outras famílias preservam raster.
Character Shadows, RT Reflections e DLSS no gameplay continuam pendentes.
O teste não estabelece fidelidade geral nem desempenho de 60 FPS.
