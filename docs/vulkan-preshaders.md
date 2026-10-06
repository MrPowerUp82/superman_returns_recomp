# Pré-shaders do Vulkan nativo

O renderer carrega `superman_returns_vulkan.srvk` ao lado de `superman_returns.exe`.
A biblioteca contém os containers originais e o SPIR-V com os requisitos e
interfaces usados pelo Vulkan. Um shader reconhecido fica pronto imediatamente;
um shader ausente continua usando o compilador de runtime. Arquivos inválidos
são rejeitados por inteiro, com diagnóstico no log e compilação de runtime.
O D3D12 continua usando sua biblioteca `.srsl` e ferramentas próprias.

## No instalador do navegador

Selecione seu ISO ou pasta e mantenha **Gerar e incluir pré-shaders Vulkan no
pacote** marcado. A página lê os `.AST` em blocos, traduz com o XenosRecomp
corrigido deste projeto e compila com DXC em WebAssembly, em um worker. Progresso
e cancelamento ficam visíveis. Os arquivos do jogo não são enviados ao servidor.
O pacote inclui a biblioteca e um relatório; também é possível salvar somente
o `.srvk` e colocá-lo ao lado do executável. Para atualizar o build com novos
pré-shaders, selecione o jogo novamente; sem geração, a atualização continua
podendo ser feita sem os arquivos do jogo.

A geração no navegador encontra containers não comprimidos nos `DATA/*.AST`:
no jogo local, encontrou e compilou **160 shaders, sem falhas**. Ainda não
descomprime o executável XEX nem reproduz shaders criados dinamicamente pelo
jogo. Essas fontes continuam no runtime. Pipelines do driver são específicos da
GPU e continuam sendo compilados/cacheados pelo jogo: pré-shaders não eliminam
todo engasgo nem alteram o FPS sustentado por si só.

A biblioteca só é usada por builds com este suporte; builds anteriores ignoram
o `.srvk`. A página e um build atualizado precisam ser publicados juntos.

## Geração local

Após extrair o corpus e construir o emissor Vulkan:

```powershell
powershell -File tools/build_vulkan_m2.ps1
python tools/shaders/make_vulkan_preshaders.py --install port/out/build/win-amd64-release
python tools/shaders/verify_vulkan_preshaders.py artifacts/shaders/superman_returns_vulkan.srvk
```

O corpus local com dumps de runtime tem 462 containers: **372 aceitos e 90 com
falhas do tradutor/compilador já existente**. A geração escreve a parte válida,
um relatório de falhas e retorna 1 quando a cobertura é parcial. Não transforma
falhas em shaders substitutos. Os dados gerados ficam em `artifacts/` e na pasta
local do executável; não entram no Git nem no ZIP público do build.

## Ferramentas WebAssembly

`docs/wasm/` contém somente compiladores e o header de suporte, sem shaders do
jogo. Reproduzir o emissor exige Emscripten 4.0.23 instalado em `.tools/emsdk`:

```powershell
powershell -File tools/shaders/build_wasm.ps1
node tools/shaders/generate_wasm_preshaders.mjs game
python tools/shaders/verify_vulkan_preshaders.py artifacts/shaders/wasm/superman_returns_vulkan.srvk
node --test tests/web/vulkan-preshaders.test.mjs
```

O emissor usa a árvore isolada de `prepare_vulkan_emitter.py`, com as mesmas
correções de ABI do runtime. O DXC WebAssembly genérico vem de
[ShadowDusk](https://github.com/kaltinril/ShadowDusk/tree/c3768aa53f54257ba5a14a7dea185227150e0499/.wasm-build),
com revisão e SHA-256 fixados pelo script. É DXC 1.7.2212; o runtime local usa
DXC 1.9. Ambos recebem os mesmos argumentos de `vulkan_contract.py` e produzem
SPIR-V para Vulkan 1.1. Não se afirma identidade binária entre essas versões.
A verificação Python reconstitui cada resultado a partir do SPIR-V para conferir
independentemente a reflexão produzida em JavaScript. As licenças e procedência
estão em [wasm/README.md](wasm/README.md).

O fluxo de geração local no navegador foi inspirado pelo
[instalador NFSMW-NX](https://stevensnd.github.io/nfsmw-nx-installer/) e sua
[documentação de shaders](https://github.com/StevensND/nfsmw-nx/blob/main/docs/shaders.md).
O emissor, opções de compilação e formato da biblioteca deste projeto são próprios:
os módulos específicos de shaders do NFSMW não são usados.

## Formato SRVKLIB v1

Inteiros little-endian. Header: magic `SRVKLIB\0` (8 bytes), versão u32 = 1,
contagem u32 e FNV-1a 64 do corpo. Cada entrada: stage u32 (0 VS, 1 PS), tamanho
do container u32, tamanho do resultado u32, reservado u32 = 0, container e
resultado SVR3 v1. SVR3 carrega requisitos, interfaces e SPIR-V. A lookup compara
stage e **todos os bytes originais**; colisões de hash não misturam shaders.
Limites: biblioteca 256 MiB, 16.384 entradas, container 1 MiB, resultado 17 MiB.
Mudanças incompatíveis de ABI exigem nova versão do formato e regeneração.
