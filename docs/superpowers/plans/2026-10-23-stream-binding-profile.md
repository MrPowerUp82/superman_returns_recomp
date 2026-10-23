# Stream binding profile — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Identificar o custo de BindVertexStreams, medido em 11,51 ms/quadro, antes de otimizar buffers ou declarações.

**Architecture:** Instrumentação opcional no renderer D3D12 atual. Subtempos distintos para setup, ApplyBuffer, binding IA, preparação de SRV, leitura de declaração e montagem de metadata. Contadores brutos fecham cada bloco de 120 swaps. Sem decisões novas de cache ou renderização.

**Tech Stack:** C++23, temporizadores CPU existentes, gameplay e timeline.

**Spec:** `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`, etapa CPU aprovada.

## Global Constraints

- Exigir CPU profile e `SR_NATIVE_STREAMS_PROFILE=1`; Off por padrão, sem relógios novos quando desabilitado.
- Não mudar dados, ordem dos bindings, fallback, descritores, reuso ou lifetimes.
- Separar chamadas de BindVertexStreams de draws efetivamente renderizados; contagens não são intercambiáveis.
- Não usar dados de título/introdução como gameplay, nem comparar FPS de câmeras diferentes causalmente.

## Review Focus

- Early returns/continues precisam fechar timers sem contar uma declaração que não foi lida.
- Contadores de planos/action não equivalem a uploads efetivos de ApplyBuffer.
- Tempos são distintos dentro de streams; nenhum pai/filho será somado em duplicidade.
- Descriptor allocation failure mantém comportamento anterior e aparece na contabilidade.
- Vulkan não executa BindVertexStreams: não descrever zeros como custo medido desse backend.

### Task 1: Diagnóstico e medição

**Files:** `port/src/native_renderer/native_renderer.cpp`, novo `docs/d3d12-stream-binding-profile-2026-10-23.md`, logs ignorados.
**Interfaces:** env `SR_NATIVE_STREAMS_PROFILE`, linhas `native CPU stream detail` e `native CPU stream totals` publicadas/zeradas a cada 120 swaps.
- [x] Acrescentar timers/contadores opcionais sem alterar decisões de renderização.
- [x] Build e CTest nativo; conferir detalhe Off ausente e flag sozinha sem habilitar perfil.
- [x] Gameplay confirmado/aquecido, ≥6 blocos completos, validar partições e subtempos, guardar imagens/manifestos/timeline.
- [x] Revisão independente, relatório da causa localizada ou da incerteza e commit local. Próximo patch de comportamento só depois dos dados.
