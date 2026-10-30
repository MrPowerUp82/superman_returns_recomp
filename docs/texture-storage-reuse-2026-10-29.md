# Compartilhamento de texturas entre samplers — 29/10/2026

Segunda implementação da [pesquisa do Xenia Canary](xenia-canary-opportunities-2026-10-29.md).
O renderer nativo agora pode compartilhar armazenamento quando fetches diferem
somente no sampler, evitando decodificar, enviar e manter cópias dos mesmos
pixels para cada configuração de filtro/clamp/LOD bias. **Ativo por padrão no
D3D12; experimental e desligado por padrão no Vulkan**, após queda repetida
de FPS nas medições desta máquina.

## Identidade e invalidação

`port/src/graphics/guest/texture_storage_identity.h` remove apenas 30 bits:
clamp U/V/W, mag/min/mip/aniso, filtros de volume e LOD bias. Todos os outros
bits permanecem exatos, incluindo limites de mip, endereços base/mip, formato,
endian, swizzle, sinais/gamma, dimensões, pitch, tiling e campos desconhecidos.
Fetches que não são texturas não são normalizados.

D3D12 usa essa identidade na tabela de armazenamento, compara os seis dwords
após o hash e conserva o fetch original da entrada. Colisões continuam
aposentando recursos/SRVs. Os bindings de slots e o cache de samplers recebem
os fetches originais. A entrada compartilhada conserva ranges, watch, provas
e versão do recurso; não há nova exceção às regras de tracking ou hash.

Vulkan agrupa snapshots no frontend pela mesma identidade e mantém os fetches
originais nos pacotes. No backend, os IDs de binding continuam distintos.
Somente a mesma identidade, versão e **mesmo snapshot imutável** permitem
compartilhar imagem/view. Um serial igual em duas capturas independentes não
basta. O índice usa referências fracas; bindings, descriptors e submissões
retêm os recursos usados. Conteúdo novo cria outra imagem, preservando versões
anteriores mesmo com uploads gravados em outra command buffer. Resolves GPU
permanecem no caminho próprio e são recusados pela API de alias de uploads.

`SR_NATIVE_TEXTURE_STORAGE_REUSE=0` restaura o lookup anterior nas duas APIs;
`=1` força compartilhamento nas duas. Sem variável, D3D12 liga e Vulkan desliga.
A variável é lida uma vez e exige reiniciar o processo.
Exemplo PowerShell, na mesma sessão em que será iniciado o jogo:

```powershell
$env:SR_NATIVE_TEXTURE_STORAGE_REUSE='0'
.\run.cmd
```

Para voltar aos padrões de cada API, remova a variável antes de iniciar outra execução:
`Remove-Item Env:SR_NATIVE_TEXTURE_STORAGE_REUSE`.

## Validação

- Build Release incremental final concluído, SHA256:
  `3819981B6984251ACF91DAAE0E60968F843F7772E974F5BF2A7C2CFDCFF21E85`.
- Suíte nativa: 604 testes, zero falhas; CTest 5/5. A nova verificação
  percorre os 192 bits do fetch e exige identidade diferente para cada bit
  fora da whitelist; cobre também fallback desligado e tipos inválidos.
- Runtime/SDK: CTest 4/4, incluindo 36 testes no processo principal. Para
  cada um dos 30 bits removidos, o decoder real e o adaptador D3D12 produzem
  os mesmos pixels e mapping; ranges, layouts e mips também coincidem.
- Suíte Vulkan de CPU: CTest 1/1.
- Revisões independentes dos caminhos D3D12 e Vulkan sem problemas de produção
  acionáveis. A lacuna de teste com uploads separados encontrada durante a
  revisão foi coberta pelas fixtures merged e global antes da conclusão.
- RTX 2060: leitura após fence com sharing On/Off em descriptors por draw,
  uploads separados e ABI/heap global. Dois filtros sobre duas versões usam
  **2 decodes ligados contra 4 desligados**. O quinto draw usa uma captura
  independente de serial igual e exige um decode adicional em ambos os modos.
- Pixels esperados: point azul, linear roxo; conteúdo atualizado verde nos
  dois samplers. Versões antigas permanecem válidas até o fence. A fixture
  também recusa versões obsoletas e resolves como aliases de uploads.
- Regressões GPU `--texture-mask` e `--resolve-sample` passaram. A revisão
  independente do caminho D3D12 não encontrou problemas acionáveis.
- Após ajustar os padrões, Release e suítes foram reconstruídos. As fixtures
  merged/global repetiram os modos sem variável, `=1` e `=0`: sem variável,
  Vulkan usa 5 decodes na sequência completa, como o caminho desligado;
  `=1` usa 3. O teste nativo confirma D3D12 On/Vulkan Off sem override.

As fixtures usam shaders/pixels controlados. O teste do decoder real é separado
da fixture Vulkan, que injeta um decoder controlado para medir exatamente as
chamadas, ownership e resultado de sampling. Layers de validação Vulkan não
estão instaladas: os readbacks executaram em hardware, sem validação de API.
Isso não comprova equivalência visual de todas as cenas do jogo.

## Evidência reproduzível

Chaves e implementação: `tests/native/test_texture_storage_identity.cpp`.
Decoder real: `tests/memory_tracking/test_texture_upload.cpp`.
GPU: `tests/vulkan/resources_integration.cpp`, opção `--texture-storage` ou
`--texture-storage-merged`; heap global em
`sr_vulkan_global_test.exe --texture-storage`.

Logs locais: `logs/texture_storage_*`. O script local
`.tools/bench_texture_storage_20261029.ps1` executa pares Off/On com o mesmo
executável, 5 s de aquecimento e 30 s de medição, 1280×720, sem VSync ou limite
de FPS. Os contadores D3D12 `sampler_variant_reuses` e Vulkan
`texture_storage_reuses_total` identificam reutilização real de armazenamento.
O ganho controlado é a remoção de uploads duplicados; ganho de FPS no jogo
depende da cena e requer medições repetidas.

## Ensaio no jogo

Mesmo executável Release e RTX 2060, ordem Off depois On no primeiro par.
Vulkan repetido em ordem On depois Off. Os JSONs registram janela, escala,
MSAA, VSync e limite de FPS. Build de medição, antes da decisão de padrões por
API: SHA256 `5A6B89BA8DA2FB41F04D1445CE58C55EAD6D33FD4F53534DD0252A9ADD0DDF5C`.
Todos os ensaios forçam explicitamente a variável em 0 ou 1.

| API | Off (FPS médio) | On (FPS médio) | Evidência |
| --- | ---: | ---: | --- |
| D3D12 | 33,0105 | 37,5581 | `benchmark-20261029-221740.json` / `benchmark-20261029-221831.json` |
| Vulkan, par 1 | 25,2647 | 22,4662 | `benchmark-20261029-221940.json` / `benchmark-20261029-222035.json` |
| Vulkan, par 2 (ordem invertida) | 26,8126 | 21,7659 | `benchmark-20261029-222311.json` / `benchmark-20261029-222216.json` |

No D3D12, a última amostra cumulativa registrou 7.652 reutilizações de variantes
com On e zero com Off. O p99 foi 61,112 ms Off e 67,9318 ms On, portanto nem
todos os indicadores melhoraram. Cada modo também percorreu quantidades
diferentes de frames, buffers e trabalho de cena. Os valores de VRAM total
não isolam texturas e não comprovam economia de memória. Um único par em ordem
fixa, com stalls, não estabelece ganho estável ou causal de FPS.

No primeiro par Vulkan, a última amostra contou 8 aliases no backend On e zero
Off. O p99 foi 121,389 ms Off e 174,25 ms On: esse par não demonstra ganho no
Vulkan e motivou repetir em ordem invertida. A queda repetiu; no segundo par,
p99 de 128,274 ms Off e 182,976 ms On. A origem da diferença ainda não foi
isolada, portanto o Vulkan permanece opt-in. As últimas amostras de captura
alcançaram frames diferentes (1080 Off, 960 On), com 1.311 e 1.282 entradas,
respectivamente; esses números não são uma comparação de memória da mesma
cena. O índice fraco do backend só faz pruning acima de 2.048 chaves; esse caso
grande não foi medido aqui, e a varredura por submissão permanece uma limitação.
