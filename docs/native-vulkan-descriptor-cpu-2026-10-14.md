# CPU dos descritores Vulkan — 2026-10-14

Retida a correção de uma colisão entre IDs válidos e o marcador de consulta
não realizada no cache de descritores. Ela elimina reconstruções de conjuntos
para bindings repetidos com texturas dummy. O experimento adicional de
reutilização do último binding foi removido depois das medições.

Não foi demonstrado ganho estável de FPS no jogo nem eliminação de todos os
gargalos de CPU. A execução final mediu 14,9 FPS parado e 14,8 em movimento.

## Causa e correção retida

`DescriptorStore::Shared` inicializava `last_id` com `UINT64_MAX`, que também
é `DummyTexture(k2D)`. Se o primeiro slot dessa dimensão usasse a dummy, a
consulta era pulada e o ponteiro permanecia nulo. `complete` ficava falso e
o draw não consultava nem alimentava o cache, apesar dos recursos válidos.
Cada repetição alocava e escrevia três novos conjuntos de descritores.

O mesmo marcador podia coincidir com um ID de buffer. Flags explícitos
`buffer_seen` e `texture_seen` agora distinguem primeira consulta de repetição.
O primeiro recurso sempre é consultado; as consultas subsequentes continuam
memoizadas por ID dentro do draw. Chave, samplers, política de cache, exclusão
de buffers transientes e retenção até a fence permanecem as existentes.

Não foram alterados shaders, constantes, ordem dos comandos, resolução,
precisão, política de hash de texturas, código guest ou sincronização da GPU.
Nenhum novo flag de runtime permaneceu na versão final.

## Diagnóstico isolado com o código de produção

RTX 2060, Release, `ResourceStore` e `DescriptorStore::Prepare` reais: 3.000
draws com bindings dummy idênticos. Inclui preparação/alocação/escrita dos
descritores e o primeiro miss; não executa o jogo nem mede FPS. Sem builds ou
testes pesados concorrentes durante essas medições.

| Ordem | Código | CPU µs/draw | Hits | Misses |
| --- | --- | ---: | ---: | ---: |
| 1 | Original | 102,942 | 0 | 3.000 |
| 2 | Original | 106,499 | 0 | 3.000 |
| 3 | Corrigido | 4,681 | 2.999 | 1 |
| 4 | Corrigido | 4,879 | 2.999 | 1 |
| 5 | Original, repetição após os corrigidos | 106,025 | 0 | 3.000 |

A redução de aproximadamente 95% é do custo desse caso isolado, não do frame
inteiro. Draws com geometria transiente continuam fora do cache por lifetime.
Não extrapolar esses números para todos os draws do jogo.

Reprodução do caminho atual:

```powershell
tools/build_vulkan.ps1
build/tests-vulkan/sr_vulkan_resources_test.exe --descriptor-cache-ids
build/tests-vulkan/sr_vulkan_resources_test.exe --bench-descriptor-dummies
build/tests-vulkan/sr_vulkan_contract_test.exe --production-bindings
```

Referência do diagnóstico preservada localmente em
`build/tests-vulkan/sr_descriptor_cache_before.exe`. Logs:
`logs/descriptor_cache_{before,after}_micro*.log`.

## Gameplay e experimento descartado

Intel i7-10750H / RTX 2060, 1280×720, escala 1, MSAA 1, FXAA desligado,
VSync e limite 30, frame generation desligado. Cada janela parado/movimento
dura 20 segundos. Todas as rodadas abaixo passaram a afirmação final de HUD
em gameplay; nenhum build, teste pesado ou restauração de fontes ocorreu
durante suas janelas medidas.

| Rodada limpa, em ordem | Estado | Parado | Movimento |
| --- | --- | ---: | ---: |
| `cpu_descriptor_before` | Original | 13,5 | 13,1 |
| `cpu_descriptor_off1` | Correção, atalho OFF | 14,6 | 14,6 |
| `cpu_descriptor_on1` | Correção, atalho ON | 15,0 | 12,4 |
| `cpu_descriptor_on2` | Correção, atalho ON | 13,7 | 13,7 |
| `cpu_descriptor_off2` | Correção, atalho OFF | 15,2 | 14,3 |
| `cpu_descriptor_before2` | Original, repetido | 15,3 | 14,7 |
| `cpu_descriptor_final` | Somente correção, experimento removido | 14,9 | 14,8 |

A referência original também melhora na repetição. Isso impede atribuir à
correção um ganho geral de FPS com esta série. A correção é aceita por remover
trabalho comprovadamente redundante e preservar os contratos de rendering.
Os gargalos gerais e os 30 FPS continuam pendentes.

O atalho experimental exigia igualdade dos bindings e samplers, mesma
submissão e mesma revisão do mapa de recursos. No teste isolado com grupos de
quatro draws iguais, reduziu aproximadamente 5 para 2,9 µs/draw; sem repetição,
ficou mais caro. Nos últimos intervalos de gameplay do perfil ON, somente
aproximadamente 23% dos draws usaram o atalho. O custo por draw nos perfis de
gameplay ON/OFF ficou próximo, sem benefício convincente nas rodadas limpas.
Foram removidos o helper, seus três testes, os contadores/revisões de recursos,
o campo de profiling e `SR_VULKAN_DESCRIPTOR_REUSE`.

Perfis `cpu_descriptor_{on,off}_profile` são diagnóstico, excluídos da aceitação
de FPS. Últimos intervalos ON: gravação 50–52 ms/frame, descritores 11–12 ms,
aproximadamente 2.500 draws/frame; OFF: gravação 56–59 ms, descritores 12–14 ms,
aproximadamente 2.600–3.000 draws. As cargas diferentes exigem normalização por
draw; esses tempos não provam uma redução do frame inteiro.

## Validação e artefatos finais

- Regressão de IDs falhou no código original e passou após a correção. Cobre
  dummy-only, primeiro buffer com ID `UINT64_MAX`, conjuntos iguais em draws
  repetidos e rejeição após remover uma dummy ainda retida pelo cache/submissão.
- CTest final: 91 testes Vulkan, 177 testes nativos e orçamento de alocações
  passaram. Fixtures reais de lifetime, gravação normal/merged, frame assíncrono
  e sampling de resolve passaram. Três contratos de pixels de bindings de
  produção passaram, `validation_errors=0`. Camadas de validação indisponíveis;
  esse contador não representa uma execução com validation layers.
- Revisão independente do diff final sem problemas concretos de correção ou
  lifetime. O diff de produção ficou restrito à consulta dos IDs em `Shared`.
- Oito gates de imagem contra `cpu_descriptor_before` passaram, sem alterar
  limites. Final: 33,31 dB / histograma 0,01288; cena/HUD conferidos visualmente.
  A cobertura é das cenas registradas e fixtures, não de todo o jogo; não resolve
  o gate histórico C7 mencionado no relatório de performance anterior.

Executável validado nesta etapa: `port/out/build/win-amd64-release/superman_returns.exe`.
Cópia preservada: `artifacts/native-renderer-performance/descriptor_dummy_cache_fix.exe`.
SHA-256: `657F9CAF036C1FFC5492054A84ACC46916E77EFC547EFD8CFD7154FD2B64AB70`.
Usar as dependências do runtime Release. Nenhum executável de distribuição foi
substituído. Até o encerramento desta medição, não havia commit ou publicação.

Original preservado: `descriptor_reuse_before.exe`, SHA-256
`F0F770CD289E24693BDEB1670912BADB6EA6AB0B3BDEA54A2D18777E227C9523`.
Experimento descartado preservado: `descriptor_reuse_experiment.exe`, SHA-256
`247E642E32690F3AF7D4892DFFC9803F324E02CCB736066F08F3DAD067680097`.
Ambos ficam em `artifacts/native-renderer-performance/` e são referências.

Builds: `build/descriptor_cache_final_{vulkan,release}_build.log`.
Testes: `logs/descriptor_cache_final_*` e `logs/descriptor_cache_ids_{red,green}.log`.
Gameplay: `logs/bench_cpu_descriptor_*`, CSV `logs/bench_results.csv`.
Gates: `logs/descriptor_reuse_image_gates.json`; helper Windows PowerShell 5.1
`build/compare_descriptor_reuse.ps1`. A primeira tentativa no PowerShell 7 falhou
por incompatibilidade de `System.Drawing` e não é um resultado de imagem.

A primeira referência concluiu as medições/HUD, mas o helper tentou restaurar
o executável antes de o processo terminar completamente. A restauração foi
concluída e o helper passou a esperar a saída/repetir a cópia. A falha ocorreu
após as janelas de FPS; as demais rodadas terminaram sem esse erro.
