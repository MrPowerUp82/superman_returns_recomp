# Continuação do renderer nativo — 02/10/2026

Retomada de `checkpoint1.md`. A correção de leitura dinâmica dos vértices está implementada no working tree e no executável local. Não foi feito commit. O renderer ainda apresenta iluminação escura e falhas pontuais de PSO; não considerar o port visualmente concluído.

## Correção implementada

- Shaders VS com o parâmetro `instance_data` agora leem buffers por `ByteAddressBuffer`, usando o índice calculado pelo shader. O primeiro fetch de `POSITION1` pode transformar o índice antes dos fetches seguintes. Cada full-fetch captura seu operando; mini-fetches preservam o índice do full-fetch anterior.
- A conversão do índice segue o backend Xenos local: `floor(index)`, ou `floor(index + 0.5)` quando arredondado. Os formatos são convertidos conforme `MapDeclType`, sobre os buffers já colocados em ordem de bytes do host.
- Esses shaders não declaram entradas IA. Manter entradas não utilizadas na assinatura DXIL ainda fazia o D3D12 validar tipos e rejeitar o PSO.
- Metadados das declarações ficam em `SharedConstants` a partir de c32: 224 entradas de quatro DWORDs (SRV, offset, stride, tipo XDK). A root signature ganhou a tabela SRV space5; o CB compartilhado passou de 512 para 4096 bytes.
- Buffers sujos usados por esses shaders são capturados por inteiro, pois o intervalo de índices do draw não descreve o intervalo efetivamente lido pelo shader.
- A detecção é registrada em `GuestShaderInfo`, incluindo os caminhos sem a biblioteca opcional de pre-shaders.
- Uploads completos e parciais restauram `NON_PIXEL_SHADER_RESOURCE`. Descritores raw são reciclados após o fence da submissão, e o dump antes do draw preserva suas leases durante os fences intermediários de readback.

O tradutor baixado em `.tools/` é reproduzido pelo novo patch versionável `tools/shaders/xenosrecomp/patches/0007-dynamic-vertex-fetch.patch`. A aplicação dos sete patches ao pin original foi verificada contra o código testado.

## Estado local dos shaders

O corpus gerado que estava neste checkout era antigo: inicializava r0 com zero. Também faltavam os dumps de runtime mencionados no checkpoint anterior.

Foram recapturados 189 arquivos de runtime e preservados em `logs/native_shaders/` (ignorados por Git). O corpus atual tem 223 containers, dos quais 221 compilam: 79 VS e 142 PS. A biblioteca `.srsl` e o pack embutido foram reconstruídos e instalados junto ao executável.

Os dois containers sem DXIL continuam sendo `978B0FF62C3693C2.vs` e `A5232A3184AA617C.vs`: respectivamente entrada sem location conhecida e parâmetro `iPosition0` duplicado. Não incluir artefatos de shaders ou dados do jogo no commit.

Para reconstruir preservando a cobertura atual:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/shaders/build_corpus.ps1 -DumpDir logs/native_shaders -NoSpirv
.\build.cmd
```

## Validação realizada

- Build final: sucesso, `logs/continue_build.log`.
- Suíte nativa CTest: passou.
- Suíte do tradutor CTest: passou, incluindo índice calculado, mini-fetch e arredondamento.
- Pytest de `test_game_profile.py` e `test_shader_tools.py`: 6 passaram.
- Revisão independente: os três problemas encontrados (barreira parcial, dependência da biblioteca e lifetime no dump) foram corrigidos e revisados novamente.
- RenderDoc: `logs/rdc/continue_instancing_mesh_capture.rdc`, análise em `logs/rdc/continue_instancing_mesh.json`. Os 12 maiores draws (até 68.376 índices) têm zero atributos IA e suas primeiras 16 posições pós-VS são finitas e não zeradas. A captura antecede as últimas correções de barreira/detecção/lifetime; o caminho de fetch permanece o mesmo.
- Smoke final de aproximadamente 102 s com a camada de debug D3D12: Superman, prédios, árvores, HUD e movimento renderizados. Log preservado em `logs/continue_instancing_verified.log`.
- O heap ficou em `srv_next=3543` com 20.314 buffers, em comparação com 18.800 descritores em apenas 70 s antes da reciclagem. A execução final contabilizou 4.213.025 draws e 86.001 skips de PSO.
- Amostras do script: 22,1 FPS na janela chamada idle e 24,2 FPS em forward. Parte da janela idle ainda mostra a câmera da introdução; isso é uma validação de execução, não um benchmark definitivo de gameplay estabilizado.
- Imagens: `logs/bench_continue_instancing_verified_idle.png`, `logs/bench_continue_instancing_verified_forward.png`. `logs/bench_continue_instancing_final_forward.png` mostra a rua com HUD antes de o personagem chegar à parede.

Os scripts temporários de teste foram removidos. Para reproduzir com os scripts existentes, o menu nesta máquina precisou de aproximadamente 12 s entre Enter (Start) e Espaço (Start New Game); os 3 s atuais podem ser insuficientes. O detector de HUD também pode antecipar a entrada em gameplay.

## Próximos pontos de investigação

1. Iluminação escura e linhas azuis em bordas da rua. Comparar passes e constantes com o capture Xenos existente `logs/rdc/gameplay_reference_capture.rdc`; não assumir que o instancing resolve iluminação.
2. PSOs remanescentes: `C63F6F22679609F0 / F590EE61ABB72FD0` e `C5E8551EFDE01162 / CD5A5ADBA05B093B`. O DXIL de VS declara `COLOR0`, mas os layouts registrados não o fornecem. Investigar a fonte esperada dessa entrada antes de sintetizar valores.
3. O teste final recebeu flags de dump antes do draw aos 50 s, mas não gerou arquivos nem a mensagem de dump. O lifetime desse caminho foi revisado, porém a execução dele permanece sem validação. Investigar o timer/caminho de disparo dos diagnósticos antes de depender desses dumps.
4. Validar runtime com `sr_native_preshaders=false` se esse modo for necessário. A dependência incorreta na detecção foi removida e os caminhos de registro foram revisados; esse modo ainda não foi executado nesta retomada.

RenderDoc portátil desta máquina: `.tools/renderdoc/RenderDoc_1.46_64/qrenderdoc.exe`; não está instalado em `C:/Program Files/RenderDoc`.
