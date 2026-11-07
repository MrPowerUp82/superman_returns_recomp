# Milestone 1: motion vectors de câmera, objetos e deformação

Data: 05/11/2026. Base inspecionada: `2b07a68`. Escrito antes do código e
aprovado pelo usuário em 05/11/2026. A implementação e seus limites são
registrados em `graphics-milestone-1-implementation-2026-11-05.md`.

## Objetivo e sequência

Entregar vetores current-to-previous RG16F em pixels da resolução interna,
incluindo câmera, Superman/capa, NPCs e veículos. Consumidores iniciais: DLSS
D3D12 e, no milestone 2, rejeição/reprojeção temporal do denoiser RT. Contratos
de identidade, quadro e validade ficam independentes de D3D12 para permitir
Vulkan/FSR3 posteriormente. Este milestone não instala FSR3.

Os cinco milestones serão entregues em ordem: movimento; DXR sombras/reflexos;
pacing; cobertura de shaders/PSOs; menu/preferências. Cada um precisa de seu
próprio desenho antes do código e relatório de validação depois. Este documento
só autoriza o escopo técnico do primeiro, não declara os demais implementados.

## Estado comprovado no código

- `native_renderer.cpp::RecordDlssFrame` gera movimento por compute a partir de
  depth e view-projection. Marca `motionCameraOnlyAccepted=true`. Não consome
  `TemporalMotion::Outputs()`.
- `native_motion_capture.inc` contém captura experimental de dois quadros,
  acionada por arquivo. `BeginMotionCapture` para depois do segundo quadro;
  `FinishMotionScene` só rasteriza velocidades na fase 2. Logo não basta ligar
  a opção de debug para produzir movimento contínuo.
- `PostVertexClipCapture` reexecuta o VS original com streams/constantes reais,
  e retém clips exatos, incluindo deformação e fixups. `TemporalMotion` já
  rasteriza current/previous com interpolação correta de perspectiva, depth
  EQUAL, RG16F de velocidade e R8_UINT de validade.
- `TemporalGeometryHistory` exige quadros consecutivos, dimensões iguais,
  identidade/generation verificada e ausência de ambiguidade. Ordem dos draws,
  hash de malha e endereço de objeto isolados não estabelecem correspondência.
- Existem contratos auditados para parte de mundo estático, skin e capa.
  Cobertura completa de NPCs/veículos ainda precisa ser comprovada em gameplay;
  o experimento anterior de capa não demonstra cobertura de todas essas classes.
- O menu existente na pausa contém cinco opções RT/DLSS. DLSS só tem Off/Quality.
  O launcher grava um INI por instalação em LocalAppData; o menu grava um
  companion `.graphics.toml`. A unificação exigida pertence ao milestone 5.

## Abordagem e alternativas

A escolha técnica proposta é promover o caminho pós-VS existente a produtor
contínuo e conectar sua saída ao DLSS. Para geometria rígida, o resultado é a
reprojeção das posições pelos transforms atuais/anteriores; para skin e capa,
preserva também os inputs anteriores da deformação. Guardar os clips anteriores
produzidos pelo VS equivale a reexecutar os transforms anteriores para os mesmos
vértices e evita reconstruir incorretamente um shader guest arbitrário.

Uma variante MRT com transforms atuais/anteriores dentro de cada VS reduziria
o replay, mas exigiria alterar todos os shaders traduzidos e seus bindings,
incluindo paletas e streams deformáveis. Fica como otimização após a validação
do produtor existente. Matriz anterior por índice de draw seria menor, mas
associa objetos errados quando a ordem muda e não atende à deformação; é
descartada. O caminho camera-only permanece disponível para comparação explícita.

## Contrato por objeto e draw

Congelar no frontend, junto de `WorkCmd`, um token de instância viva e geração,
identidade de parte/submalha/topologia, domínio de câmera, ordem de vértices
expandida, recursos/versões retidos e constantes usadas pelo draw. Aproveitar
os observadores existentes de lifetime, skin e capa; ampliar somente a partir
de evidência do produtor real. Qualquer novo hook deve constar no manifesto
ou em midasm documentado com endereço, ABI, chamada original e justificativa.

Para veículos e famílias ainda desconhecidas, capturar dois quadros de gameplay,
rastrear o produtor/transform e provar correspondência antes de habilitar seus
vetores. Troca de buffer dinâmico pode preservar identidade somente quando
há prova de slots/topologia estáveis. Reuso de endereço, mudança de LOD,
destruição/recriação ou duplicidade conflitante invalidam o histórico da parte.

Conservar os slots atuais de captura (três arenas limitadas de 64 MiB) e os
limites de 4096 draws/4.194.303 vértices. O slot anterior não pode ser reutilizado
até terminar seu consumo GPU pelo quadro seguinte. Reter também conteúdos,
depth/ownership e descritores; fence de término do produtor isoladamente não
autoriza sobrescrever dados ainda lidos pelo consumidor. Estouro invalida a
saída daquele quadro, registra motivo e retorna ao caminho normal sem fatal.

## Produção contínua e cobertura

Separar o estado do diagnóstico de dois quadros do estado de produção.
Produção começa quando DLSS ou outro consumidor temporal solicita inputs,
captura cada quadro elegível e sela o histórico na fronteira de cena auditada.
Sem consumidor, não aloca/reexecuta geometria. Readbacks e logs por draw ficam
exclusivos do diagnóstico, fora da medição de desempenho.

Congelar depth antes de clear/alias/reuso. O produtor e `RecordDlssFrame` devem
referenciar a mesma cena/câmera, dimensões, serial e geração de superfícies.
A composição final deve concluir antes da avaliação NGX no filtro auditado.
Se a cena não for qualificada, invalidar o histórico e usar o fallback normal.

Transportar um ticket imutável de linhagem de cena: frame/guest swap, câmera,
geração e serial da última escrita HDR, par color/depth e snapshot de depth.
O ticket acompanha cada versão produzida por resolve/cópia completa auditada;
destino/endereço/dimensões sem serial não bastam. Escrita parcial, clear,
alias/reuso ou produtor desconhecido invalida a tag. Propagar através dos
passes de pós-processamento somente depois de auditar seus inputs reais,
registrando o slot que transporta a cena principal e as dependências auxiliares.
Tonemap VS0408/PS84FD e filtro B397 são pontos conhecidos a verificar; não
assumir que um resolve anterior no mesmo endereço é o input final correto.
Fog/transparência e demais compositores entre HDR e filtro fazem parte da
auditoria de linhagem, não são implicitamente ignorados.

A autorização de NGX exige que o input real da cadeia até B397 descenda do
mesmo ticket que produziu clips/depth, sem escrita desconhecida no caminho,
e que sua configuração espacial não tenha mudado. Se houver escala/warp, só
aceitar com transformação dos inputs temporais explicitamente auditada; caso
contrário rejeitar. Logs diagnósticos mostram a cadeia de recurso/versão/pass
e o primeiro ponto de rejeição. A máscara de ownership e o snapshot de depth
permanecem ligados ao ticket original mesmo depois de o depth guest ser limpo.

Cada fragmento qualificado calcula:

```
currentUV  = currentPixel / renderSize - currentJitter / renderSize
previousUV = previousClip.xy / previousClip.w * (0.5, -0.5) + 0.5
             - previousJitter / renderSize
MV         = (previousUV - currentUV) * renderSize
```

Isso compõe movimento de câmera, mundo e deformação no mesmo vetor. Não somar
novamente um vetor de câmera ao vetor completo. Quando o produtor usa o mesmo
vértice rigidamente transformado, os clips correspondem a `P*V*W*position` de
cada quadro. Para skin/capa, a posição também depende da pose/stream de cada
quadro. Velocidade zero e histórico inválido continuam estados distintos.

Preservar cull, scissor, winding, bias e cobertura real do PS. O MRT de ownership
auditado identifica o último fragmento coberto no quadro atual; não substitui
identidade temporal. Não promover alpha/translucência desconhecida a válida.
Draws desconhecidos devem invalidar as regiões afetadas na ordem correta,
impedindo que o vetor de um objeto oculto apareça sobre o objeto visível.

Um passe de composição reutilizável seleciona o vetor de objeto válido sobre
o fallback de câmera para superfícies comprovadamente estáticas. Pixels de
objetos sem identidade não podem ser silenciosamente classificados como
estáticos. Expor máscara de validade e estatísticas por família para identificar
essas lacunas; não marcar cobertura completa enquanto existirem nas classes
de aceitação.

A primeira implementação usa fallback de QUADRO INTEIRO, decidido antes de
NGX por qualificação conservadora no CPU: todo draw que possa contribuir à
cena precisa de contrato de cobertura/identidade/histórico ou ser um produtor
estático/sky com fallback de câmera auditado. Qualquer draw desconhecido,
histórico ausente/ambíguo, translucência sem política comprovada ou overflow
rejeita a avaliação naquele quadro, apresenta raster normal e marca reset
para a próxima avaliação. A máscara R8_UINT não é enviada como se fosse uma
entrada de reset por pixel de NGX. Sem readback síncrono para autorizar frames.
Resets globais mantendo NGX ativo e composição parcial de raster ficam fora
desta implementação. Registrar frames elegíveis/rejeitados e causas por classe;
uma taxa alta de fallback não satisfaz a aceitação do milestone. O A/B legado
camera-only é um modo diagnóstico explícito, sem atestar movimento completo.
O produtor continua acumulando/selando histórico enquanto apresenta raster;
isso permite qualificar o quadro seguinte e evita um ciclo permanente de
rejeição por falta de histórico inicial.

## Integração NGX

Manter SDK oficial instalado 3.7.20 e seu adapter. `MVLowRes` corresponde à
resolução de renderização; high-res só se as dimensões do buffer passarem a
ser as dimensões de saída. A convenção é previous-minus-current, pixels,
`InMVScaleX/Y=1`. Vetores com jitter removido não usam `MVJittered`. O jitter
real aplicado à cena continua sendo informado em `InJitterOffsetX/Y`.

O SDK recebe o recurso externo pelo campo `pInMotionVectors`; a seleção de
resolução é independente de o produtor ser externo. Não inventar flags para
substituir identidade/cobertura. O adapter atual já configura `MVLowRes`,
`AutoExposure` e opcionalmente `MVJittered`/`DepthInverted`/`IsHDR`.

Referências verificáveis: headers locais `nvsdk_ngx_defs.h` e
`nvsdk_ngx_helpers.h` (campos/flags da versão instalada) e o
[guia oficial NVIDIA](https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf).
O código final deve fixar referências à versão usada, sem atualizar SDK como
parte desta mudança.

Somente atestar `motionIncludesCameraObjectsAndDeformation` para frames com
entradas qualificadas; não alterar esse booleano para contornar gates. Reset
em primeiro quadro, gap, camera cut, resize, reload, troca de modo, perda de
identidade ou falha do produtor. Mudança do modo de inputs invalida o histórico.
Transicionar RG16F para NON_PIXEL_SHADER_RESOURCE antes de NGX e restaurar
estado root/PSO/IA/targets após captura, movimento e avaliação.

## Vulkan e falhas

Metadados comuns não incluem ponteiros D3D12. A implementação D3D12 possui
alocações e transições; uma futura implementação Vulkan consome o mesmo token,
clips/transform/pose e convenção de vetores. Vulkan continua compilando e usando
seu caminho atual. Não anunciar suporte MV/RT Vulkan antes de implementá-lo.

Não adicionar `REX_FATAL`. Falhas opcionais de PSO/alocação, identidade ausente
e NGX indisponível registram estado efetivo e mantêm imagem raster funcional.
Respeitar fences também ao desabilitar, redimensionar e encerrar.

## Implementação e verificação previstas

1. Extrair política contínua de histórico/consumo da captura diagnóstica;
   testar gap, resize, cut, identidade/generation, ordem variável e overflow.
2. Habilitar produtor contínuo, retenção por fence e máscara de cobertura;
   validar geometria rígida, skin e capa antes da conexão ao NGX.
3. Auditar e completar NPCs/veículos observados no percurso. Não ampliar regras
   genéricas de identidade para fazer contadores passarem.
4. Compor inputs de movimento, conectar NGX e manter A/B camera-only explícito.
5. Compilar jogo com DLSS On, adapter sem SDK, testes nativos e testes GPU de
   clip/motion/coverage. Fazer smoke Vulkan e conferir ausência de novos fatals.
6. Rodar benchmark embutido, três vezes por variante, mesma build/configuração,
   sem profiling/readback/compilação concorrente. Registrar média FPS, média de
   frame time, desvio padrão, p50/p95/p99/p99.9 e lows 1%/0,1% dos CSVs.
7. Fazer captura separada de screenshots e sequência curta durante voo, incluindo
   Superman/capa, NPCs e veículos. Comparar camera-only versus objeto+câmera no
   mesmo percurso, com máscaras e magnitudes. Screenshots isoladas não provam
   eliminação de ghosting temporal. O benchmark de título mede custo, mas não
   substitui essa validação de gameplay.

Critério de saída: vetores finitos corretos nas classes exigidas, ausência de
rastro de movimento nos percursos documentados, execução NGX confirmada por
fence, comparação de três rodadas arquivada e regressão Vulkan aprovada. Se
alguma classe não estiver coberta, o milestone continua parcial.

## Evidência desta preparação

O relatório de baseline é mantido separadamente em
`graphics-milestone-1-baseline-2026-11-05.md`. O primeiro ensaio solicitou um dump
no swap 1200, anterior ao início da avaliação NGX: o benchmark terminou válido,
mas o harness de DLSS rejeitou corretamente a captura sem saída upscaled.
Esse ensaio diagnóstico não será usado como média de desempenho.

Revisão independente do desenho em 05/11: duas lacunas iniciais (fallback e
linhagem HDR/depth até B397) foram resolvidas nas seções acima. A segunda leitura
não encontrou perguntas importantes pendentes para planejar a implementação.
