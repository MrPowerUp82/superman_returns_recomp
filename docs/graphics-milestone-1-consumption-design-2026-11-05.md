# M1: consumo temporal e regiões sem correspondência

Revisão do desenho antes do código, autorizada pelo pedido de concluir o projeto.
A guarda `gpu_pixels_verified=false` não tem produtor e bloqueia NGX para sempre;
não constitui implementação do consumidor. A prova CPU permanece por draw/objeto,
mas a política numérica de fragmentos passa a ser explícita em GPU.

O compute compõe movimento pós-VS nas regiões válidas, câmera no clear depth e
zero + máscara de preferência por cor atual nas regiões sem correspondência.
A máscara R8_UNORM, resolução interna, segue `pInBiasCurrentColorMask` do SDK
instalado 3.7.20, não uma suposta flag de reset por pixel. O contrato de avaliação
identifica cobertura parcial com máscara em vez de atestar cobertura completa.
Referência: [guia NVIDIA da geração do SDK](https://raw.githubusercontent.com/NVIDIA/DLSS/d489595a8c5cb19c7a8d770b0cb5bc198cc99018/doc/DLSS_Programming_Guide_Release.pdf), seção Bias Current Color.
Essa política é específica do SDK fixado; versões novas podem mudar suporte.

Selecionar a cena após uma câmera compartilhada 2C45 reconhecida, no seu par
color/depth. Congelar câmera, depth, fixups e serial antes de clear/reuso. Cor
continua sendo consumida na emissão efetiva do filtro B397 antes do HUD, como
no caminho Quality já validado; a máscara cobre lacunas de geometria, não
invalidações de câmera/cena. Resets em troca de modo, resize, gap e camera cut.

Adicionar modos Quality/Balanced/Performance/DLAA ao adapter NGX real. Mudanças
de modo recriam feature somente após o fence que cobre seu uso, preservando
recursos dos slots. Configuração solicitada não é aplicação confirmada.

Validação: fixture do compute para vetor assinado/máscara, contrato sem SDK,
Evaluate/composição/fence no jogo, captura da máscara e sobrevoo travado a 30
FPS (a velocidade da cena de título desbloqueada não é referência temporal).
Não declarar eliminação de ghosting por um contador de chamadas à API.

## Composição posterior ao fechamento geométrico

O trace real `graphics_m1_hdr_trace_20261105` identifica fog 7C8C/4054 e
VFX 0408/201C + 0408/5C1E após o snapshot. Fog amostra SceneSampler slot0
nas mesmas UV; os dois VFX compõem com blend e usam alpha de contribuição.
O histórico de cor segue esses writes auditados, com versão anterior exata,
sem atualizar o snapshot de depth. A variante original dos dois PS VFX mantém
inclusive SV_Depth e adiciona ao MRT de ownership o marcador UINT_MAX quando
alpha contribui. O compute temporal lê esse marcador como preferência por cor
atual. Assim VFX sem correspondência própria não herdam movimento do chão.
Teste deve preservar assinatura original, código de alpha/discard/depth,
marcador por pixel e rejeição de variantes desconhecidas.
## Correções após revisão

MRT de ownership sobrescreve zero em VFX transparente e não pode representar OR
persistente. Manter ownership apenas para a associação original; a rejeição VFX
passa a um R32_UINT UAV por slot, limpo uma vez por quadro, com InterlockedOr
somente quando alpha contribui. u1/space6 tem tabela própria; compute lê esse
mask após a transição UAV/SRV. Dois VFX sobrepostos não apagam rejeição.

Draws sem correspondência e sem escrita de depth precisam reproduzir o compare
de depth original no invalidator, não EQUAL. Mistura de camadas sempre invalida
histórico naquele fragmento. Stencil/alpha não conhecidos podem invalidar área
maior, nunca validar a área do fundo. Captura ausente continua rejeição do quadro;
uma falha de cobertura não pode esconder uma captura ausente.

### Visibilidade dos invalidadores

A captura de regressão revelou zero pixels válidos com ALWAYS para todo draw
sem correspondência: geometria do fundo podia apagar vetores do primeiro plano.
Preservar EQUAL para correspondências e para invalidadores que escreveram depth.
Para contribuintes sem escrita de depth, reproduzir a comparação de depth original
(ALWAYS somente quando o teste estava desativado). Continuar rejeitando alpha
sem cobertura comprovada, com scissor/cull e ordem originais. Pré-aquecer as oito
comparações nos dois formatos; journal mantém ABI com comparação em bits altos.

### Captura contínua em gameplay por trigger existente

Quando motion contínuo está ativo e o caminho diagnóstico de trigger está
configurado, consumir o arquivo para capturar exatamente o próximo quadro
processado: motion/depth/validity, buffers NGX e saída final após HUD. Não mudar
flags de qualidade, identidade ou aplicação; nenhum I/O com caminho vazio.
As famílias/contadores desse quadro são evidência, não classificação de veículo.
Capturas ficam fora de qualquer janela de desempenho. O modo diagnóstico de
dois quadros com DLSS Off conserva seu comportamento.
