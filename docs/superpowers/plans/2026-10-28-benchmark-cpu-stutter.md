# Benchmark CPU and stutter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Reduzir stuttering e trabalho de CPU no benchmark sem reduzir qualidade.

**Architecture:** Primeiro medir o executável atual em uma instalação isolada e identificar a causa dos picos. Alterar uma política por vez, preservar uma referência executável e comparar rodadas frias e aquecidas.

**Tech Stack:** C++20, Windows, D3D12, ReXGlue, PowerShell, Python, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-10-28-benchmark-cpu-stutter-design.md`

## Global Constraints

- i7-10750H, RTX 2060 confirmada pelo log; Nativo + D3D12, 1280×720, escala 100%, sombra 1, MSAA 1, FXAA/geração/VSync off, sem limite.
- Benchmark normal: 5 s de aquecimento + 60 s, sem alterar cena/simulação; guardar inválidas.
- Preservar binários e caches originais; caches frios em diretórios isolados. Não alegar cache frio do driver.
- Sem builds/testes concorrentes com medição; não publicar release.
- Aceitação: p99 -15%, CPU ocupada por quadro no alvo -10%, três rodadas válidas; ampliar para cinco pares se a variabilidade cobrir o ganho.
- Sem regressão consistente >5% nos lows ou >3% no FPS médio; conferir imagem e custo frio separadamente.

## Review Focus

- GPU híbrida: rejeitar rodada executada na Intel UHD (tarefa 1).
- Caches/preferências herdados: registrar argumentos, conteúdo e hashes; não chamar duas condições diferentes de equivalentes (tarefas 1/3).
- Timers de apresentação guest/host distintos: documentar âncoras e não inventar correspondência por número de linha (tarefa 2).
- Travada durante compilação/IO: conservar a cauda e separar frio de aquecido, sem excluir outliers (tarefas 1/3).
- Cache de recurso invalidado/animado: teste da invalidação e inspeção visual, incluindo smoke Vulkan se compartilhado (tarefa 3).

### Task 1: Referência reproduzível

**Files:** artefatos ignorados em `artifacts/benchmark-cpu-stutter/`; relatório final em `docs/benchmark-cpu-stutter-2026-10-28.md`.
**Interfaces:** consumir exe/DLLs de `port/out/build/win-amd64-dist`, ferramentas de `artifacts/release/stage`, `game/`; produzir manifesto por rodada, log, JSON/CSV nativos e CSV de CPU por thread.

- [x] Criar branch `codex/benchmark-cpu-stutter` na árvore limpa atual para reaproveitar dependências/builds locais; registrar esta escolha no ledger.
- [x] Preservar exe/DLLs e suas dependências em instalação de referência; calcular SHA-256 e registrar bibliotecas e caches presentes.
- [x] Executar uma rodada fria dos caches locais controlados e três aquecidas, com `--sr_benchmark=true --sr_benchmark_exit=true`, em sequência; verificar GPU, argumentos, validade e duração.
- [x] Usar `tools/analysis/sample_thread_cpu.ps1` durante as rodadas, salvar intervalos com timestamps. Guardar ambiente/hardware/processos/alimentação disponíveis.
- [x] Resumir p50/p95/p99/máximo, lows, picos >33,3/50/100 ms por minuto e CPU nas mesmas janelas, sem descartar picos.

### Task 2: Diagnóstico causal

**Files:** leitura de `port/src/graphics/frame_timeline.h`, `port/src/native_renderer/native_renderer.cpp`, `pipeline_cache.cpp`, `guest_cpu_profile.cpp`, `port/src/title_benchmark.cpp`; artefatos na pasta da tarefa 1.
**Interfaces:** consumir referência e resultados; produzir hipótese sustentada por picos, threads e regiões medidas.

- [x] Executar rodada diagnóstica com `SR_FRAME_TIMELINE` e perfis relevantes; aceitar overhead somente nesta rodada.
- [x] Correlacionar timestamps/fases/presents; identificar trabalho de CPU, waits e GPU sem somar tempos aninhados.
- [x] Investigar código e fonte oficial do Canary fixada por SHA apenas onde a hipótese exigir; REA somente para perguntas restantes sobre binário/runtime.
- [x] Se faltar visibilidade, adicionar a menor instrumentação desligável, com teste de associação/limites antes da implementação; repetir diagnóstico.
- [x] Registrar causa mais cara comprovada e escopo exato da correção. Se nenhuma causa for comprovada, ampliar medição em vez de alterar políticas por palpite.

### Task 3: Correção e A/B

**Files:** o hotspot exato é decidido pela tarefa 2; candidatos delimitados: `native_renderer.cpp`, `pipeline_cache.cpp/.h`, políticas locais e testes correspondentes em `tests/native/`; runtime compartilhado só com evidência de necessidade.
**Interfaces:** consumir hipótese da tarefa 2; produzir variante executável isolada com fallback A/B quando viável e prova de validade da mudança.

- [x] Antes da alteração, atualizar o ledger com arquivos/funções, invariante e teste exatos. A seleção adaptativa é intencional: o desenho proíbe escolher a otimização antes da medição.
- [x] Escrever teste do comportamento/invariante que falha na implementação atual; executá-lo e registrar a falha esperada.
- [x] Implementar somente a correção necessária, sem alterar benchmark/qualidade/simulação para melhorar números.
- [x] Executar o teste e a suíte pertinente; compilar em saída separada e guardar hashes. Testar memória se tocada e smoke Vulkan se compartilhado.
- [x] Rodar três pares aquecidos intercalados e comparação fria equivalente; ampliar para cinco se inconclusivo; rejeitar regressões segundo a spec.
- [x] Conferir visualmente início/meio/fim em rodada separada, sem contaminar os tempos de aceitação. Manter/reverter a mudança conforme evidência.

### Task 4: Evidência e revisão final

**Files:** `docs/benchmark-cpu-stutter-2026-10-28.md`, este plano, ledger ignorado `.superpowers/sdd/2026-10-28-benchmark-cpu-stutter/progress.md`.
**Interfaces:** consumir artefatos de todas as rodadas; produzir relatório reproduzível e diff revisado.

- [x] Escrever resultados, causa, mudanças, hashes, testes e limitações; separar observação de inferência.
- [x] Revisão independente do diff e dos critérios de aceitação; corrigir achados importantes e repetir apenas verificações afetadas.
- [x] Confirmar diff limpo de erros de whitespace, testes relevantes e estado Git; registrar commits locais sem publicação.

## Execução

Execução sequencial nesta sessão: o usuário aprovou explicitamente o desenho e o início das medições. A execução local evita trabalhos concorrentes contaminando o benchmark; uma revisão independente ocorre ao final. O plano foi conferido contra todas as seções da spec. As tarefas 2/3 são adaptativas por exigência do diagnóstico, não autorizam reimplementar amplamente RenderWare.

## Resultado da execução

Concluída a otimização delimitada de alocação de buffers; relatório em docs/benchmark-cpu-stutter-2026-10-28.md. Os três pares atenderam às metas propostas, e um par adicional em primeiro plano também melhorou. Ainda existem travadas e uma diferença de desempenho dependente das condições de foco/sistema; sua causa não foi atribuída. Isso está registrado, sem alegar que todos os gargalos do jogo foram resolvidos. A série de aceitação permaneceu sem perfis/capturas; os testes visuais e diagnósticos foram separados. Não houve publicação.
