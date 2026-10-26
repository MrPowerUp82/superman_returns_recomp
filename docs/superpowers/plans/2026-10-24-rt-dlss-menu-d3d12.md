# RT, DLSS SR e menu gráfico — plano incremental D3D12

Referência aprovada: `../specs/2026-10-23-native-renderer-parity-design.md`.
Base: `adddef2`, build `build.cmd` passou em 24/10/2026. Branch de trabalho
`codex/rt-dlss-menu-20261024`.

## Dependências comprovadas antes das alterações

- A RTX 2060 reportou D3D12 DXR, mas a consulta de capacidade não executa raios.
- A câmera é extraída de `projection_regs.inc`, ainda sem hash/registrador válido.
- Os IDs `kPassShadowMaps`, `kPassOpaque`, `kPassEndTiling`, `kPassSorted`,
  `kPassUpscale`, `kPassHud` estão em `-1` neste jogo.
- Sem cor/depth de cena e separação de HUD identificados, o blit final não pode
  receber DLSS sem também afetar a interface; sem motion de objetos a reprojeção
  não será correta.
- Não há Streamline/NVNGX, BVH, BLAS/TLAS, DXR shader ou menu de pausa nativo
  específico instalado no renderer.

## Ordem de implementação e testes

1. Criar contrato temporal puro com câmeras atual/anterior, número de quadro,
   dimensões, surface keys, HUD e gates de readiness; testar invalidadores
   (frame gap, resize, camera cut, NaN, falta de depth/HUD/motion).
2. Integrar no executor D3D12 com diagnóstico opt-in em swap, sem transição de
   recurso, alocação por draw ou alteração visual. Somente mapear dado validado.
3. Extrair catálogo de pass/câmera/scene depth e observar ciclo de vida de
   geometria/material com capturas instrumentadas para city, Superman e
   veículos. Validar identidade/geração e cobertura offscreen.
4. Integrar SDK NVIDIA Streamline de versão fixada opcional: consulta real
   em runtime, vetores de movimento inclusive skinning, jitter, cor antes do
   HUD, depth, Quality e reset. Comparar parada/voo e overlays.
5. Construir BLAS/TLAS a partir da extração validada, ray query de sombra,
   iluminação/softness/caster do personagem e denoise; depois reflexos com
   material/roughness, fallback, offscreen e invalidação temporal.
6. Integrar menu `GRAPHICS OPTIONS` à pausa guest (teclado/gamepad, bloquear
   input, sair sem despausar); só habilitar `ON` quando o efeito passou gates
   reais. Persistir solicitações separadamente do estado aplicado.
7. Validar raster Off, RT sombras/reflexos e DLSS Quality em New Game e
   voando na RTX 2060. Registrar fps reais, p95/p99, VRAM, CPU e GPU; portar
   a implementação validada para Vulkan.

Sem implementar a etapa 3, as etapas 4 a 6 não devem ser declaradas prontas.
Quando diagnóstico estiver ativo, imprimir a razão precisa do bloqueio.

## Progresso da etapa 3 (24/10/2026)

Captura comprovada de gameplay, diagnóstico por estado de input triangular
e validação de layout `POSITION0`/stream/bounds em D3D12. Em 2.945 draws,
2.827 tiveram declaração `FLOAT3` inicialmente válida; nenhuma posição
foi ainda transformada/decodificada para world space. Testes nativos e build
principal passaram. Evidências, limites e métricas em
`../../geometry-position-evidence-2026-10-24.md`. RT, DLSS e menu pendentes.

### Avanço da verificação de geometria

O executor D3D12 agora diferencia capacidade do buffer residente e da view IA,
valida a janela de draw não indexado ou dos índices 16/32-bit e registra
explicitamente `index_values_needed` quando ainda não inspecionou os valores.
Uma amostra de posição local `FLOAT3` de draw não indexado pode ser decodificada
do scratch CPU já convertido durante upload integral fresco, sem GPU readback
ou segundo acesso à memória guest. Reuso e uploads parciais não são marcados
como posições validadas. Foram aprovados 283 testes nativos e 2/2 suítes CTest;
`build.cmd` produziu o executável. Falta captura real com os novos contadores,
validação dos índices efetivos e transformação para world space antes de BLAS.

## Marcos posteriores comprovados (continuação em 24/10)

- Backend DXR executou BLAS/TLAS, ray queries, visibilidade Low/High e reflexão
  de albedo em cena sintética na RTX 2060. Extração pós-VS direta dos contratos
  auditados executou BLAS/TLAS e raios na cidade; cobertura continua parcial.
- NVIDIA NGX v3.7.20 opcional substituiu Streamline nesta implementação para
  evitar um interposer no lifecycle do presenter. Initialize/Query/Evaluate
  produziram saída GPU sintética confirmada por fence. No gameplay, somente
  Initialize/Query estão conectados; motion/deformação/jitter/HUD faltam.
- Entrada nativa GRAPHICS OPTIONS na pausa guest verificada, com Y/B/Start,
  supressão e retorno aprovados em duas rodadas isoladas. Persistência de
  solicitações e cvars adicionada; o fechamento normal salvou preferências.
- Depth D24S8 antes da troca de superfície foi comparado com os triângulos
  capturados: 302/576 amostras concordaram dentro de 1%, insuficiente para
  promover a captura a contrato temporal completo.
- 309 testes nativos passaram após as regras de preferências, retenção de Y
  e direção de raios da projeção afim de sombra. O teste GPU também cobriu
  leitura R32/D24, rejeição de clear e detecção de receptor inconsistente.

Esses marcos não concluem as etapas 3–7: faltam cobertura/identidade de cena,
materiais, integração da iluminação/reflexos, entradas temporais e aplicação
real das opções. O painel continua mostrando Off para os efeitos incompletos.
Evidências detalhadas em `../../post-vertex-world-contracts-2026-10-24.md`,
`../../graphics-pause-integration-2026-10-24.md` e `../../dlss-sdk.md`.

## Continuação em 25/10 — marcos parciais, sem conclusão das etapas 3–7

- RT Shadows já executou no gameplay na família PS8A6D/VS2C45, dentro do
  fator de iluminação original. On/Low/High/Off passou com contador GPU e
  fence; demais materiais mantêm raster. Controle separado de personagem e
  RT Reflections continuam incompletos. Ver `bench_rt_inline_menu_2410.log`.
- A persistência foi restaurada após reinicialização sem override CLI.
- Captura pós-VS de clip exato e módulo de movimento com depth EQUAL foram
  integrados ao executor D3D12 como experimento opt-in de dois quadros.
  Cull/winding/bias/scissor, D24/D32 e 4.194.303 vértices passaram no teste
  GPU da RTX 2060. Isso comprova o módulo com fixtures, ainda não os dados
  temporais completos do jogo.
- Primeiro runtime: nenhum dos 646 candidatos estáticos manteve o histórico;
  o consumidor também rejeitou o limite cumulativo de 3M. Corrigidos o limite
  para a arena de 64 MiB e o fingerprint que incluía SRVs transitórios.
  Canonização exige recurso/geração/versão reais; 331 testes nativos passaram.
  Nova validação de gameplay em andamento; não presumir vetores válidos.
- DLSS no gameplay permanece Off: ainda faltam motion de objetos/deformação,
  jitter aplicado, fronteira pré-HUD comprovada e execução NGX nessa cena.


## 2026-10-25: preserve actual alpha-tested pixel ownership for motion

Frozen actual clips/depth replay identifies VS4F172/PS559F as the sampled ground contributor (56000/56000 ROI pixels). Existing static world dependencies support this VS, but generic motion excludes its blend/alpha test. Do not remove that guard. Extend the opt-in capture with an R32_UINT owner target produced by an audited original-PS variant: preserve original color, alpha/discard, depth and raster state; append a current-frame draw ID in a separate unblended MRT. Zero/invalidate unknown writes so stale IDs never supply coverage. TemporalMotion optionally tests this owner ID before writing any velocity/validity; mismatch discards, preserving other draws. IDs provide current-frame fragment coverage only, never cross-frame object/pose identity. Keep matched immutable world-input proof and all frame/fence/resource checks.

Implementation split: pure audited PS instrumentation (559F+2203 only) with distinct cache identity; D3D12 TemporalMotion optional owner-map sampling and meaningful hole/unknown-occluder GPU tests; renderer opt-in allocation/binding/variant-cache integration and captured runtime validation. Original Off path and all unqualified families keep existing behavior. No DLSS activation is implied by this work. This evolves existing shader variants and motion backend; it does not replace the guest frontend.

## Continuação em 26/10 — movimento da capa validado, integração final pendente

O caminho opt-in agora qualifica a capa A523/71CB por ciclo de vida/topologia,
bindings e índices reais, preservando slots através dos buffers rotativos.
Captura do VS original e cobertura do PS original produziram 19.007 pixels
de velocidade finita e não nula atribuídos exclusivamente à capa na RTX 2060.
Build com SDK DLSS passou; 460 testes nativos e CTest3/3 passaram; rodada
`motion_cloth_correspondence_2610d` aprovou pausa/menu/retorno e fechamento normal.

Isso não conclui as etapas3–7. DLSS ainda não executa na cena real; faltam os
domínios de resolução/resolve e a qualificação de entradas para NGX. Há nova
evidência de inicialização/lifecycle da dupla default para qualificar a alocação
antes do primeiro clear. Reflexos e controle completo de casters de personagem
continuam pendentes. Evidências, correções RED→GREEN e próximos contratos em
`../../rt-dlss-continuation-2026-10-26.md`.
