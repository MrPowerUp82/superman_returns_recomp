# Otimizações dos relatos anexados — 29/10/2026

## Conferência do checkout

| Referência | Estado conferido |
| --- | --- |
| Caminho nativo D3D12/Vulkan | Já existe; o renderer nativo é o padrão. |
| Evitar leituras do heap de upload | Staging CPU já presente para swaps de vértices/índices e constantes derivadas. |
| Evitar re-hash de texturas intactas | Tracking, write watches e backoff já presentes. Este incremento evita também a consulta com locks quando as épocas provam o mesmo upload. |
| Evitar VirtualQuery por carga de constantes | `CopyGuestBytes` já usa cópia protegida por SEH; o caminho PM4 limitado é padrão. |
| Não recriar render targets por quadro | Cache de superfícies e identidade completa de resolves já presentes. |
| Cadência e pré-compilação | Pacing, scheduler de render e cache de pipelines já presentes. Não foi demonstrado que todos os quatro peers sejam limitadores de apresentação; o tick de simulação foi preservado. |
| Sombras empilhadas e janelas | D3D12 já possui resolves Texture3D e evidência de quatro slices. O Vulkan ainda usa armazenamento em camadas com view 2D nesse caminho; não afirmar paridade de sombras entre APIs nem fidelidade ao console. |
| Flush do último bloco XMA | Já existe; dois defeitos foram corrigidos neste incremento, descritos abaixo. |

As imagens descrevem resultados de outro fork. Seus 60/108 FPS e a comparação
com o console não são resultados deste checkout.

## Mudanças

### Texturas D3D12

O upload mantém as faixas exatas e o domínio do tracking capturado antes da
leitura dos bytes. O caminho rápido só aceita o mesmo recurso com todos os
stamps completos, sem escritores, e todas as épocas de páginas intactas.
Mips/camadas em faixas separadas também são conferidos. Sem view rápida, com
domínio alterado ou qualquer falha, continua a consulta `Sample` existente e
a política de hash/upload anterior. O modo `compare` continua auditando.

Ligado por padrão; `SR_NATIVE_TEXTURE_FAST_PROOF=0` permite comparar o caminho
anterior no mesmo executável. `native texture tracking` registra `fast_reuses`.

### Áudio XMA

- Pacotes contêm **2.048 bytes / 16.384 bits**. A política usava 2.048 bits e
  podia declarar entrada esgotada cedo demais. A constante agora é conferida
  por `static_assert` com o decoder do SDK.
- O export `XMAEnableContext` retorna zero em `r3`. O kick após o flush agora
  restaura o endereço do contexto antes de chamar o export novamente.

Os testes de política cobrem os dois buffers e os limites exatos de consumo.
O teste do hook compila seu corpo atual com adaptadores na fronteira do runtime:
a primeira chamada sobrescreve `r3`, a segunda precisa avançar a saída do
contexto original, e uma próxima passagem sem progresso precisa desistir.
Esse teste executa o hook, mas não decodifica áudio real.

## Validação

- Build Release com `build.cmd`, seguida de build incremental para incluir os
  últimos testes e as correções XMA: sucesso.
- Suíte nativa: **575 testes, zero falhas**. CTest nativo: **5/5**, incluindo
  o teste de ABI do hook XMA registrado quando Python e Clang/GCC estão disponíveis.
- CTest do runtime de tracking: **4/4**. Inclui prova rápida de textura com
  duas faixas e escritas pelo mapeamento físico e três aliases virtuais.
- Os três novos testes puros falharam primeiro pela ausência da API de prova
  rápida; passaram depois da implementação. As regressões XMA verificam as
  condições corrigidas, sem usar o oracle de draws como evidência de áudio.
- Regressões XMA antes da correção: política com 9 checks falhando em 3 testes;
  hook com 4 checks falhando, incluindo contexto zero no segundo kick. Depois:
  12 testes de política e um teste de hook passaram.
- Revisão independente do caminho rápido: nenhuma falha concreta encontrada;
  26 testes focados passaram.

### Rodada exploratória no jogo

Mesmo executável SHA256
`9358D638DC784DD3B75CD505666B57B5F7124C26718A50132BE61988C630544C`,
RTX 2060, D3D12, 1280×720, escala 1, MSAA 1, sombra 1, VSync e frame generation
desligados, limite zero, `SR_NATIVE_CPU_PROFILE=1`. Benchmark embutido em modo
headless, 5 s de aquecimento e 30 s de medição por modo.

| Modo | FPS médio | 1% low | Maior quadro | Provas rápidas reutilizadas no último log |
| --- | ---: | ---: | ---: | ---: |
| `SR_NATIVE_TEXTURE_FAST_PROOF=0` | 34,73 | 6,09 | 611,93 ms | 0 |
| `SR_NATIVE_TEXTURE_FAST_PROOF=1` | 40,49 | 6,11 | 412,60 ms | 22.314 |

Ambas as rodadas foram válidas e registraram `mismatches=0`. Fontes locais:
`logs/texture_fast_ab_{0,1}_{game,stdout}.log` e os JSONs
`port/out/build/win-amd64-release/benchmarks/benchmark-20261029-{201946,202051}.json`.

O contador de provas é cumulativo da execução, incluindo o aquecimento; não é
somente a janela medida pelo benchmark. As câmeras e os draws variam entre
aberturas. Há stalls grandes e só uma rodada por modo: a diferença de médias
não estabelece ganho causal de 17%, ausência de stutter ou 60 FPS estáveis.
O resultado confirma execução do novo caminho e reutilização sem `Sample`.

Não houve comparação visual alinhada com o console nem confirmação auditiva
da decolagem nesta execução. Os guards numéricos de ponteiros no código XMA
também não comprovam que uma página esteja mapeada; essa limitação preexistente
permanece, sem alegação de proteção completa contra falhas de memória.
