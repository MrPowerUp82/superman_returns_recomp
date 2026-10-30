# Texturas usadas pelos shaders — 29/10/2026

Implementada a primeira candidata da [pesquisa no Xenia Canary](xenia-canary-opportunities-2026-10-29.md): evitar preparação de slots que o par VS/PS não pode consultar. O caminho está ligado por padrão em D3D12 e na captura/replay Vulkan. Para comparar com o comportamento anterior, reinicie com:

```text
--sr_native_used_texture_mask=false
```

## Implementação

- `graphics/guest/shader_texture_usage.h` lê o container original e o microcódigo big-endian com limites explícitos. Analisa todos os blocos EXEC do prefixo de control flow, incluindo os caminhos condicionais, jumps e loops suportados. Não executa o shader nem avalia predicates.
- `ShaderCapture` publica `{mask, known}` no snapshot imutável do registro. A máscara funciona com bibliotecas existentes e tradução runtime, sem mudar o formato do cache.
- A política combina VS e PS, inclusive quando os slots do dispositivo estão invertidos. Shader desconhecido, stage duplicado, VS ausente, container malformado ou opcode não suportado mantêm todos os slots. PS realmente ausente permite o draw de profundidade; ausência de captura de um PS ligado não permite filtragem.
- D3D12 omite o lookup/sampler dos slots não usados e escreve explicitamente seus descriptors nulos (2D=0, 3D=1, cube=2, sampler=0). O cache de bindings e a revalidação por quadro continuam atendendo slots que voltarem a ser usados.
- Vulkan calcula a máscara no mesmo instante em que captura os ponteiros/identidades dos shaders. O comando leva essa máscara até o replay, que zera os fetches omitidos. Não existe consulta nova a estado vivo no executor neutro.
- Shaders host substituídos por diretório externo, variantes D3D12 RT/motion/coverage e diagnósticos que dependem dos bindings completos conservam o caminho anterior. A filtragem não reduz a resolução nem altera o shader traduzido.

O log D3D12 inclui `native: texture usage masked_draws=... skipped_slot_preparations=...`. O resumo de captura Vulkan inclui `skipped_slot_preparations`. São contagens de iterações omitidas, inclusive slots vazios; não são medições de tempo economizado.

## Testes e revisão

| Verificação | Resultado |
| --- | --- |
| Decoder puro | 21 testes, Clang e MSVC, sem falhas |
| ASan/UBSan | 1.558.689 chamadas, incluindo truncamentos, mutações e entradas aleatórias, sem diagnósticos |
| Corpus do jogo | 241/241 máscaras conhecidas e exatas, 99 VS + 142 PS, 106 máscaras vazias; nenhum slot faltante ou extra |
| Suíte nativa integrada | 600 casos sem falhas; CTest 5/5 |
| Suíte Vulkan | 108 casos sem falhas; CTest 1/1 |
| Runtime de tracking | CTest 4/4 |
| Build Release do jogo e fixtures Vulkan | Passaram |
| Revisão independente | Sem defeitos acionáveis na análise e integração examinadas |

O decoder teve RED com 49 checks falhando antes da implementação. A regressão de replay reproduziu três checks falhando antes de zerar fetches omitidos. Os testes incluem união VS/PS, slot 31, stage invertido, fallback desconhecido, PS ausente versus não reconhecido, máscara vazia e reativação sem alterar um pacote pendente.

Teste de GPU com cena congelada:

```text
build/tests-vulkan/sr_vulkan_resources_test.exe --texture-mask
```

Ele usa o decoder de pacotes de produção, os descriptors Vulkan e um resolve real. Compara os 1.024 bytes dos pixels de dois draws idênticos, um com filtragem e outro sem; depois muda o resolve de vermelho para verde, reativa um slot omitido e confere os pixels após o fence. Todas as quatro imagens corresponderam aos bytes esperados. O teste também está disponível no CTest quando `SR_VULKAN_GPU_TESTS=ON` e a fixture de contrato é construída.

A primeira versão dessa fixture precisou de duas correções próprias: linkar o suporte de replay também no executável global que inclui o arquivo de fixtures, e inicializar explicitamente os bytes sintéticos do `WorkBatch` (seu allocator evita zerar memória por padrão). Ambas foram corrigidas antes da execução final. Não foram mudanças no comportamento do jogo.

A camada de validação Vulkan não estava disponível neste ensaio; houve execução/readback na RTX 2060, mas não validação dessa camada. O ensaio congelado usa shaders e geometria próprios de teste, não uma captura de uma fase inteira. Não houve comparação visual alinhada de todas as cenas do Superman.

Evidências locais: `logs/texture_mask_{native_green,release_final,vulkan_build,frozen_gpu}.log`; decoder/corpus/sanitizers em `.tools/shader_texture_usage_decoder_20261029/verification.txt` e `corpus_report.json`.

## Medições exploratórias no jogo

RTX 2060, 1280×720, escala 1, MSAA 1, sombra 1, sem frame generation, limite zero e VSync desligado. Benchmark embutido headless, 5 s de aquecimento + 30 s medidos. Perfis CPU ligados. Cada par usa o mesmo executável; a cena/câmera varia entre aberturas.

| API / par | Máscara Off, FPS | Máscara On, FPS |
| --- | ---: | ---: |
| D3D12 / 1 | 28,87 | 22,12 |
| D3D12 / 2 | 35,88 | 37,88 |
| D3D12 / 3 | 28,18 | 47,24 |
| Vulkan / 1 | 19,10 | 23,33 |

Média aritmética dos três D3D12: **30,98 Off / 35,75 On**. O primeiro On teve um quadro de **22.160 ms**, contra máximos de 440 e 110 ms nos outros On. O watchdog capturou o worker em `NtQueryVirtualMemory → VirtualQuery → rex::memory::QueryProtect → tracking API`; o perfil incluiu essa pausa no lookup de texturas. Isso localiza a pausa observada, mas não estabelece por que ela ocorreu nem exclui uma interação com a nova ordem de acesso. O runtime arma watches com consultas de proteção por página; essa etapa não foi alterada aqui.

As oito rodadas foram aceitas pelo benchmark e terminaram com exit 0. Isso não significa ausência de stalls: há pausas e dumps do watchdog também em Off. As rodadas D3D12 e Vulkan usaram builds distintas apenas pelo ajuste final de bypasses diagnósticos e publicação da máscara junto dos shaders na captura Vulkan. Os três pares D3D12 usaram SHA256 `73A7D354C6019B6268FD824AE9ABB442C1D3E2C91EF46B2331989C4E9AB0DBEB`; o par Vulkan e a build final usam `908E7F9B9E6A3F7D5D1745B75DAE868ABB0E69AF7A72D133F87B689B9FAB015B`.

O último log D3D12 On registrou 25.822.587 preparações de slot omitidas; Vulkan On registrou 73.507 na última janela de captura. Há evidência de redução do trabalho de bindings, porém **as diferenças de FPS não comprovam ganho causal ou estável**: as cenas variam, houve um stall extremo e o Vulkan só tem um par. A otimização não resolve todos os gargalos e não demonstra 60 FPS sustentados.

Logs por rodada: `logs/texture_mask_<api>_<par>_<true|false>_{game,stdout,stderr}.log`. O resumo com caminhos dos oito JSONs, p50 e maior quadro está em `logs/texture_mask_20261029_summary.json`. Reprodução local: `.tools/bench_texture_mask_20261029.ps1`; o script preserva o executável entre os modos e não encerra processos preexistentes.
