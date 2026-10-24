# Fundação D3D12 e primeira evidência de shadow map empilhado

Desenho aprovado: `superpowers/specs/2026-10-23-native-renderer-parity-design.md`. Branch local `codex/native-parity-d3d12`. O incremento adiciona consultas de capacidade e diagnósticos, não implementa RT/DLSS/menu. Áudio continua adiado.

## Build e artefatos

`build.cmd` com SDK de tracking On e saída `win-amd64-write-tracking` passou. Backup anterior: `artifacts/native-renderer-performance/parity_foundation_20261023_previous/`. Log: `logs/parity_foundation_20261023_build.log` (há warnings CRT). Os quatro testes de contrato do runtime passaram em 4,03 s. O relatório offline tem oito testes, incluindo rotação numérica e lacuna nas partes do log, que passaram após RED→GREEN; logs dos testes em `logs/parity_foundation_20261023_*red.log` e `*_parser_green.log`.

A revisão independente encontrou uma falha P2 de evidência: concatenar partes rotacionadas com numeração faltante podia aceitar um trace parcial como completo. O teste `test_rejects_missing_rotated_part_inside_trace` reproduziu a falha, que foi corrigida preservando um marcador de lacuna e rejeitando-o dentro de um trace ativo. Lacunas anteriores ao frame completo selecionado não invalidam esse frame. A suíte final ficou 8/8; o frame real permaneceu 3.017 draws depois da correção. Não houve outros achados acionáveis nessa revisão.

EXE medido: SHA256 `01EF67E25EED2398E3AAB8F8CAC1090975D1FBBE43A2079853E3A6D8EFC4814B`. Manifesto completo de EXE/DLL/args: `logs/parity_foundation_20261023_game1_launch.json`. A build normal não foi substituída.

## Capacidades reais do adaptador

O novo módulo `graphics/d3d12/device_capabilities.cpp` consulta o device que o provider selecionou. Esta execução registrou:

- NVIDIA GeForce RTX 2060, LUID `00000000000167DD`, vendor `10DE`, device `1F15`, VRAM dedicada reportada 5.955 MiB.
- Consulta de adaptador e OPTIONS5 com HRESULT `00000000`.
- `raytracing_tier=12` (valor bruto); `ID3D12Device5` disponível, HRESULT `00000000`.
- Shader Model retornado 6.8, HRESULT `00000000`; teto consultado 6.9, com fallback por E_INVALIDARG em runtimes antigos.
- RT shadows/reflections e DLSS continuam sem integração. O SDK DLSS não foi inicializado nem seu suporte consultado.

Não converter capacidade de hardware em afirmação de efeito ativo. Referências da consulta: [OPTIONS5](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options5) e [feature support Microsoft](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3dx12_check_feature_support.h).

## Perfil e janela

New Game observado visualmente, aquecimento adicional de 26 s, trace disparado por arquivo absoluto através do novo `sr_native_trace_trigger_path`. Vazio por padrão, ele não faz polling; é one-shot por execução e não usa exceções de filesystem. O arquivo permaneceu presente: houve exatamente um begin/end.

O trace durou de 17:46:22,786 a 17:46:25,012 (horário local). O logger rotacionou em 5 MiB, repartindo o frame em `.2.log`, `.1.log` e `.log`. A primeira tentativa de analisar somente o arquivo atual falhou corretamente por não haver frame completo. O parser foi corrigido com um teste de regressão que une arquivos numerados em ordem cronológica numérica, sem tocar nas referências ou descartar a validação das bordas.

Depois do trace, foi iniciada outra janela, sem trace ativo: 96,53 s, nove blocos completos de 120 swaps e 1.166 quadros completos de timeline (margem 250 ms nas bordas). Todos os 35 blocos publicados na execução passaram na contabilidade de uploads/reusos/bytes e nos subtempos dentro dos pais. Nenhuma falha de alocação. Sem build/testes durante a janela; houve consultas curtas aos logs ao investigar a rotação. Câmera de idle e tráfego variaram.

| Medida | Média ms/quadro |
| --- | ---: |
| UploadConstants | 25,002 |
| VS obtenção/sanitização | 3,542 |
| VS escrita no upload | 0,759 |
| PS obtenção/sanitização | 2,075 |
| PS escrita no upload | 0,329 |
| Slots, aninhado | 11,294 |
| Lookup de texturas, dentro de slots | 6,490 |
| Hash residual, dentro do lookup | 0,410 |
| BindVertexStreams | 11,270 |

Obtenção/sanitização VS+PS: **5,616 ms**; escrita: **1,087 ms**. Isso distingue o caminho `Pm4Mirror::FloatConstants` da escrita GPU, mas o tempo de obtenção inclui checks/cache; não atribuir tudo à execução aritmética da sanitização sem medir seu código.

Timeline: intervalo 79,7 ms, worker 68,48 ms, GPU real 6,89 ms. O perfil detalhado e a variação de cena impedem comparação causal de FPS com rodadas anteriores. 60 FPS não foram atingidos.

Dados: `logs/parity_foundation_20261023_game1_{window,end,blocks}.json`, `logs/timeline_parity_foundation_20261023_game1_window.{json,txt}`, CSV original e capturas `_trace.jpg`/`_end.jpg`. O processo próprio foi encerrado depois da coleta.

## Frame e shaders

`native_frame_report.py` identificou o frame **2816**, após guest swap 2816: **3.017 draws, 69 pares de shader, 104 shaders distintos, zero desconhecidos** ao juntar pelos container hashes do catálogo. `roles_confirmed=false`: o relatório conserva eventos em ordem e candidatos de reflection, sem preencher `game_profile.h` por heurística.

O par dominante foi VS `C7C0E381A3C9B79E` / PS `DD925AEBAD3D08B7`, 1.113 draws. A reflection do VS inclui `shadowMapCasterPass_Thickness_Bias`, `shadowMapOffsetScales` e `shadowMapU/V/Z`. O par VS `2C45BFFE76D77398` / PS `8A6DD1D52752FAD6`, 771 draws, declara VS `gViewProjectionXform` c0–c3 e `gViewInverse` c5–c7; são candidatos concretos de câmera que precisam de valores/transformações verificados.

O frame mostra grupos 640×640 resolvendo repetidamente para `1E820000` antes da superfície de cena 1280×720. A transição de superfície, as famílias de shader e seus parâmetros corroboram uma região de shadow casting. Ainda não há hook de passe confirmado ou referência visual do console.

## Achado novo: binding de shadow map empilhado

O trace registra no slot 15 do receptor:

```
fetch 85000002 1E820458 0C4FE27F 01281400 00000003 00000200
fmt 24 srv 3 (resolve 640x640 dxgi 37)
```

De acordo com os campos de `include/rex/graphics/xenos.h` do SDK, `fetch[1].stacked=1`, tamanho **640×640×4 camadas**, DataDimension `k2DOrStacked=1`. O mesmo header distingue a dimensão da instrução: uma textura empilhada é armazenada como 2D no fetch, mas acessada por `tfetch3D`.

O HLSL traduzido real de `8A6DD1D52752FAD6.ps` lê `samp_depth_Texture3DDescriptorIndex` no slot 15 por três chamadas `tfetch3D`, enquanto `UploadConstants` decide os heaps somente pela dimensão do fetch: com dimensão 1 coloca o SRV no heap 2D e o índice 1 no heap 3D, que é o descritor nulo 3D criado na inicialização. A declaração de shader é **Texture3D**, não Texture2DArray. A reflexão do DXIL compilado também confirmou `g_Texture3DDescriptorHeap`, dimensão 3d, espaço1, e o índice do slot 15 no byte 124 do shared block (`logs/parity_foundation_20261023_receiver_dxil.txt`). O resolved resource atual, porém, foi criado como textura 2D de uma camada. Esse contrato incompatível está comprovado para este draw; não depender da mensagem `texture decode failed` para detectá-lo.

Além disso, `Resolve()` captura `level` e `slice` em `WorkCmd.u[6/7]`, mas o executor D3D12 não encaminha esses argumentos para `ExecResolve()`, e sua criação de recurso usa DepthOrArraySize=1. Os valores efetivos desses argumentos não estão no trace atual: precisam ser registrados antes de escolher o mapeamento/cópia das camadas.

Isso explica por que as rodadas anteriores puderam ter zero mensagens de rejeição stacked e ainda não provar suporte: esta textura foi encontrada no cache de resolve, que antecede o caminho de decode/rejeição. Não é prova de que toda divergência visual do jogo venha desse caso, nem de fidelidade ao console.

### Próxima mudança justificada

Registrar level/slice e fetch de destino por resolve no próximo frame; verificar coordenadas Z reais/semântica da instrução. Implementar o armazenamento e binding que combinem com o shader observado, atrás de opção Off. Não ligar um Texture2DArray a uma declaração Texture3D. O patch precisa preservar camadas, resolves parciais, sampler/filtragem e lifetime, com testes de seleção/limites e comparação de gameplay. A separação de custos também justifica um experimento posterior na obtenção/sanitização, sem prometer que seus ~5,6 ms resolvam o orçamento inteiro.
