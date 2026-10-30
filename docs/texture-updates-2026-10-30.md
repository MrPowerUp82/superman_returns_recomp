# Atualizações de texturas — 30/10/2026

Implementação do quinto candidato da [pesquisa Canary](xenia-canary-opportunities-2026-10-29.md).
Objetivo: reduzir recriações de recursos compatíveis e transferências de
subresources sem mudança, preservando o conteúdo ainda usado pela GPU.

## Critérios de correção

Uma nova versão não pode sobrescrever pixels usados por comandos pendentes.
Compatibilidade inclui dimensões, formato, layout de mips/slices e view, não
apenas endereço guest. Reutilização exige prova de lifetime; ausência de prova
mantém alocação e upload completos. Baselines de CPU, quando usados, têm
orçamento limitado e igualdade exata dos bytes, sem confiar em hash isolado.

O flag de comparação é `SR_NATIVE_TEXTURE_UPDATES=1` (experimental, default Off).
`=0` restaura o caminho anterior. Requer reiniciar o jogo.
O decoder completo e os texture mods continuam sendo aplicados; economizar
transferências não deve ser apresentado como decode seletivo sem evidência.

## D3D12

Recursos compatíveis e com último uso concluído podem conservar alocação e SRV.
Compatibilidade compara resource desc, formato da view, component mapping e
dimensão. Fence de device removal (`UINT64_MAX`) nunca autoriza sobrescrita.
Uso no frame corrente bloqueia reuso mesmo após uma submissão de debug.
Hits do cache de slots renovam o uso, e o fence final cobre os comandos do frame.

Se a versão anterior ainda estiver em uso, é criada outra alocação. Mips/slices
sem mudança são copiados pela GPU da versão antiga; somente os alterados recebem
upload de CPU. Barriers protegem os leitores e restauram o estado da versão
antiga. Recurso e descriptor antigos são retidos até o fence da submissão final.
Layouts sem cobertura completa e volumes conservam a transferência completa.

Snapshots só são admitidos após a primeira atualização da textura. O ensaio
inicial que os admitia na criação encheu os 64 MiB com conteúdo estático e não
registrou reusos; esse comportamento foi corrigido antes do build final.
Snapshots guardam até 8 MiB de pixels por textura, com orçamento total de
64 MiB contabilizando dados e metadados. Não são provas de memória guest:
descrevem os bytes decodificados enviados à GPU. Tracking/hash continuam com
suas próprias regras; snapshot ausente ou incompatível exige upload completo.

## Vulkan

Reuso só ocorre quando a submissão anterior foi aposentada e o mapa é o único
dono do recurso. Aliases, descriptors, holders externos e image-owner chains
impedem sobrescrita, assim como imagens de resolve. Layout, formato, swizzle,
extents, regiões e níveis precisam coincidir exatamente com o baseline.

Regiões iguais são omitidas; as alteradas são compactadas em staging e copiadas.
Uma versão nova com pixels idênticos pode não transferir bytes. Imagens
pendentes ou com outros donos continuam sendo substituídas por novas imagens
completas. O caminho respeita uploads gravados em outra command buffer.
Snapshots são limitados a 8 MiB por recurso e 64 MiB no processo, inclusive
snapshots retidos por leases; falta de baseline conserva o fallback completo.
Também são admitidos apenas quando o ID já tinha uma versão anterior: a primeira
versão de cada recurso estático não ocupa o orçamento.

Os contadores chamados `mips_*` contam regiões/subresources: em cubemaps ou
slices podem incluir múltiplas regiões para um mesmo nível de mip.

## Validação efetiva

- Native: **618 testes**, sem falhas; CTest **5/5**. As novas verificações
  exercitam comparação exata, ranges inválidos, orçamento e fences.
- D3D12 GPU: readbacks RGBA8 e BC1, mips/faces, versão pendente com cópia e
  reuso concluído passaram. A fixture usa os helpers de comparação/fence de
  produção e reproduz as operações GPU; não instancia o Renderer completo.
- Vulkan CPU: **109 testes**, CTest **1/1**.
- Vulkan GPU: `--texture-updates` e `--texture-updates-separated`, com flag
  **0 e 1**, passaram nas verificações do executor e na matriz do coordenador.
  A atualização controlada transferiu **16 bytes**, um mip e pulou dois,
  reutilizando a imagem já concluída. Versão idêntica transferiu zero bytes.
- Fixtures Vulkan preservaram pixels antigos ainda referenciados, recusaram
  reuso com leases/aliases e trocaram a alocação quando o layout mudou.
- A matriz final do coordenador também confirmou fallback acima do limite:
  textura de 2048×1024 RGBA8 (8 MiB mais metadados) manteve upload completo e
  alocações distintas. Pixels inicial/final foram lidos sem exceder o readback.
- A fixture Vulkan exige ausência de snapshot na primeira versão e admissão
  após a primeira mudança, conforme a política final.
- Runtime/tracking/XMA: CTest **6/6** passou após o build integrado.
- Revisão independente estática sem achados P1/P2 nos novos caminhos,
  incluindo retiros de recurso/descriptor e fences de debug no D3D12.

Layers Vulkan estavam indisponíveis; houve readback em hardware, sem validação
de API. Os testes não comprovam equivalência visual de todas as cenas. Áudio
após decolar e 60 FPS estáveis continuam sem confirmação geral.

Reprodução: `tests/native` inclui o target `sr_d3d12_texture_updates_gpu`;
`SR_NATIVE_GPU_TESTS=ON` registra-o no CTest. `SR_VULKAN_GPU_TESTS=ON` registra a
matriz Vulkan. Não são necessários para executar o jogo normalmente.

Build Release final de medição: SHA256
`5786F7BC0EF65A222C54BCE1FA9A303C349599B93617E6C469CC2217384F55E9`.
Runtime preservado: `3D4681E7726FA28408CC68863D2713C2FB95F8FAC9F80DB1DB68EB03AFC7BB4A`.
Logs do coordenador: `logs/texture_updates_*`; runner local:
`.tools/bench_texture_updates_20261030.py`. Flags 0/1 usam o mesmo executável,
5 s de aquecimento e 30 s de medição, 1280×720, sem VSync/limite/LSFG.

O primeiro par D3D12, antes do ajuste de admissão, usou o build
`0EEFC0B3FE50571E437E34174BAC587FF98DD07A009A24394CEFB23CC5AD45D8`:
37,8 FPS Off e 39,1 On, mas zero reusos e zero mips omitidos na última amostra,
com `snapshot_bytes=67108728`. Esses números não comprovam benefício da
otimização e não foram combinados com o build final.

## Medições no build final e padrão

| API | Off FPS médio | On FPS médio | JSON Off / On |
| --- | ---: | ---: | --- |
| D3D12 | 37,7614 | 32,7990 | `benchmark-20261030-081139.json` / `benchmark-20261030-081230.json` |
| Vulkan | 23,4 | 24,8 | `benchmark-20261030-081322.json` / `benchmark-20261030-081416.json` |

Ambos os pares terminaram com exit code zero. Nas amostras do jogo com On, o
máximo registrado de `resource_reuses` e `mips_skipped` foi zero. No D3D12, o
orçamento de snapshots terminou zerado após a política de admissão, em vez de
64 MiB ocupados por texturas estáticas. Os logs de watches também mostraram
janelas sem reloads. As cenas medidas não exercitaram os benefícios demonstrados
pelas fixtures de atualização; os valores de FPS não comprovam ganho causal.

O padrão final permanece **Off nas duas APIs**. É possível testar com
`$env:SR_NATIVE_TEXTURE_UPDATES='1'` antes de iniciar o jogo; `=0` ou ausência
da variável mantém o caminho de uploads completos. Não foram mudados os
padrões das otimizações anteriores. Os cinco candidatos têm implementação;
isso não significa que todos tenham benefício comprovado ou ativação padrão.
