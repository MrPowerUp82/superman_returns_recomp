# P2: mira, poderes e câmera própria — 11/11/2026

## Sintomas relatados

1. Os poderes do P2 (visão de calor, sopros) miravam pela câmera do P1.
2. O retículo/alvo do P2 não correspondia à metade dele.
3. A câmera do P2 não girava/seguia corretamente fora dos casos já roteados.

## Método

Seguindo o playbook `coop-anything` (*split-screen*, "second player inside one
copy": apontar o campo do singleton para o P2 enquanto se chama o método do
próprio jogo), foi feito um mapeamento completo dos leitores do singleton de
câmera `0x8296D86C`:

- 138 instruções `lwz` desse singleton foram extraídas da imagem guest
  (`SR_DUMP_IMAGE`) e receberam, só durante a investigação, hooks de diagnóstico
  que registravam a cadeia de chamadas guest (back chain de `r1`) e se o ator
  P1 ou P2 estava na pilha. A instrumentação foi removida depois.
- Todos os leitores rodam na thread de simulação. A cadeia por ator é
  `8223EB08` (update do mundo, 5 fases) → `821C2468` (laço de entidades) →
  `821BE6C8` (update de um ator: FSM/Lua em +132, cérebro/locomoção em +96).
- O feixe dos poderes não roda dentro do update do ator: a entidade do sistema de
  poderes (`82203890`) chama `8221E748`, cujo componente tem o ator dono em +32.
  Sem alvo travado, `822172B8` (alvo −1) usa o ponto de mira da câmera, +528.
- O ponto de mira +528 é calculado por `821759B8` dentro da atualização completa
  do gerenciador (`821808F8`), que não roda para o grafo de câmera do P2. O único
  efeito externo dessa rotina é o vetor de direção em `*(0x82970174)+3104`.

Mapas de eventos de input identificados (id = eixo de `82181860`): 51/52
TARGETER X/Y, 53/54 LOCO MOVE/STRAFE, 55/56 LOCO CAM YAW/PITCH, 59 ACTIVATE POWER.
RT ativa a visão de calor.

## Correção

`port/src/coop_second_camera.cpp`:

- **Escopo de câmera por ator.** `REX_HOOK_RAW(sub_821BE6C8)`: enquanto o ator
  vivo do P2 (ID do port 1, mesma geração F2) executa o próprio update, o
  singleton aponta para o gerenciador do P2; é restaurado ao sair. P1, inimigos
  e NPCs continuam com a câmera do P1. Isso cobre locomoção relativa à câmera,
  variáveis de animação de mira (`CurAimDir`, `CurAimForward`,
  `CurRelativeToCamDir`), sistema de alvos, targeter e trocas de modo de câmera
  disparadas pelo FSM do P2.
- **Escopo do componente de poder.** `REX_HOOK_RAW(sub_8221E748)`: mesmo escopo
  quando o dono do componente (+32) é o P2.
- **Ponto de mira próprio.** Depois do transform do P2, copia a posição (+480)
  e chama `sub_821759B8` no gerenciador do P2, preservando o vetor global
  `*(0x82970174)+3104` do P1. O escopo só é aberto depois que existe um ponto de
  mira válido na geração atual.
- Decisão isolada em `CameraForActorUpdate` (`coop_camera_input_policy.h`),
  testada em `tests/native/test_coop_camera_input.cpp`. Os hooks de manifesto
  de 09/11 continuam válidos: dentro do escopo o registro já contém o
  gerenciador do P2 e eles o mantêm.
- Rejeição barata: sem P2 ativo, nenhuma leitura é feita; com P2 ativo, só a
  leitura do ID (+88) por ator, sem syscall.

Probe: o input sintético aceita gatilhos opcionais
(`probe.ps1 -Action Input -Port 1 -RightTrigger 255`); formatos antigos de 4 e 6
campos continuam válidos.

## Validação

- Testes nativos: 757 testes, 0 falhas (inclui os novos de política e gatilhos).
- Probe isolado (save 15%, Metropolis, D3D12 split), logs/capturas em
  `logs/coop_aim_20261111/`:
  - `p2yaw.png`/`p2fwd.png`: analógico direito do P2 gira só a câmera dele; P2 voa
    na direção da própria câmera e a câmera o segue.
  - `p2heat.png`: RT no P2 — o feixe vai ao centro (retículo) da metade do P2;
    na metade do P1 o feixe aparece cruzando a cena. `p1heat.png`: o inverso.
  - `p2target.png`: P2 com câmera inclinada para baixo, feixe no ponto mirado.
  - `rejoin.png`: remover/recriar o P2; o novo ator também mira pela própria
    câmera. Nenhum aviso de singleton alterado durante o escopo.

## Limites conhecidos

- O HUD continua compartilhado: o retículo é desenhado no centro de cada metade
  (correto para mira livre), mas o marcador de alvo travado (lock-on) e outros
  elementos do HUD ainda vêm do P1. Não testado com inimigos.
- O restante da atualização do gerenciador (`821808F8`: tremores de câmera em
  +4912, altura de terreno em +5008, listeners) não roda para o P2.
- Não validado com dois controles físicos, combate, morte/respawn, cutscenes ou
  transições de fase.
