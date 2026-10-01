# Uso direto de Lossless.dll

## Resultado da investigação — 2026-09-30

A integração pretendida usa somente a DLL fornecida pelo usuário. Não precisa
abrir LosslessScaling.exe, criar perfis no aplicativo nem capturar sua janela.
O launcher externo criado durante a investigação foi removido.

Referência examinada: [NetherSX2_nx, revisão f084dc1](https://github.com/NaGaa95/NetherSX2_nx/tree/f084dc1038c8c67cdff0fa9ad109bce3efe58f22).
O [leitor de recursos](https://github.com/NaGaa95/NetherSX2_nx/blob/f084dc1038c8c67cdff0fa9ad109bce3efe58f22/third_party/lsfg-vk/lsfg-vk-backend/src/extraction/dll_reader.cpp)
lê RT_RCDATA do PE como dados. O
[registro de shaders](https://github.com/NaGaa95/NetherSX2_nx/blob/f084dc1038c8c67cdff0fa9ad109bce3efe58f22/third_party/lsfg-vk/lsfg-vk-backend/src/extraction/shader_registry.cpp)
seleciona os shaders SPIR-V por ID. Não chama Init/Activate da DLL.
A [ponte de apresentação](https://github.com/NaGaa95/NetherSX2_nx/blob/f084dc1038c8c67cdff0fa9ad109bce3efe58f22/source/lsfg/lsfg_bridge.cpp)
entrega os frames Vulkan ao LSFG-VK, sincroniza as filas e apresenta os frames
gerados junto com os originais.

DLL examinada: versão 3.2.2.0, 7.521.280 bytes,
SHA-256 `626b196d799606cd4250b7b29e04228692ab70cf56a5d1bbb56d748c8219f0eb`.
Encontrados 300 recursos RCDATA e 98 shaders SPIR-V de computação.
Todos os recursos selecionados pelas quatro variantes do registro examinado
(FP16/FP32, qualidade/desempenho) existem e passam pela verificação estrutural
de SPIR-V. Isso não valida interfaces de descriptors, criação de pipelines,
suporte do driver, qualidade da imagem nem funcionamento em tempo real.

## Verificação reproduzível

```powershell
python tools/inspect_lsfg_dll.py "C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\Lossless.dll" --output logs/lsfg_dll_inspection.json
```

O script usa apenas a biblioteca padrão Python e lê o arquivo como bytes.
Não executa a DLL e não salva os shaders. Retorna 0 quando pelo menos uma
variante corresponde ao registro, 2 quando nenhuma corresponde, 1 para erro.
O relatório mantém `runtime_validated: false` até existir validação real.

## Trabalho necessário no renderer

O executável atual apresenta através de D3D12. A ponte do Switch recebe
VkDevice, VkQueue, VkImage e VkSwapchainKHR; seus handles não podem receber
ID3D12Resource/IDXGISwapChain diretamente. Copiar a ponte ou carregar a DLL
não implementa geração de quadros neste executável.

A rota que preserva o renderer atual é portar o backend LSFG-VK para Windows
e acrescentar uma ponte D3D12/Vulkan na apresentação:

1. Selecionar no Vulkan a mesma GPU do D3D12, por LUID. Verificar suporte a
   memória externa Win32 e semáforos compatíveis com fences D3D12. Se ausente,
   manter a apresentação atual sem LSFG.
2. Criar texturas compartilháveis para os dois frames reais e um intermediário;
   converter o formato de saída do presenter (R10G10B10A2) para RGBA8 esperado
   pelo contexto SDR do LSFG. Importar os recursos no Vulkan sem leitura pela CPU.
3. Portar a importação do backend: o código de referência usa FDs e
   VK_KHR_external_memory_fd; no Windows são necessários handles Win32.
   As barreiras e as fences devem impedir que o D3D12 reutilize uma textura
   enquanto o Vulkan ainda a processa.
4. Inserir o frame intermediário no presenter com cadência 2x. O primeiro frame,
   mudanças de tamanho, reset do dispositivo e desligamento de LSFG precisam
   invalidar o histórico e devolver os recursos ao renderer.
5. Expor uma opção desligada por padrão e um caminho configurável para a DLL.
   Medir separadamente FPS reais, FPS apresentados, tempo de GPU e latência.
   Verificar gameplay, HUD, menus, alt-tab e redimensionamento antes de habilitar.

Referências para a ponte Windows: [importação de memória Win32 no Vulkan](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryWin32HandleInfoKHR.html)
e [semáforos externos compatíveis com fences D3D12](https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalSemaphoreHandleTypeFlagBits.html).

O LSFG-VK vendorizado é GPL-3.0-or-later; uma incorporação deve preservar sua
licença e obrigações de distribuição. A DLL e seus shaders permanecem fornecidos
localmente pelo usuário, fora do repositório.

## Primeira etapa implementada

O backend LSFG-VK agora compila no Windows em `port/third_party/lsfg-vk`, com
origem e alterações registradas no README do código vendorizado. A biblioteca
`sr_lsfg_interop` seleciona a mesma GPU por LUID, verifica suporte à importação
e compartilha texturas RGBA8 e fences de timeline D3D12 com Vulkan. Handles
Win32 temporários são fechados depois da importação; os recursos têm RAII.

O teste `sr_lsfg_smoke` envia duas imagens sintéticas pelo D3D12, copia-as para
o contexto Vulkan, executa os shaders reais da DLL e lê o intermediário pelo
D3D12. A ida e volta usa recursos e fences compartilhados; leitura pela CPU
ocorre somente para verificar a imagem final do teste. Não há captura de janela.

Foi necessário inicializar os slots temporais de features do LSFG, além dos
dois frames RGB: sem isso, a primeira interpolação apresentou artefatos e
reprovou. O teste usa quatro frames estáticos para inicializar o histórico e
depois verifica movimento entre posições distintas. Essa etapa terá de ser
respeitada também ao ligar LSFG, redimensionar ou recriar o dispositivo no jogo.

Compilação opcional, desligada por padrão:

```powershell
.\tools\setup_lsfg.ps1
$env:SR_LSFG = 'ON'
.\build.cmd
.\port\out\build\win-amd64-release\src\lsfg\sr_lsfg_smoke.exe "C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\Lossless.dll" logs/lsfg_generated.ppm
```

O terceiro argumento opcional é `quality` ou `performance`; o padrão é
`performance`. O teste usa FP32 e imagens 256×256. Na RTX 2060, o modo
desempenho produziu centroide horizontal 110,524 entre os centros originais
95,5 e 127,5; o modo qualidade produziu centroide 108,403. Ambos passaram.
Isso valida uma interpolação sintética e a ponte de recursos,
sem medir desempenho ou qualidade em gameplay. O log fica em
`logs/lsfg_smoke.log` e `logs/lsfg_quality.log` na sessão de validação.
O build opcional compilou e a suíte Python permaneceu em 35 testes aprovados,
1 ignorado. O build padrão, com `SR_LSFG=OFF`, também passou. Os casos de DLL
ausente, arquivo PE inválido e saída tentando sobrescrever a DLL foram
rejeitados pelo teste com erro explícito. Não houve teste na Intel UHD,
em 1280×720 nesse teste sintético.

## Integração experimental na apresentação

`SR_LSFG=ON` também compila o presenter do projeto e a ponte em
`port/src/lsfg/presentation.cpp`. O sistema gráfico cria esse presenter
explicitamente quando `--sr_lsfg=true`: o SDK é uma DLL e seu factory interno
não é substituído apenas por compilar métodos de mesmo nome no executável.
O build padrão e a opção `sr_lsfg` permanecem desligados por padrão.

A ponte captura o quadro composto BGRA8 do swapchain (depois da conversão
normal da saída R10G10B10A2), converte para RGBA8 na GPU, atualiza o histórico
Vulkan e apresenta um intermediário seguido do original. Não lê imagens pela
CPU. Cada serial de saída do guest é processado uma vez; a inicialização usa
quatro passes estáticos. Mudanças de tamanho ou swapchain recriam o contexto.
Uma falha de inicialização desativa LSFG e mantém a apresentação normal.
Uma falha depois de substituir o backbuffer segue o tratamento de perda de
dispositivo do runtime, pois já não é seguro apresentar esse buffer como original.

Validação no jogo: `logs/lsfg_game3.log` confirma o factory do projeto,
inicialização em 1600×900, FP32 performance e mais de 7.000 quadros reais
processados, com um intermediário por quadro depois do aquecimento do histórico.
Não houve erro de LSFG no trecho observado. Isso confirma a execução da
ponte, sem provar ganho de FPS real, fluidez ou latência.
Os builds completos com `SR_LSFG=ON` e `OFF` passaram nesta etapa, e os
35 testes Python continuaram aprovados, com 1 ignorado.
Os objetos de `sr_native` também compilaram com `SR_NATIVE=CAPTURE` e
`SR_LSFG=ON`; isso verifica a interface do factory nesse caminho, sem validar
o renderer nativo em tempo de execução.

### Verificação visual posterior

Depois de fechar manualmente o aviso de firewall, foram comparadas execuções
com `performance`, `quality` e LSFG desligado no mesmo executável. Os logs são
`logs/lsfg_game4.log`, `logs/lsfg_game_quality.log` e
`logs/lsfg_game_baseline.log`. As capturas foram feitas na tela de título e
na sequência de demonstração, sem validar gameplay controlado.

Ambos os modos de LSFG apresentaram deformações e duplicação de texto,
bordas de prédios e personagens em movimento. As amostras sem LSFG não
mostraram as mesmas deformações acentuadas. Não houve comparação sincronizada
quadro a quadro nem medição de fluidez/latência; estes resultados não aprovam
a qualidade visual para uso padrão. O teste sintético anterior é insuficiente
para validar conteúdo real com HUD, texto, movimento de câmera e cortes.

No modo qualidade, maximizar e restaurar a janela recriou o contexto de
1600×900 para 1920×1051 e novamente para 1600×900, reinicializou o histórico
e retomou os contadores, sem erro de LSFG registrado. Com `--sr_lsfg=false`,
o log confirmou a opção desligada, sem criação do presenter LSFG nem
inicialização da ponte. O jogo foi deixado aberto nesse modo para comparação.

Pendências antes de recomendar a função: investigar fluxo óptico e histórico
em cenas reais, proteger texto/HUD e cortes de cena, testar gameplay controlado
e comparar desempenho e latência com uma cena reproduzível. LSFG continua
desligado por padrão; não há afirmação de aumento do FPS real.

Compilação separada, sem substituir o executável habitual:

```powershell
$env:SR_LSFG = 'ON'
$env:SR_RUNTIME_OUTPUT_DIR = "$PWD\port\out\lsfg-test"
.\build.cmd
& .\port\out\lsfg-test\superman_returns.exe --game_data_root="$PWD\game" --fullscreen=false --sr_lsfg=true --sr_lsfg_dll="C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling\Lossless.dll" --sr_lsfg_mode=performance --sr_renderer=xenos --sr_render_scale=100 --sr_skip_intro=true --mnk_mode --mnk_mouse
```

`sr_lsfg_mode` aceita `performance` e `quality`; as opções exigem reiniciar.
Remova `SR_RUNTIME_OUTPUT_DIR` para retornar à pasta de saída habitual.
O caminho experimental ainda usa esperas síncronas da GPU e meia duração do
quadro no thread de apresentação. Pode acrescentar latência e reduzir o FPS
base; uma fila de geração e apresentação independente depende de medições
e validação adicional antes de habilitar por padrão.

Geração de quadros acrescenta trabalho na GPU e não acelera a simulação nem
reduz o custo dos draws Xenos. Ela deve ser avaliada separadamente das
otimizações que buscam aumentar o FPS real, sobretudo na Intel UHD.
