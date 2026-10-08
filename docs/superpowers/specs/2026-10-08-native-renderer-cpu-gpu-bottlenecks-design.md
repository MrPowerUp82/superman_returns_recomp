# Renderer nativo: gargalos de CPU e GPU em D3D12 e Vulkan (ciclo 4)

Design validado em 2026-10-08 por brainstorming. Sem implementação ainda.

## Objetivo aprovado

Levar o renderer nativo, em **D3D12 e Vulkan**, a **30 FPS estáveis** (o limite do jogo) na Intel UHD deste notebook (i5-13420H), sem perda de qualidade. "Estáveis" significa mínimos ≥ ~28 e 1% low alto, não só a média.

## Resumo do entendimento

- **O quê:** descobrir e atacar os gargalos de CPU e GPU do renderer nativo nas duas APIs.
- **Por quê:** o Vulkan é o padrão do launcher e roda em ~22–28 FPS. O D3D12 fica em ~29, mas varia com a cena. O ciclo 3 reduziu a gravação do quadro de ~50 para ~30 ms e não fechou a meta (ver `2026-10-06-vulkan-fps-parity-cycle3-design.md`). O benchmark de 2026-10-08 em `logs/bench_results.csv` mostra o estado atual.
- **Para quem:** o autor e os jogadores que abrirem o launcher com as opções padrão.
- **Hardware de medição:** Intel UHD deste notebook; `bench_api.ps1` em 1280x720, vsync e limite de 30 FPS.
- **Restrições:**
  - Imagem **bit a bit idêntica**.
  - Texturas **exatas a cada quadro**, sem backoff, atraso nem vigilância parcial.
  - Mudança estrutural permitida: **pipeline entre estágios**.
  - `sr_renderer=native` nunca cai para Xenos.
- **Fora do escopo:** gravação paralela em várias threads (contingência, exige nova decisão); presets de qualidade; atalhos de revalidação de textura; paridade em outras GPUs (a RTX 2060 de casa não é medida aqui); cenas M3 e ciclo de vida da janela.

## Requisitos não funcionais (suposições)

- **Performance:** média ≥ 30 (no limite) e mínimos ≥ ~28 em duas execuções seguidas de `bench_api.ps1`, parado e andando, nas duas APIs. O uso de CPU pode crescer (hoje ~6 de 12 threads), mas a thread do jogo não pode ficar mais lenta em CPUs com poucos núcleos.
- **Escala, segurança e privacidade:** não se aplicam (app desktop de um jogador, otimização interna).
- **Confiabilidade:** nenhum crash, travamento ou corrupção novos. Testes unitários, contrato de produção, os 15 fixtures de GPU, o fixture `hold-lifetime` e o gate de imagem continuam passando.
- **Manutenção:** um commit por passo, medido e revertido se não reduzir o custo do seu estágio. Resultados registrados em `docs/` (o usuário alterna entre Claude e Codex).

## Suposições adicionais

1. As validation layers do Vulkan seguem ausentes; fixtures e gate de imagem são a única rede.
2. A comparação é por **custo de estágio** (ms por quadro ou µs por draw), porque a cena varia entre execuções.
3. Os perfis do ciclo 3 estão desatualizados; a primeira etapa é medir de novo.
4. Captura e hash são compartilhados entre as APIs, então ganhos nessa parte valem para as duas.

## Perguntas em aberto (a Fase 0 responde)

- A thread do jogo (lógica, PM4 e captura) cabe em ~33 ms depois de reduzir o hash?
  Resposta (Fase 0, 2026-10-08): no Vulkan, a média ainda não cabe com a captura inclusa (o `game` mede 34,99 e 36,67 ms, p99 de 48,3 e 49,1 ms), mas esse número é o estágio de ritmo: o `game` vai da saída de um `OnSwap` à entrada do seguinte, então `game` + `front_wait` fecha com o intervalo do quadro por construção (34,99 + 0,92 ≈ 35,9; 36,67 + 0,95 ≈ 37,7) e o seu `util.` (97%) é quase tautológico e o seu `limitante` (73% e 72%) sai inflado pela mesma construção. O que os dados sustentam: o `front_wait` fica em ~1 ms (0,92 e 0,95), então a thread do jogo não é segurada pelo renderer esperando; a captura é só 6,59 e 7,16 ms; o `game_other` (28,40 e 29,51 ms) é o maior bloco que existe. Sem a captura, a média do `game` (~28 a 30 ms) ficaria abaixo do orçamento de 33,3 ms, e só o p99 (37,85 e 37,69 ms no `game_other`) estouraria. Não está resolvido se o `game_other` é trabalho ou espera da thread do jogo fora do renderer (por exemplo vblank ou outra sincronização); a execução de D3D12 limitada pela GPU mostra que boa parte do `game` pode ser espera (cai para 23,47 ms, contra 32,76 ms quando o limitador de 30 FPS manda). Logo, reduzir o hash sozinho não fecha a meta no Vulkan, mas também não está demonstrado que a thread do jogo seja o gargalo. No D3D12 a média cabe (32,76 e 30,86 ms), mas o p99 não (42,4 e 43,3 ms), e o limitador de 30 FPS faz o `game` ficar perto do intervalo por construção, então ele não prova gargalo. Detalhes e ressalvas em `docs/native-renderer-timeline.md`.
- O replay worker (~40 ms) está no caminho crítico ou tem folga?
  Resposta (Fase 0, 2026-10-08): não tem folga de p99 em nenhuma API. Vulkan: 29,32 e 31,55 ms de tempo ocupado (82% e 84%), limitante em 14% e 13% dos quadros, p99 de 43,7 e 43,8 ms. D3D12: 29,17 e 32,37 ms (87% e 97%), `bloq.` de 0,00 ms, limitante em 26% e 57% dos quadros, p99 de 47,6 e 43,5 ms; é o estágio de trabalho mais ocupado do D3D12. Uma execução extra com `--sr_native_render_scale=2` (GPU como gargalo) mostrou o `bloq.` do worker em 28,35 ms, então o caminho de espera de fence funciona.
- O gargalo da GPU é compartilhado entre as APIs ou específico de uma?
  Resposta (Fase 0, 2026-10-08): é específico do Vulkan. A GPU do Vulkan mede 29,75 e 31,93 ms (83% e 85% de `util.`, limitante em 8% e 9% dos quadros, p99 de 39,4 e 37,2 ms, acima do orçamento), 1,39x e 1,28x a do D3D12 (21,47 e 25,02 ms, 64% e 75%, limitante em 0% e 1%, folga p99 de +7,1 e +3,4 ms). O valor do Vulkan é limite superior (inclui fila atrás do quadro anterior, com 2 quadros em voo) e a cena varia entre execuções.
- O Vulkan já tem timestamps de GPU por grupo de passes?
  Resposta (Fase 0, 2026-10-08): não tinha. Esta fase adicionou só o tempo de GPU por quadro inteiro (do `TOP_OF_PIPE` do primeiro command buffer ao `BOTTOM_OF_PIPE` do fim do quadro). O agrupamento por passe ficou como decisão pendente: os ids de passe do perfil em `game_profile.h` são -1, então a medição por grupo de passes precisa de projeto antes do passo 2.

## Decision Log

| Decisão | Alternativas consideradas | Motivo |
| --- | --- | --- |
| Meta: 30 FPS estáveis nas duas APIs, na UHD | Passar de 30 FPS (headroom/LSFG); ganho relativo em várias GPUs | É o limite do jogo e o que o launcher entrega ao usuário |
| Textura exata por quadro | Cobertura de escrita provada; backoff com guarda | O ciclo 2 corrompeu a imagem com atalhos de vigilância e posse |
| Pipeline entre estágios permitido; gravação paralela fora | Só micro-otimizações; pipeline mais gravação paralela | Usa núcleos ociosos com risco médio; a gravação paralela é o maior risco |
| GPU só bit a bit | Equivalente visual; presets de qualidade | Verificável com tolerância zero; sem julgamento humano |
| Medir antes de projetar (Fase 0) | Ir direto às otimizações conhecidas | O ciclo 3 otimizou etapas que nem sempre eram o limite |
| Abordagem A (caminho crítico medido, pipeline e hash paralelo exato) | B (só micro-otimizações por estágio); C (front-end comum compartilhado) | B tem ganhos decrescentes e não fechou a meta no ciclo 3; C é grande e sem ganho de FPS comprovado (YAGNI) |
| Hash paralelo: cada textura inteira em uma thread, join antes de seguir | Hash adiado ou especulativo | Preserva o valor e a semântica de leitura de hoje |
| Pipeline com um dono por estado e filas limitadas (capacidade 2) | Filas ilimitadas; estado compartilhado entre threads | Evita os problemas de ownership do ciclo 2; limita a latência a +1 quadro |

## Design

### 1. Fase 0: linha do tempo unificada (medir antes de mexer)

Responde qual estágio limita o FPS de cada API, com dados do mesmo quadro.

- Reaproveita `SR_VULKAN_PROFILE`, `SR_NATIVE_CPU_PROFILE`, `tools/bench/vulkan_profile_summary.ps1`, `bench_api.ps1 -Profile` e os timestamps de GPU do D3D12.
- **Registro por quadro** em ring buffer fixo, sem alocação nem lock: início e fim do quadro N na thread do jogo (lógica, PM4, captura/hash), no replay/worker, na gravação, na submissão e na GPU, mais o instante do present. O dump acontece só no fim do bench.
- **Formato comum** às duas APIs; cada backend preenche os estágios que tem.
- **Caminho crítico:** um script em `tools/bench` informa, por quadro, qual estágio terminou por último e quanto cada um esperou o anterior; calcula média, mínimo, 1% low e o percentual do tempo em que cada estágio foi o limitante.
- **GPU:** timestamps por grupo de passes (profundidade, cena, pós-processo) nas duas APIs. Confirmar na implementação se o Vulkan já tem; se não, entram aqui.
- **Aceitação:** instrumentação < 0,3 ms por quadro (ligada contra desligada); desligada por padrão, ativada por variável de ambiente, sem mudar imagem nem comportamento. Saída: tabela "estágio limitante" por API em `docs/`, que define a ordem da Fase 1.
- **Não faz:** nenhuma otimização.

### 2. Hash paralelo exato (compartilhado)

Entra se a Fase 0 mostrar captura/hash no caminho crítico. O AVX2 (commit 2943e6f) já baixou o hash para ~8–13 ms, e ele segue sendo o maior bloco de CPU da thread do jogo no Vulkan (e ~7,5–8 ms/quadro no D3D12).

- A captura **coleta** as texturas ainda não validadas no quadro. Acima de ~256 KB no total, reparte entre helpers e a própria thread e espera por contador atômico.
- Cada textura é hasheada **inteira, por uma thread, na mesma ordem lógica**. O valor e o seed encadeado não mudam; o resultado é idêntico ao de hoje.
- A thread do jogo só segue depois do join; a leitura da memória acontece no mesmo ponto lógico. Sem leitura atrasada nem especulativa.
- Cvar `sr_native_hash_threads`, padrão baseado nos núcleos (máximo 3 a 4 helpers); `0` desliga. Helpers com spin curto e depois sleep. Proteção SEH mantida em cada helper.
- **D3D12:** o hash fica em `GetTextureSrvIndex`, dentro de `UploadConstants`, no worker. O mesmo pool serve, com a coleta no preparo do draw; só entra se o hash do D3D12 estiver no caminho crítico.
- **Verificação:** testes unitários de hash paralelo igual ao serial (tamanhos variados, `threads=0`, `1` e `4`); gate de imagem e fixtures a cada commit.
- **Não faz:** não pula nem espaça nenhuma revalidação.

### 3. Pipeline entre estágios

Entra depois do hash paralelo e só se a Fase 0 mostrar soma serial > 33 ms com núcleos ociosos. Se um estágio sozinho já passa de 33 ms, o trabalho vai para dentro dele.

- **Princípio:** cada estágio é dono único do seu estado mutável (`ResourceStore`, `DescriptorStore`, cache de texturas, SRV heap). Os estágios conversam por filas limitadas de capacidade 2, com backpressure; a latência extra fica limitada a 1 quadro.
- **Vulkan** (captura → replay → gravação já são threads distintas): aumentar a profundidade entre replay e gravação, para o quadro N+1 ser replayado enquanto N é gravado.
- **D3D12** (o worker faz preparo e gravação, 62–70 ms): separar o **preparo** (`UploadConstants`, streams, `PrepareDraw`) da **gravação do command list**. O preparo escreve na arena por quadro (`upload_buffers_[3]`); a gravação codifica a partir dela.
- **Riscos:** pontos de sincronização reais (resolves lidos pelo quadro seguinte, readbacks, queries), que precisam de inventário e viram barreiras explícitas; estado compartilhado do D3D12 entre preparo e gravação (índices do SRV heap, PSOs), que precisa poder ser congelado por quadro.
- **Verificação:** `hold-lifetime`, gate de imagem, fixtures e um desligador por variável de ambiente que volta ao modo de uma thread.

### 4. GPU bit a bit

Os números de GPU disponíveis são antigos e conflitantes (timestamps de ~7,5 ms no D3D12 contra 70–85% de uso; ~32–35 ms no Vulkan no fim do ciclo 3). A Fase 0 mede por grupo de passes e define a ordem. Medições antigas do D3D12 apontam pass de profundidade (~30%), pós-processo (~18%) e shader de folhagem (~5%).

Candidatos que não mudam a imagem, cada um só entra se estiver entre os maiores custos medidos:

1. Bandwidth: `loadOp`/`storeOp` com `DONT_CARE` quando o conteúdo não é lido depois; resolves e copies só da região usada.
2. Barreiras e transições redundantes entre passes.
3. Clears de targets totalmente sobrescritos antes de qualquer leitura.
4. Trabalho morto: draws que não chegam a nenhum resolve nem ao quadro final; passes de profundidade sem consumidor.
5. Stutter de pipeline: persistir o cache de pipelines e aquecer em segundo plano os pipelines conhecidos (afeta o 1% low).

Os candidatos são encontrados com o dump de quadro (`SR_VULKAN_DUMP_FRAME`, `packets.txt`) e as ferramentas RenderDoc de `tools/analysis`.

**Verificação:** em cena estável (o instante de detecção do HUD), comparar os readbacks brutos dos resolves e dos targets finais com a otimização ligada e desligada, com tolerância zero. Cada item tem desligador por variável de ambiente para o A/B no mesmo binário.

**Não faz:** não muda formatos, precisão de shader, sombras, MSAA nem pós-processo.

### 5. Ordem, critérios de parada, riscos e testes

| # | Passo | Condição para entrar |
|---|---|---|
| 0 | Linha do tempo unificada + tabela de estágio limitante | Sempre; decide o resto |
| 1 | Hash paralelo exato | Captura/hash no caminho crítico |
| 2 | Itens de GPU bit a bit, do maior custo medido para o menor | GPU entre os limitantes |
| 3 | Pipeline entre estágios (Vulkan, depois D3D12) | Soma serial > 33 ms com núcleos ociosos |
| 4 | Estabilidade do 1% low (PSO em segundo plano, picos de stall) | Mínimos < 28 mesmo com a média em 30 |

Os passos 1 a 3 podem trocar de ordem conforme a Fase 0. O passo 4 garante "estável", e não só a média.

- **Parada por passo:** se o custo do estágio atacado não cair de forma medida (µs/draw ou ms/quadro, não FPS bruto), o commit é revertido.
- **Parada global:** média e 1% low na meta, nas duas APIs, em duas execuções seguidas de `bench_api.ps1`. Se faltar depois do passo 3, **parar e perguntar** antes de gravação paralela.
- **Testes a cada passo:** testes unitários nativos e Vulkan, contrato de produção, os 15 fixtures de GPU, `hold-lifetime`, gate de imagem e bench nas duas APIs (D3D12 como sanidade, para não regredir).

## Riscos

- **Ownership de recursos entre quadros** (o que quebrou o ciclo 2): mitigado pelo invariante `Hold`, pelo fixture `hold-lifetime` e por um dono por estado no pipeline.
- **Variação de cena** entre execuções: mitigada pela comparação por custo de estágio.
- **Sem validation layers:** fixtures e gate de imagem são a única rede.
- **Máquina única de medição:** ganhos na UHD não garantem o mesmo na RTX 2060.
- **Pontos de sincronização** (resolves, readbacks, queries) podem limitar o ganho do pipeline.
- **O gate de imagem** só pega corrupção grosseira; por isso a verificação de GPU usa comparação bit a bit em cena estável.

## Critérios de saída do brainstorming

- Understanding Lock confirmado.
- Abordagem A aceita.
- Suposições e riscos documentados.
- Decision Log completo.
