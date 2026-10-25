# DLSS Super Resolution D3D12 (opcional)

Adaptador standalone em `port/src/graphics/d3d12/dlss_super_resolution.h/.cpp`.
Off/Quality, NGX direto, sem Streamline, interposer ou hooks no presenter.
Não modifica renderer, opções, app ou CMake. A integração nesses pontos pertence
ao chamador. Macro ausente ou `SR_HAS_DLSS=0` compila o fallback sem headers,
libraries, DLL ou inicialização NVIDIA.

## Escolha e fonte oficial

A spec `docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md`, seção
DLSS, pede Streamline. A instrução desta implementação autoriza NGX direto;
essa escolha evita o lifecycle anterior ao device/interposer no presenter já
existente. NGX inicializa **depois** da criação do device real. Não relaxa os
requisitos temporais, de suporte ou de execução da spec.

- SDK: [NVIDIA/DLSS v3.7.20](https://github.com/NVIDIA/DLSS/tree/v3.7.20).
- Commit imutável: `a11125971f4d5f19ce17ca8cbe6f703eaa16de76`.
- Runtime release oficial: `nvngx_dlss.dll`, FileVersion/ProductVersion `3.7.20.0`.
- Referência: [guia oficial nesse commit](https://github.com/NVIDIA/DLSS/blob/a11125971f4d5f19ce17ca8cbe6f703eaa16de76/doc/DLSS_Programming_Guide_Release.pdf),
  sobretudo seções 3.3–3.10 e 5; headers `nvsdk_ngx.h`, `nvsdk_ngx_defs.h`,
  `nvsdk_ngx_params.h`, `nvsdk_ngx_helpers.h` do mesmo commit.
- Versão fixada para esta implementação, sem alegação de ser a mais recente.
  Usa `NVSDK_NGX_Version_API` do header fixado, `MaxQuality`, `MVLowRes` e
  `AutoExposure`. Não habilita sharpening, DLAA, Frame Generation ou RR.

## Instalação e licença

```powershell
./tools/setup_dlss.ps1 -SdkRoot .tools/dlss-v3.7.20
```

O diretório `.tools/` já é ignorado pelo Git. O setup obtém somente arquivos
públicos `raw.githubusercontent.com/NVIDIA/DLSS/<commit>/...`, verifica seus Git
blob SHA-1 fixos, verifica Authenticode NVIDIA na DLL e grava todos os SHA-256 em
`dlss-sdk-manifest.json`. Recusa sobrescrever um diretório existente; não faz
cadastro, clickwrap, publicação, instalação global, build ou execução de jogo.
`-RuntimeDirectory <diretório existente>` opcional copia a DLL release validada,
sem sobrescrever uma DLL existente. Não há DLL de outro jogo.

Licença: [NVIDIA RTX SDKs LICENSE e suplemento DLSS/NGX](https://github.com/NVIDIA/DLSS/blob/a11125971f4d5f19ce17ca8cbe6f703eaa16de76/LICENSE.txt),
preservada integralmente no SDK local. É proprietária, não MIT/BSD. Seu texto
associa uso a aceitação; o download/uso local foi explicitamente autorizado pelo
usuário nesta tarefa, sem ação de aceitar termos em portal. SHA-256 da licença:
`21b5daec892b12bea692e66bc8fe45cf5902ccaf3a7b831e78050d8859881c37`.

Redistribuir é uma etapa separada: ler a licença integral, incluindo requisitos
de aplicação com funcionalidade adicional, restrições de distribuição do SDK
isolado/developer tools, termos protetivos, avisos e suplemento de marcas/créditos.
Usar a DLL **rel**, não o developer runtime, e revisar compatibilidade com a
licença do produto antes de publicar. Esta tarefa não publica nem assume que os
requisitos de distribuição já estão atendidos.

SHA-256 dos binários oficiais instalados (o manifesto inclui também os headers):

| Arquivo relativo ao SDK | SHA-256 |
| --- | --- |
| `lib/Windows_x86_64/rel/nvngx_dlss.dll` | `0d5560f8c09e23bca5ff6922e41fe70fefbee2ed9d0d613ea3f12ef971d2d81b` |
| `lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib` | `e3c624cbe3593d263163d1f8a5613312cd2866188223637d60c976d003c49e03` |
| `lib/Windows_x86_64/x86_64/nvsdk_ngx_d_dbg.lib` | `35ecb90852e492432de01019af591a34fcad62f3f3ee8c26e926472e52420593` |
| `lib/Windows_x86_64/x86_64/nvsdk_ngx_s.lib` | `07e2a89ab2745766ee4c55ef9d076bdece656242737b9d70b24e57cb83a7009b` |
| `lib/Windows_x86_64/x86_64/nvsdk_ngx_s_dbg.lib` | `8a2dad7bcfa62b9941530e16b6cfa3defbfc8043acd6d34e0438980c521a8481` |

## CMake: pontos para quem integra

`SR_DLSS_SDK_DIR` deve apontar para a raiz absoluta `.tools/dlss-v3.7.20`.
Apenas quando os arquivos opcionais existem e o recurso foi explicitamente
habilitado, definir `SR_HAS_DLSS=1` no translation unit do adaptador. Adicionar:

| Item | Path/configuração |
| --- | --- |
| Fontes | `port/src/graphics/d3d12/dlss_super_resolution.cpp` |
| Include privado | `${SR_DLSS_SDK_DIR}/include` |
| Release `/MD` | `${SR_DLSS_SDK_DIR}/lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib` |
| Debug `/MDd` | mesmo diretório, `nvsdk_ngx_d_dbg.lib` |
| Release `/MT` | mesmo diretório, `nvsdk_ngx_s.lib` |
| Debug `/MTd` | mesmo diretório, `nvsdk_ngx_s_dbg.lib` |
| Windows libs | `d3d12`, `dxgi`, `dxguid`, `advapi32`, `user32` |
| Runtime | `${SR_DLSS_SDK_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll` |

O sufixo `_d` significa CRT dinâmico; `_dbg` significa Debug. Para builds Debug,
manter também `_ITERATOR_DEBUG_LEVEL` compatível com a library escolhida. O smoke
local usa `/MD`, Release, MSVC x64 14.44.35207, C++20, `/W4 /WX` e headers NVIDIA
como externos `/external:W0`. O projeto pode usar C++23. Se o SDK estiver ausente,
definir `SR_HAS_DLSS=0` e não adicionar includes/link/cópia de runtime NGX.

## API e sequência exata

Namespace `superman_returns::graphics::d3d12`, classe `DlssSuperResolution`:

```cpp
void SetRequested(DlssRequested); // Off ou Quality; não inicializa nem executa.
DlssResult Initialize(ID3D12Device*, const wchar_t* applicationPath);
DlssResult QueryOptimalSettings(uint32_t outputWidth, uint32_t outputHeight,
                               DlssOptimalSettings& out);
DlssResult Evaluate(const DlssEvaluation&);
DlssResult NotifySubmitted(uint64_t ticket, ID3D12Fence*, uint64_t value);
DlssResult ConfirmExecution(uint64_t ticket, ID3D12Fence*, uint64_t value);
DlssResult Shutdown(ID3D12Fence* = nullptr, uint64_t value = 0);
const DlssStatus& GetStatus() const;
DlssError ValidateDlssFrame(const DlssFrameContract&); // função livre pura
```

1. No render thread, após criar o device efetivamente escolhido, chamar
   `SetRequested(Quality)` e `Initialize(device, applicationPath)`. O path é um
   **diretório absoluto existente e gravável**, usado para dados/log NGX e busca
   da DLL oficial. Colocar a mesma DLL ao lado do EXE ou nesse diretório. NGX
   usa um GUID próprio deste custom engine, não um NVIDIA Application ID emprestado;
   nenhuma submissão/cadastro é feita. Apenas uma instância NGX proprietária por
   device/processo nesta integração, device single-node.
2. `Initialize` chama `NVSDK_NGX_D3D12_Init_with_ProjectID`,
   `GetCapabilityParameters` e lê `SuperSampling.Available` e
   `SuperSampling.NeedsUpdatedDriver` no **device fornecido**. Não infere suporte
   pelo nome RTX/vendor. Falha ou ausência mantém fallback raster, sem criar feature.
3. `QueryOptimalSettings(outputWidth, outputHeight, out)` chama
   `NGX_DLSS_GET_OPTIMAL_SETTINGS(...MaxQuality...)`. Usar exatamente os tamanhos
   retornados para renderizar/alocar os inputs. Não hardcodar razão 2/3. Este
   adaptador exige resolução fixa, não implementa dynamic resolution.
4. Renderizar cena, depth e velocidades na resolução interna, aplicar jitter
   real e construir o `DlssFrameContract`. Fazer as transições e chamar `Evaluate`
   na lista DIRECT aberta, antes de HUD/menu. A primeira chamada válida cria a
   feature via `NGX_D3D12_CREATE_DLSS_EXT`, depois executa
   `NGX_D3D12_EVALUATE_DLSS_EXT`. Erro devolve ticket zero: usar raster e registrar
   `lastError`/`lastNgxResult` brutos. Chamadas SDK que falham podem ter gravado
   comandos: manter dependências até o fence, fechar/descartar a lista com cuidado,
   fazer `Shutdown` antes de tentar recriar o recurso.
5. Gravação bem-sucedida devolve `ticket != 0`, estado `PendingGpu`. Fechar a lista,
   executar na mesma DIRECT queue e fazer `queue->Signal(fence, value)`; verificar
   as falhas do renderer. Então chamar `NotifySubmitted(ticket, fence, value)`.
   Sem isso, outro `Evaluate` retorna `SubmissionRequired`, preservando o ticket;
   uma lista abandonada não pode estabelecer histórico nem criação executada.
   Para abandonar definitivamente, descartar a lista e fazer Shutdown seguro.
6. `NotifySubmitted` aceita um fence do mesmo device e valores crescentes, sem
   espera. Somente a submissão bem-sucedida permite reutilizar histórico em ordem
   na mesma queue. A classe não executa nem sinaliza queues. Ela não consegue
   provar que o chamador realmente submeteu o ticket ou não sinalizou pelo CPU;
   essa associação é um contrato obrigatório do renderer.
7. Posteriormente chamar `ConfirmExecution` com o **mesmo ticket/fence/value**.
   `GpuPending` permite continuar raster/estado pendente. Apenas fence concluído,
   device válido e ticket da geração temporal atual aplicam `Quality`. Pode
   confirmar tickets de frames anteriores; até 64 submissões não confirmadas são
   mantidas, evitando memória ilimitada. Off/falha invalida a geração; confirmar
   um frame antigo libera seu registro, sem reativar Quality.
8. O chamador recompõe seu estado D3D12 após qualquer chamada NGX de criação/avaliação:
   heaps, root signatures, tabelas/constants, PSO e demais binds usados em seguida.
   O adaptador não tem acesso ao cache de estados do renderer. Compor HUD/menu
   na resolução de saída. Não usar output após uma avaliação falha.
9. Resize, mudança de formato/convenção de criação ou encerramento: parar de
   gravar, submeter/descartar listas, esperar um fence cobrindo **todas** as
   criações/avaliações e chamar `Shutdown(fence, value)`; liberar recursos depois.
   Resize exige Shutdown+Initialize+Query. Off não destrói recursos em voo.
   Shutdown parcial entra em modo somente teardown, rejeitando query/evaluate.
   O destrutor exige Shutdown prévio após GPU work: assert em debug; retenção
   deliberada de dependências em release se o contrato for violado, evitando UAF.

`requested`, `support` e `applied` são independentes em `DlssStatus`; inicialização
e query nunca fazem applied=Quality. `lastNgxResult` é diagnóstico bruto, não uma
prova de execução. O chamador ainda precisa garantir apresentação/composição do
output: um dispatch concluído não prova por si que a imagem foi apresentada.

## Contratos de recursos/tempo

- Resources distintos do mesmo device, Texture2D, um mip, uma camada, MSAA=1.
  Inputs exatamente no tamanho interno consultado; output no tamanho de saída.
  Sem subrect, array, MSAA, output compartilhado com input ou conversão implícita.
- Color/output com mesmo formato: `R16G16B16A16_FLOAT`, `R11G11B10_FLOAT` ou
  `R8G8B8A8_UNORM`. HDR exige formato float; output precisa UAV typed store.
  Depth: `R32_FLOAT`, `D32_FLOAT` ou `R32_TYPELESS` legível como depth [0,1].
  Motion: `R16G16_FLOAT` ou `R32G32_FLOAT` na resolução interna.
- Inputs permitem SRV, output tem `ALLOW_UNORDERED_ACCESS`. Antes de Evaluate,
  todos os inputs devem estar exatamente `NON_PIXEL_SHADER_RESOURCE`, output
  exatamente `UNORDERED_ACCESS`, todos os subresources. O SDK restaura esses
  estados conhecidos (guia §3.4); read states combinados são rejeitados. O adapter
  valida a declaração do chamador, não consegue consultar estados D3D12 reais.
- `conventionsVerified`, `colorBeforeHud`, `rasterJitterApplied` e
  `motionIncludesCameraObjectsAndDeformation` precisam representar fatos
  validados na captura/renderização. Não basta preencher true ou fornecer textura
  não nula. Motion somente de câmera é rejeitado por contrato e não é solução final.
- Motion após `mvScaleX/Y` deve mapear pixel atual à posição anterior, em pixels
  internos, +direita/+baixo; escalas finitas não zero podem inverter eixos/unidades.
  `motionJittered` descreve se os vetores incluem diferença de jitter; vira flag NGX.
  Matrizes/poses anteriores precisam incluir movimento de objetos e deformações
  de Superman/capa, veículos etc.; o adaptador não gera essas velocidades.
- Jitter real da projeção, pixels internos, finito em [-0,5,+0,5], sinais +direita/
  +baixo. Não fornecer jitter decorativo sem aplicá-lo à cena. Depth Forward ou
  Reversed deve estar verificado; linear Z/convenção desconhecida são incompatíveis.
- Autoexposure NGX está habilitado, sem exposure texture; informar `preExposure`
  finito positivo correspondente à cor. Pipeline de cena/HDR/tonemap e LOD bias
  adequados à resolução interna permanecem responsabilidade do renderer (guia
  §3.5). Nenhuma exposição do guest é inventada.
- `reset=true` em load/cut/teleport/identidade inválida. Primeira avaliação,
  Off→Quality e falhas forçam reset adicional. Manter recursos/listas/descritores
  válidos até conclusão do fence do renderer; não apontar para memória guest reciclada.

## Verificação local e limites

```powershell
./tools/dlss_smoke.ps1            # SDK real: Initialize + QueryOptimalSettings
./tools/dlss_smoke.ps1 -Evaluate  # também dispatch/readback sintético e fence
```

Script separado compila somente os novos fontes, sem build.cmd/CMake/jogo,
usando o SDK instalado. Logs e EXE em `.tools/dlss-smoke/`. O smoke também verifica
que a DLL carregada é a cópia oficial no diretório de teste.

Em 24/10/2026, Windows x64, NVIDIA GeForce RTX 2060, vendor `10DE`, device `1F15`,
LUID `00000000:000167DD`: Initialize e Query Quality retornaram NGX `0x00000001`.
Saída `1920x1080`, interno `1280x720`, min `960x540`, max `1920x1080`, sharpness
informado `0.35` (não aplicado). Query manteve applied Off.

Smoke sintético: plano constante RGB `(0.25,0.50,0.75)`, depth `0.5`, motion zero,
sem movimento de câmera/objetos/deformação, jitter efetivamente zero. `Evaluate`
retornou sucesso e ticket; applied permaneceu PendingGpu na gravação/submissão;
somente após o fence passou a Quality. Readback central:
`(0.2499,0.4998,0.7490)`, substituindo output inicialmente magenta. Rejeitou input
com read states combinados, reavaliação sem submissão, confirmação de ticket não
submetido e Shutdown antes do fence. Recursos foram liberados após fence.

Teste CPU standalone `tests/native/dlss_super_resolution_test.cpp` com o `.cpp`
do adaptador, `/std:c++20 /EHsc /W4 /WX /DSR_HAS_DLSS=0`: passou em validação de
tamanho, motion incompleto/convenções desconhecidas, NaN, escala zero e
preExposure inválido; confirmou fallback/ausência de aplicação sem SDK.
Também executado o binário existente `build/tests-native/sr_native_tests.exe`:
286 testes, zero falhas (não é uma rebuild da suíte concorrente).

**Pendente de integração/aceitação em gameplay:** superfície pré-HUD validada,
jitter da rasterização, depth real, velocidades com objetos/deformação, mudanças
de resolução, frames em voo, device loss e comparação estática/em movimento.
O teste sintético comprova chamadas e resultado GPU NGX; não comprova qualidade
visual em Superman/cidade, fidelidade temporal ou desempenho do jogo.
