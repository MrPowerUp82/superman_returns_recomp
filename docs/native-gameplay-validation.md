# Validacao da gameplay nativa

Validacao local em 2026-10-02, Intel UHD Graphics, build Release, renderer
`native`. O teste entrou em Start New Game, pulou a abertura, verificou o HUD
e manteve W pressionado por 12 segundos. Superman mudou de posicao na rua;
HUD, tutorial, minimapa e parte da cidade foram renderizados.

## Correcoes

- `sub_820F2810` grava o viewport em `device+12376` (`0x3058`): X, Y,
  largura e altura sao uint32; MinZ e MaxZ sao floats. O offset anterior
  `0x3090` apontava para outro estado do device. O trace passou de profundidade
  `0..0` para `0..1`. As leituras do ajuste de meio pixel tambem foram corrigidas.
- O motor chama diretamente `sub_82101A58`, que recebe device em r3, flags em
  r4, retangulo em r5, ponteiro para float4 em r6, Z em f1 e stencil em r8.
  A funcao publica `sub_82101998` converte D3DCOLOR e chama essa mesma rotina.
  O hook agora captura ambos os caminhos pela rotina compartilhada. A cor
  float4 e copiada antes da chamada original e preservada no comando do worker,
  inclusive valores HDR. Os clears de profundidade antes perdidos chegam ao D3D12.

## Evidencias Locais

- Build Release concluido; suite CTest nativa e testes Python de perfil passaram.
- `logs/bench_native_gameplay_clear_forward.png`: gameplay apos movimento.
- `port/out/build/win-amd64-release/logs/gameplay_clear_dump/output_1280x720.raw`:
  saida nativa aos 40 segundos, com superficies e resolves na mesma pasta.
- `logs/bench_results.csv`, rodada `native_gameplay_clear`: 20,5 FPS parado e
  22,5 FPS em movimento. Sao janelas curtas com dump/trace ativos; o dump
  interfere na janela parada. Nao representam um benchmark de desempenho estavel.

## Limitacoes

Ainda ha geometria deformada, iluminacao escura e draws sem pipeline. A rodada
nao valida combate, voo, progresso de missoes ou estabilidade prolongada.
Nao houve comparacao visual A/B com Xenos. A gameplay aparece e responde ao
movimento, mas a fidelidade visual permanece experimental.
