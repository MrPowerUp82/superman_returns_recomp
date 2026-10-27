# Otimizações de CPU e desbloqueio de FPS — 27/10/2026

## O que mudou

O build normal agora prepara e verifica o runtime ReXGlue v0.10.0 com o patch
de rastreamento de escritas. O código já selecionava o pacote de CPU por padrão,
mas o `build.cmd` e o cache da build Release usavam `SR_MEMORY_TRACKING_SDK=OFF`.
Nesse caso, a API de rastreamento não existe no runtime e o renderer precisa
continuar calculando hashes. Selecionar uma opção do renderer não bastava.

`SR_MEMORY_TRACKING_SDK=OFF` continua disponível para o runtime original. O
setup preserva a revisão fixada, a verificação do patch e os hashes das fontes;
alterações desconhecidas são rejeitadas. A verificação CMake permanece obrigatória.
O setup só copia fontes cujo conteúdo mudou e só regrava um manifesto diferente,
preservando timestamps de entradas idênticas nas execuções seguintes.

O executável e novas preferências do launcher usam `sr_native_fps_limit=0`.
Continuam disponíveis 30/60/120 FPS, com preservação das escolhas explícitas e
das preferências salvas. As preferências locais das instalações Release e
`artifacts/launcher` foram alteradas de 30 para 0, preservando cópias em
`artifacts/cpu_unlock_20261027/launcher-settings/`.

## Relação com as dicas dos prints

| Dica | Caminho efetivo neste projeto |
| --- | --- |
| Evitar leitura do upload pela CPU | Shared constants e gamma já são montados em RAM CPU e copiados para o upload. |
| Evitar hash repetido de texturas | Runtime com rastreamento agora incluído no build normal. Só reutilizar com prova de cobertura completa, mesma geração/alocação e recurso enviado; caso contrário, usar hash. |
| Evitar VirtualQuery por load de constantes | O padrão já usa cópia limitada com `ReadProcessMemory`; memória parcial/inacessível é rejeitada e os bytes não são reutilizados como prova de validade. |
| Evitar recriação alternada de render targets | O padrão já usa identidade completa de resolve, incluindo dimensões, formato, escala e camadas. Variantes mantêm recursos separados. |

Esses caminhos de CPU são do D3D12 nativo. A implementação Vulkan permanece
disponível; não se afirma que o pacote D3D12 seja aplicado a ela.

## Limite de apresentação e tempo do jogo

O controle existente regula os vblanks nativos: 30 seleciona 60 Hz, 60 seleciona
120 Hz e 120 seleciona 240 Hz, conforme a hipótese de dois vblanks por quadro.
Zero usa `sr_native_unlocked_vblank_hz`, cujo padrão é 240 Hz. Portanto, zero
retira o alvo nativo de 30, mas não significa uma fonte infinita de interrupções.
VSync do host continua independente.

A afirmação de quatro loops no fork do print não fornece endereços ou contratos
para este executável. Nenhuma constante de simulação 1/30 deve ser substituída
apenas por coincidir com esse valor. O desbloqueio nativo não é prova de 60
imagens únicas por segundo nem de simulação correta acima de 30 FPS.

## Validação

- SDK fixado e testes de setup passaram: idempotência, revisão/caminho, patch
  parcial, dependências incompatíveis e preservação de edições locais.
- Testes nativos: 492 casos sem falhas; CTest 3/3, incluindo contratos DLSS
  sem SDK e orçamento de captura.
- Auditoria focada de PM4: 38 testes, sem falhas, incluindo leituras parciais,
  memória inacessível, atualização dos bytes e posse de streams aninhados.
- Testes Vulkan: 108 casos sem falhas; CTest 1/1 da suíte de unidade.
- Python: 169 testes e 4 subtestes passaram.
- Launcher: testes passaram, incluindo opções 0/30/60/120, persistência,
  configurações antigas sem FPS e valores inválidos; build WPF publicado localmente.
- Build Release com runtime rastreável e os dois backends passou; a DLL final
  exporta `RexGetMemoryTrackingApi`.
- CTest do runtime: 4/4, incluindo perda de contrato, falha ao armar proteção
  e memória não inicializada. Os testes foram repetidos após o build final.

As evidências locais usam o prefixo
`logs/cpu_unlock_20261027_`; o executável/runtime anteriores ficam em
`artifacts/cpu_unlock_20261027/reference/`.

## Gameplay: diagnóstico de esperas

A rodada `cpu_unlock_diag_20261027` usou D3D12/RTX 2060, 1280×720,
escala 1, MSAA 1, VSync ligado, geração de quadros desligada e limite nativo zero.
O log confirmou os quatro caminhos do pacote e reuso efetivo de texturas:
no último relatório, 2.404.555 verificações e 306.960.089.088 bytes de hash
evitados, com zero divergências detectadas. São totais cumulativos desde o boot,
não contagens por quadro. Cobertura desconhecida/inválida continuou usando hash.

`SR_NATIVE_GUEST_SLEEP_PROFILE=1` registra chamadas da rotina `8246A898`, sem
alterar parâmetros ou resultados. Usa armazenamento fixo e fica desligado por
padrão. O callsite `824822C4`, que solicita 33 ms no estado 5 de um caminho
associado ao player de vídeo, não foi observado nesta rodada, inclusive nas
janelas de gameplay. As esperas observadas foram de 5/16/50 ms em outros
callers. A soma dos tempos de espera inclui threads concorrentes e não deve
ser tratada como custo serial por quadro. Isso não descarta loops de polling
ou outros estados/cenas.

As médias diagnósticas foram 26,4 FPS parado e 26,6 andando. Os últimos 1.100
intervalos entre swaps tiveram mínimo de 23,06 ms e mediana de 35,54 ms;
312 ficaram abaixo de 33 ms. Não houve um teto rígido de 33,33 ms por swap
nesse trecho. Isso não comprova imagens únicas ou ritmo correto da simulação
a 60 FPS. A timeline apontou o jogo como limitante em 98% das amostras, com
37,46 ms de busy médio contra 22,76 ms do worker; os estágios se sobrepõem.

Esses números incluem diagnóstico e timeline. A medição sem esses instrumentos
é registrada separadamente. As capturas foram inspecionadas e mostram Superman,
cidade e HUD; não houve comparação visual alinhada contra uma referência Xbox.
