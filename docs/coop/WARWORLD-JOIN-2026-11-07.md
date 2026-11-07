# Co-op por F2 — 07/11/2026

## Comportamento final solicitado

A sessão iniciada com Cooperativo marcado prepara os recursos, mas começa
**desligada**. F2 solicita um spawn na posição atual do P1 durante a gameplay.
Outro F2 remove o P2 e retorna à tela inteira; reativar cria um ator novo perto
do P1. Não há spawn, reposicionamento ou recriação automática ao carregar a fase.
O nome legado sr_coop_autojoin permanece no preset para compatibilidade de CLI,
mas sua descrição e comportamento agora indicam o ciclo manual por F2.

## Implementação

O evento de janela ignora repetição de tecla e publica uma revisão atômica.
Spawn e remoção usam o contexto Lua P1 já validado. A intenção mais recente
consolida pedidos rápidos; falhas com ator parcial exigem limpeza antes de outra
criação. O grafo de câmera mantém os parâmetros de startup, invalida snapshots
por geração de sessão e faz um binding novo após reativar, mesmo se um ID for
reutilizado. O par de renderização em andamento termina; novos pares respeitam
estado ativo e geração. Não há novo hook nem fatal.

## Diagnóstico anterior e mudança de escopo

Nos logs de 07/11, o spawn inicial ocorreu perto de (-2156,1202,-159), antes de
P1 ser levado à arena perto de (5,3,-28); P2 ficou nas coordenadas anteriores.
O usuário confirmou que F2 funcionou, mas a tentativa de correção automática do
spawn não. Por sua orientação, essa heurística foi removida e a criação inicial
passou a depender exclusivamente de F2. Não se declara corrigido o spawn
antecipado: ele deixou de ocorrer automaticamente.

## Validação

Testes de ciclo de vida cobrem início desligado, primeira tecla, ciclos de
remoção/criação, espera por gameplay, falhas parciais e pedidos rápidos. Logs
locais em logs/coop_warworld_toggle_20261107. Os saves normais foram preservados;
o ensaio anterior usou a cópia do probe isolado.

Build final do jogo e publicação do launcher passaram. A suíte nativa terminou
com 738 testes e zero falhas; LauncherChecks terminou com zero falhas. O teste
novo verifica 100 iterações desligadas sem solicitar spawn e a primeira revisão
F2 como único gatilho. O executável final contém a mensagem de início desligado
e não contém a antiga rotina de relocação automática. Evidência em
default_off_build.log, default_off_launcher_tests.log e default_off_validation.json.
O F2 foi confirmado pelo usuário na sessão anterior; não se apresenta a tentativa
anterior de spawn automático como validação bem-sucedida do Warworld.
