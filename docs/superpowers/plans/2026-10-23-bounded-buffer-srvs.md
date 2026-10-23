# Bounded buffer SRVs — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Testar reuso de SRVs imutáveis de buffers residentes, reduzindo as 1.359 criações/quadro medidas sem reter um descritor para cada um dos ~28 mil buffers vivos.

**Architecture:** Cache LRU limitado, identificado pela geração única do recurso host. O renderer invalida a lease quando substitui o recurso e retira índices evictados usando sua fila existente com a fence atual. Metadatas já gravadas continuam protegidas, mesmo se houver evicção durante o mesmo quadro. Opção inteira `sr_native_buffer_srv_cache=0` por padrão, exige restart; experimento 4096, teto 8192.

**Tech Stack:** C++23, helper puro, D3D12, testes nativos e gameplay Off/4096.

**Spec:** `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`; dados `docs/d3d12-stream-binding-profile-2026-10-23.md`.

## Global Constraints

- Nenhum cache baseado só em endereço guest; gerações host novas quando o recurso muda.
- Conservar uploads, barreiras, formato/forma das views e fallback de alocação.
- Não liberar/reutilizar um índice antes da fence; manter pins de dumps e metadatas de draws anteriores.
- Off mantém o caminho anterior; sem promoção de padrão por ganho isolado.
- Reuso só elimina criação de view, não execução de draw ou alteração de vértices.

## Review Focus

- Evicção no mesmo quadro precisa proteger draws/metadatas anteriores.
- Falha de CreateCommittedResource não deixa uma lease apontando ao recurso velho.
- Reupload no mesmo recurso conserva sua geração; troca de recurso invalida a anterior.
- Recursos transitórios/câmera em movimento não podem fazer o cache crescer além do limite.
- Shader fetch usa a view realmente válida, inclusive após dump que divide a command list/fence.

### Task 1: Política e integração experimental

**Files:** novo `port/src/native_renderer/buffer_srv_cache.h`, `native_renderer.cpp/.h`, `tests/native/test_buffer_srv_cache.cpp` e CMake.
**Interfaces:** `Find(generation)->index/0`, `Insert(generation,index)->optional evicted lease`, `Erase(generation)->optional lease`; o helper não libera índices nem consulta GPU.
- [x] RED de reuso entre acessos, LRU, geração invalidada, churn limitado, zero/falha e composição com o recycler/fence; implementar GREEN.
- [x] Integrar gerações, invalidação na troca de recurso e retorno de eviction para RetireSrvIndex. Cache Off por padrão, ignorado em Vulkan.
- [x] Medir hits persistentes, evicções, tamanho e criações por bloco sem mudar as partições existentes.
- [x] Build, suíte nativa e revisão independente do cache/lifetime.

### Task 2: Comparação de gameplay

**Files:** novos manifestos/logs/imagens/relatórios e `docs/d3d12-buffer-srv-cache-2026-10-23.md`.
**Interfaces:** mesma build e flags, variando somente capacidade 0/4096; comparar custo SRV/criações e relatar validade de cena/imagem separadamente de FPS.
- [x] Rodar Off/On New Game com aquecimento, ≥6 blocos completos, hashes e metadatas iguais salvo opção.
- [x] Validar partições, ausência de falhas, limite, gameplay/voo; manter pendências de debug layer/câmera explícitas.
- [x] Documentar benefício ou ausência dele; commit local, sem promover padrão.
