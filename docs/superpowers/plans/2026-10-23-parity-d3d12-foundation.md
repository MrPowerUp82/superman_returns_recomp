# D3D12 parity foundation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Medir capacidades no dispositivo D3D12 selecionado, separar o custo das constantes e produzir um frame de gameplay anotado por shader/recurso para mapear câmera e passes.

**Architecture:** Evoluir o renderer atual, D3D12 primeiro, conforme escolha e desenho aprovados. Esta é a primeira entrega de uma sequência de planos por subsistema; não representa RT/DLSS já implementados. Coletar diagnóstico sem alterar decisões de renderização.

**Tech Stack:** C++23, D3D12/DXGI, ReXGlue v0.10.0, Python standard library, build.cmd.

**Spec:** `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md` (aprovado).

## Global Constraints

- Preservar renderer/áudio e alterações locais anteriores; D3D12 primeiro, Vulkan depois.
- Instrumentação opcional; apresentar dados antes de mudanças de comportamento.
- Sem trabalho pesado concorrente com gameplay medido e sem testes longos em silêncio.
- Não inferir suporte DLSS pelo nome da GPU, câmera por uma matriz world-view-projection, nem passes por tamanho/hash isolados.
- Meta inicial: 1280×720, escala 1, MSAA 1, RT/DLSS/frame generation Off, 60 FPS reais, conforme spec.

## Review Focus

- Adaptador Intel presente junto da RTX: consultar o device efetivamente selecionado e publicar seu LUID.
- Consulta D3D12 falhando: manter HRESULT, sem converter falha em suporte ou ligar efeitos.
- Trigger antes de gameplay: escolher arquivo absoluto criado somente após confirmação visual; apenas um frame por execução.
- Log cortado ou múltiplos frames: parser rejeita trace incompleto e seleciona frame explicitamente.
- Shader runtime/ucode divergente: juntar pelos container hashes reais dos draws; conservar shaders desconhecidos.

## Sequência de planos do escopo aprovado

1. Este plano: base D3D12 e fatos de câmera/passes.
2. CPU/60 FPS e correções raster: plano delimitado pelos hotspots e recursos identificados aqui, sem patch cego de limitador.
3. Contratos de cena/temporais e DLSS SR: usar câmera/depth/HUD comprovados; motion vectors incluem objetos e deformações.
4. RT shadows e reflexos: extração/identidade/cobertura fora da tela, materiais e composição, depois menu integrado à pausa.
5. Vulkan: portar os contratos e validar suporte/sincronização separadamente.

Os planos 2–5 serão detalhados com os fatos das etapas anteriores; não substituir endereços desconhecidos por suposições. Continuar a execução dentro do escopo aprovado, sem pedir confirmação para cada plano rotineiro.

### Task 1: Capacidades, captura sob demanda e custos VS/PS

**Files:** criar `port/src/graphics/d3d12/device_capabilities.{h,cpp}`; modificar `port/CMakeLists.txt` e `port/src/native_renderer/native_renderer.cpp`.

**Interfaces:** `void LogDeviceCapabilities(ID3D12Device* device)` consulta e registra identidade, OPTIONS5/DXR tier, Shader Model (teto consultado 6.9, fallback por E_INVALIDARG até 6.0) e interface Device5. Não habilita efeitos. Adicionar `sr_native_trace_trigger_path` vazio por padrão ao trace one-shot existente. Acrescentar `vs_capture/ps_capture` ao detalhe existente e manter `vs_copy/ps_copy` como regiões pai, publicando `vs_write/ps_write` separadamente.

- [x] Ler init/trace/fill completos e conferir que as consultas não mudam o device, o estado do draw ou o conteúdo enviado.
- [x] Implementar consultas reais e logging com HRESULT; suporte DLSS fica explicitamente não consultado/SDK ausente.
- [x] Implementar trigger opcional sem polling quando vazio e sem repetir depois de um trace completo; erro de filesystem não encerra o jogo.
- [x] Separar obtenção/sanitização da escrita usando os timers opcionais existentes, preservando o caminho sem mirror e override PS.
- [x] Rodar `build.cmd` no diretório experimental após backup do EXE/DLL; esperado exit 0.
- [x] Integração real: log contém adaptador/capacidades, trace tem uma borda begin/end após trigger; contabilidade de constantes fecha. Diagnósticos reversíveis são verificados por build/integracão real, sem testes que apenas imitem chamadas de API.
- [x] Commit local somente dos arquivos deste incremento e seus resultados verificados; sem push.

### Task 2: Relatório offline de frame e semântica candidata

**Files:** criar `tools/analysis/native_frame_report.py`, `tests/tools/test_native_frame_report.py`.

**Interfaces:** `analyze(log_text: str, catalog: dict, frame: int | None = None) -> dict`. CLI `--log --catalog --output [--frame]`. Retorna frame, draws/shader pairs, eventos ordenados (bind/clear/resolve/tiling), constantes candidatas por estágio/shader e hashes desconhecidos. Não atribui passes confirmados nem altera `game_profile.h`.

- [x] Escrever testes: `test_requires_complete_trace`, `test_requires_selection_for_multiple_frames`, `test_joins_container_hash_and_keeps_unknown`, `test_retains_event_order_and_matrix_stage`; incluir linhas de outros sistemas entre begin/end e draw fora da janela.
- [x] Executar `python -m unittest discover -s tests/tools -p test_native_frame_report.py`; esperado falhar por módulo ausente.
- [x] Implementar parser para o formato real `trace begin frame N`, `trace draw pP #I ... vs HASH ps HASH ...`, `trace end of frame`; normalizar hashes, selecionar janela e conservar mensagens de eventos sem adivinhar semântica.
- [x] Executar os mesmos testes; esperado todos passarem.
- [x] Aplicar ao log real, cruzar o resultado com linhas originais e publicar candidatos e dúvidas, não hooks não comprovados.
- [x] Commit local do parser/testes após verificação.

### Task 3: Coleta real e mapa de próximos passos

**Files:** logs/artefatos ignorados; criar `docs/parity-d3d12-foundation-2026-10-23.md`.

**Interfaces:** dados da Task 1 → `analyze` da Task 2 → relatório de evidência para o próximo plano.

- [x] New Game, confirmar gameplay, aquecer ≥25 s, criar trigger; capturar início/fim via ferramenta de computador autorizada.
- [x] Registrar janela QPC/UTC e colher ≥6 blocos completos de 120 swaps, com flags iguais às rodadas anteriores salvo os novos diagnósticos.
- [x] Validar upload+reuso=draws, bytes=uploads×4096 e subtempos dentro dos pais; medir captura versus escrita, sem atribuir causalidade a FPS entre cenas diferentes.
- [x] Conferir shader reflection/HLSL/disassembly para matrizes e shadow samplers realmente observados; documentar entradas e papéis ainda não confirmados.
- [x] Encerrar processo próprio; conferir diff, hashes e build normal preservada. Revisão independente do incremento antes de encerrá-lo.
- [x] Registrar fatos que determinam a próxima otimização ou hook; não encerrar o objetivo total por concluir esta fundação.
