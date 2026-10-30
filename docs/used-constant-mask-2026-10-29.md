# Constantes usadas pelo shader — 29/10/2026

Terceira candidata da [pesquisa Canary](xenia-canary-opportunities-2026-10-29.md),
implementada e validada como **experimental, desligada por padrão**. A máscara
reduziu uploads, mas os dois pares D3D12 perderam FPS. O único par Vulkan foi
favorável e não estabelece ganho estável. As otimizações anteriores mantêm
seus padrões; esta etapa não muda o comportamento padrão de uploads.

## Implementação e limites

`graphics/guest/shader_constant_usage.h` analisa o container XDK original,
reutilizando a validação limitada de CF da máscara de texturas. Percorre todos
os EXECs do prefixo, incluindo caminhos condicionais/loops, e coleta uma
aproximação conservadora dos três operandos ALU. Inclui `mulsc/addsc/subsc`,
cujo seletor src3 não indica a classe real do operando constante. Endereçamento
relativo por a0/aL cobre todos os 256 float4 do estágio. ALU/CF/container
desconhecidos usam o fallback. Alguns operandos inativos podem ser incluídos
extra: a análise prioriza não omitir dependências.

As três rotas de criação de `ShaderCapture` publicam metadados imutáveis. Sem a
opção ligada, a análise adicional nem é executada. Estágios são identificados
pelo shader, pois os slots do dispositivo podem se inverter. Estágio ausente,
desconhecido, duplicado ou não resolvido não autoriza exclusões.

D3D12 conserva buffers de 4096 bytes, a ABI, os bindings e o fast path por
versão do banco. Quando a versão muda, compara apenas registradores usados
contra um snapshot **normal de CPU** dos valores sanitizados efetivamente
enviados. Nunca lê o upload heap. A prova é memoizada por versão/máscara; a
versão e o baseline do último upload **não avançam em uma reutilização**.
Assim, o shader B pode exigir upload de cN que o shader A havia ignorado,
mesmo sem outra escrita entre os dois draws. O upload ring invalida o cache
por frame. Debug override continua forçando upload.

Captura de movimento, post-VS/GS, variantes de RT/coverage, packet check e
trace não usam máscaras. A GS de extração pode ler VS c3–c10 fora do ALU.
Diretórios DXIL, bibliotecas de preshaders e ferramentas explicitamente
substituídos também ficam fora da otimização D3D12. Bibliotecas geradas devem
seguir o contrato atual do emissor `CONAN_RECOMP`; o nome/hash de um shader
host arbitrário não prova suas dependências.

Vulkan aplica a comparação ao cache **opcional existente** de bancos do ABI
portável. Mantém os guards de submissão/chunk e o snapshot do offset retido;
trocar o baseline em uma reutilização faria B ler valores antigos. O heap
global mantém suas cópias completas. O cache portável ainda reserva um slice
de 12 KiB por draw: reduzir escritas não significa reduzir essa reserva.
Bool, loop, shared, snapshots de comandos e layout dos registradores continuam
completos nas duas APIs.

## Como experimentar

Reinicie o jogo após definir a variável. No D3D12:

```powershell
$env:SR_NATIVE_USED_CONSTANT_MASK='1'
.\run.cmd
```

No Vulkan, a máscara depende do cache portável, que também permanece opt-in:

```powershell
$env:SR_NATIVE_USED_CONSTANT_MASK='1'
$env:SR_VULKAN_CONSTANT_BANK_CACHE='1'
.\run.cmd --sr_native_api=vulkan
```

`SR_NATIVE_USED_CONSTANT_MASK=0` ou ausência da variável mantém o caminho
anterior. Para remover o override: `Remove-Item Env:SR_NATIVE_USED_CONSTANT_MASK`.
O flag não sobrepõe as exclusões de diagnóstico e shaders substituídos.

## Validação

- Testes nativos: 615 casos, sem falhas; CTest 5/5.
- Vulkan CPU: 109 casos, sem falhas; CTest 1/1.
- Runtime/SDK: 37 casos principais, CTest 4/4. A referência compila quatro
  fontes originais do decoder SDK, sem alterá-las, pois seus símbolos são
  privados ao plugin Xenos. Com SDK instalado sem fontes, essa referência
  adicional não é compilada.
- Cross-check opcional `SR_SHADER_CONSTANT_CORPUS=artifacts/shaders/raw`:
  **241/241 containers conhecidos**, sem nenhum bit usado pelo SDK ausente na
  nossa máscara; endereçamento dinâmico exige cobertura integral.
- Testes puros cobrem operandos estáticos, extremos c0/c255, forms escalares,
  flags de endereçamento, máscaras vazias/desconhecidas, igualdade bit a bit,
  memoização, shader switches, regressão de versão, reset e validade de chunk.
- GPU RTX 2060: sete readbacks congelados pelos caminhos reais de
  `GameRenderer`/descriptors/caches, nas cores red/red/green/blue/green/yellow/blue.
  A sequência altera uma constante ignorada, troca de shader, muda uma constante
  usada, faz leitura dinâmica e muda registradores omitidos dos dois estágios.
- No teste GPU, máscara On reutilizou VS/PS 6/2 vezes contra 5/1 com Off:
  **duas escritas adicionais de banco de 4 KiB evitadas**, com os mesmos pixels.
  Com o cache desligado, 0/0 reutilizações e os mesmos pixels.
- Regressões GPU de máscara de texturas e storage merged passaram; revisão
  independente de fonte não encontrou problemas acionáveis de correção.

O teste GPU usa shaders e máscaras controlados; o parser é verificado
separadamente contra o SDK/corpus. Não foi comprovada equivalência visual de
todas as cenas do jogo. Layers de validação Vulkan estavam indisponíveis;
houve execução e leitura em hardware, sem validação de API.

## Ensaio no jogo e decisão

Mesmo executável de medição, SHA256
`8700A36C7A3C147A8E862529F2D8E87A1A4BD07E5E318300B7233EEFE84D0FEF`,
RTX 2060, 1280×720, 5 s de aquecimento + 30 s medidos, limite/VSync/LSFG
desligados. CPU profiling e detalhe das constantes ligados em ambos os modos.
O segundo par D3D12 inverte a ordem. Vulkan força o cache de bancos em 1
**nos dois modos**, isolando a máscara dentro dele; não compara cache On/Off.
Storage segue o padrão da etapa anterior (D3D12 On, Vulkan Off).

| API / par | Off FPS médio | On FPS médio | JSON Off / On |
| --- | ---: | ---: | --- |
| D3D12 1, Off→On | 40,8927 | 29,9304 | `benchmark-20261029-225251.json` / `benchmark-20261029-225343.json` |
| D3D12 2, On→Off | 38,4504 | 36,0027 | `benchmark-20261029-225527.json` / `benchmark-20261029-225436.json` |
| Vulkan 1, Off→On | 23,8999 | 28,8746 | `benchmark-20261029-225737.json` / `benchmark-20261029-225831.json` |

Últimas amostras cumulativas D3D12 On: 14.847/1 e 16.573/1 uploads de VS/PS
evitados em relação à regra por versão para aquela execução. Off contou zero.
Esses contadores incluem aquecimento e terminam na última amostra periódica;
não devem ser tratados como totais exclusivos da janela de 30 s.

O p99 D3D12 piorou 44,3052→72,204 ms e 48,7337→93,0047 ms. Vulkan melhorou
152,524→81,8789 ms nesse único par. Quantidades de draws, buffers e trabalho
de cena diferem entre execuções; não foi isolada a origem da diferença de FPS.
Comparações e manutenção de snapshots em CPU também têm custo: menos uploads
não comprovam menor tempo total. Por isso a implementação permanece opt-in,
sem promessa de ganho ou de 60 FPS.

Logs: `logs/constant_mask_*`. Reprodução local:
`.tools/bench_constant_mask_20261029.ps1`, com `-Api`, `-StartPair` e `-OnFirst`.

Build Release final posterior à decisão de padrão: SHA256
`B5FE2070107D0779D90D4606E062CD6B5107580433E474074BACA8105683768D`.
Reconstruído com análise de captura desativada quando o flag está Off. CTest
final passou 5/5 nativo, 1/1 Vulkan e 4/4 tracking. O readback GPU foi repetido
com cache On nos modos sem variável, `=1` e `=0`; default e zero produziram
5/1 reuses de VS/PS, enquanto `=1` produziu 6/2, sempre com os mesmos pixels.
