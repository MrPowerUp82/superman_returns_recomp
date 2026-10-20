# Contrato global no runtime Vulkan — 17/10/2026

Atualização: a entrada de benchmark e a comparação real foram realizadas em
19/10. Ver [resultados de runtime](native-vulkan-runtime-results-2026-10-19.md).
O ganho sintético não se traduziu em ganho perceptível no jogo.

O contrato global agora está conectado ao executável do jogo, sob
`SR_VULKAN_GLOBAL_RESOURCES=1`. O padrão continua sendo o contrato portátil.
Não há conclusão de 60 FPS nesta etapa.

## Implementação

- `Context` habilita e verifica descriptor indexing e os limites do heap.
- O serviço, a biblioteca de shaders e o cache de pipelines distinguem ABI
  portátil (1) e global (2), sem aceitar mistura de artefatos.
- Um heap por renderer mantém imagens 2D/3D/cube, buffers e samplers. Slots
  referenciam versões dos recursos, retidas até a conclusão da submissão.
- Constantes e streams são escritos na arena mapeada; layouts de declaração
  são compartilhados na submissão. Cada draw recebe um índice e o slot da
  tabela por push constants de 8 bytes.
- `GameRenderer` usa o heap no caminho global, preservando a ordem, passes,
  estados dinâmicos e os bindings de vértices exigidos pelo pipeline.

A biblioteca local contém 241 shaders globais verificados. É derivada dos
dados locais do jogo e não faz parte do pacote público.

## Falha encontrada no primeiro uso real

A integração inicial pulava todos os bindings de vértices no modo global.
Isso era incorreto: 87 dos 99 vertex shaders do corpus ainda declaram inputs
do input assembler. O resultado foi uma cidade reconhecível, com cores
corrompidas. Esse primeiro run é inválido como evidência de desempenho.

O teste de regressão passou a usar o input IA de posição no stream 7 junto
com os acessos globais. Sem a correção ele acusa VUID 04007 (binding 7 ausente)
e pixel 0 em vez de 64. A coleta dos buffers agora ocorre também quando o
pipeline global declara bindings IA.

Evidência local:

- `logs/native60_global_ia_red.log`: falha reproduzida antes da correção.
- `logs/native60_global_ia_fixed_gpu.log`: pixels globais e portáteis iguais,
  draws indexados e diretos, samplers 2D/3D/cube, versões simultâneas pendentes,
  ausência de nova escrita ao repetir um binding; validação ativa, zero erros.
- `logs/native60_global_ia_fixed_units.log`: 107 testes, zero falhas.
- `logs/native60_global_runtime_python.log`: 111 testes Python aprovados.
- `logs/native60_global_ia_fixed_game_build.log`: build incremental do jogo.

Executável corrigido SHA-256:
`B5C49A44CF4A72162A856584BC2E50FFDA957DB9C287C65A0300CDA024823DDA`.

## Validação no jogo e desempenho

`native60_global_ia_fixed_on` carrega os 241 shaders globais na RTX 2060.
A tela inicial voltou a exibir a cidade com cores normais. A medição comparativa
na cena jogável está pendente: os toques curtos da automação não avançaram o
título de maneira confiável, e foi solicitada entrada manual no gameplay.
O vídeo de abertura retornou ao título, sem confirmar um novo jogo.
Vídeo de abertura e tela de título
não contam como resultado de FPS de gameplay. O teste de pixels não cobre
todas as combinações de passes, resolves e aliases do jogo.

Configuração da comparação: Vulkan nativo, escala 1, MSAA 1, LSFG desativado,
limite solicitado 60 FPS e o mesmo executável para `GLOBAL=0` e `GLOBAL=1`.
Argumentos exatos e hash ficam em `logs/*_launch.json`. O tamanho de janela
solicitado é 1280×720 lógico; o log registra swapchain físico 1600×900 neste
Windows, portanto não se deve descrever a comparação como render físico 720p.

Snapshot local do executável, shaders, fontes e logs de teste:
`artifacts/native-renderer-performance/global_runtime_2026-10-17/manifest.json`.
A revisão adicional da guarda IA e do teste não encontrou achados acionáveis.
