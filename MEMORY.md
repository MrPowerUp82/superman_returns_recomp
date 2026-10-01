O principal alvo é **a renderização da GPU**. O [perfil de gameplay](C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/logs/gpu_profile_gameplay.txt) atribui **86,4% do tempo capturado aos draws**, contra 7,9% às cópias/resolves. Esses valores somam três quadros; não representam diretamente o FPS.

Encontrei estas oportunidades, por ordem de prioridade:

| Prioridade | Otimização | Evidência e próximo passo |
|---|---|---|
| **1** | **Validar resolução interna menor** | Os hooks já existem em [render_scale.cpp](C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/render_scale.cpp:40). Comparar `sr_render_scale=100`, `75` e `50`, verificando as dimensões reais dos alvos, HUD e efeitos. É o experimento mais rápido; o ganho ainda não foi medido. |
| **2** | **Agrupar desenhos de vegetação** | Reexecutei a análise de uma captura: **520 draws usam um único buffer de vértices**, com 213 faixas de índices. Há oportunidade de instanciamento, preservando constantes por objeto e limites de cópias/clears. |
| **3** | **Concluir a integração do renderizador nativo** | É o caminho estrutural para reduzir a emulação Xenos, mas os hooks e offsets em [game_profile.h](C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/port/src/native_renderer/game_profile.h:8) continuam sem confirmação. Primeiro validar captura e shaders; depois comparar a imagem com Xenos. |
| **4** | **Reduzir espera ativa da CPU** | A análise estática encontrou **731 candidatos**, incluindo um laço com `db16cyc` em `sub_821181A0`. Perfilar quais realmente consomem CPU antes de substituir polling por espera ou backoff. |
| **5** | **Melhorar a medição** | O [benchmark](C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/tools/bench.ps1:101) usa FPS registrado a cada 2 segundos: seu “mínimo” não captura travadas individuais. Registrar tempos por quadro, p95/p99, duração real e hardware tornaria as comparações mais confiáveis. |
| **6** | **Acelerar builds** | O build já usa `-O3`, PCH e codegen incremental. O [limite fixo de quatro compilações](C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/build.cmd:30) pode virar configurável conforme a RAM disponível. Isso melhora o desenvolvimento, sem ganho direto de FPS. |

Para o agrupamento de desenhos, D3D12 também oferece `ExecuteIndirect`; a Microsoft mantém um [exemplo oficial](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-execute-indirect-sample-win32/). A escolha entre ele e instanciamento precisa ser medida neste jogo.

**Duas tentativas não merecem prioridade agora:** `sr_post_effects=false` já corrompeu a imagem sem ganho consistente, e as opções de registradores do codegen, ligadas juntas, impediram a geração de quadros. Além disso, `sr_preset=performance` atualmente aplica os mesmos valores de `quality`.

Minha recomendação é começar pela **validação da resolução interna**, seguida do **agrupamento da vegetação**. Analisei código e capturas existentes e gerei a lista de laços candidatos; não alterei o código nem executei novos benchmarks do jogo.