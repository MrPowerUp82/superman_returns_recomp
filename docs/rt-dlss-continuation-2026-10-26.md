# RT e DLSS — continuação de 26/10/2026

Escopo vigente: `superpowers/specs/2026-10-23-native-renderer-parity-design.md`
e `superpowers/plans/2026-10-24-rt-dlss-menu-d3d12.md`. A arquitetura aprovada
permanece D3D12 primeiro, sobre o renderer existente. Este relatório registra
avanços parciais; não declara RT Reflections, Character Shadows ou DLSS concluídos.

## Movimento da capa

Foi implementado um contrato específico para VS A5232A3184AA617C / PS
71CB82D791DE8E77. A observação deve conter a chamada 822F43C0 dentro de
82301510/82021DA8, geração viva do objeto e geração de topologia observada.
São verificados os quatro streams reais, endereços físicos, tamanhos,
strides 16/16/8/16, offsets zero, IB 16-bit, QuadList, argumentos e sequência
congelada de índices expandidos. O caminho GPU reexecuta o VS original.

O produtor 822F3FB0 copia os arrays de posição, normal e tangente por slot,
sem compactação/permutação, para os buffers selecionados pelo anel. A identidade
temporal pode portanto preservar esses slots através da troca de buffer, desde
que proprietário/topologia sejam os mesmos. UV e IB permanecem identificados
por recurso/geração/versão; os índices expandidos reais entram na chave.
As versões reais dos buffers dinâmicos continuam no detector de ambiguidades
dentro do quadro. Nenhuma regra genérica de identidade por endereço foi relaxada.

O banco float e os bools realmente enviados são preservados; a projeção desta
família está em c7..10, após a transformação de mundo c3..6. Quadros anteriores
continuam exigindo continuidade, dimensões e recursos retidos. A variante do PS
acrescenta somente o MRT de ownership, após o tail original de cobertura, com
teste inverso de preservação do código e auditoria exata de seu bloco de posição
de pixel. Um passe independente atribui pixels exclusivamente à capa, mantendo
os outros draws capturados como invalidadores.

`motion_cloth_finish_2610a` comprovou os bindings do produtor em dois quadros,
161 vértices/132 quads, rotação 0→1, antes da integração de movimento.
`motion_cloth_correspondence_2610b` capturou uma cinemática e foi rejeitado.
O harness agora exige pausa guest state4 e retorno state3 antes da captura;
não depende somente das cores visíveis do HUD.

`motion_cloth_correspondence_2610c` concluiu menu/retorno e fechamento normal às
14:24:05, mas rejeitou corretamente a correspondência da capa. A causa foi uma
exigência indevida de raw SRV: `BindVertexStreams` não cria esses descritores
no caminho static IA usado pela capa. Um teste reproduziu a falha e a correção
mantém a exigência de recursos residentes/gerações/versões; todo descritor de
fetch não nulo ainda precisa de proveniência não ambígua.

Build final com `SR_DLSS=ON`: passou. SHA256 do EXE:
`3DF57E2C85997D5E29A9A31EEB48FF2F1912E6B8BCCCBCB8E862F5F1BA27D1E1`.
460 testes nativos passaram; CTest 3/3. Logs:
`logs/rt_dlss_finish_cloth_ia_red_2610.log`,
`logs/rt_dlss_finish_cloth_ia_green_2610.log`,
`logs/build_rt_dlss_finish_cloth_final_2610.log`,
`logs/rt_dlss_finish_cloth_verified_ctest_2610.log`.
Revisão independente não encontrou defeitos acionáveis; cobertura de cena e
paths de lifecycle ainda não observados continuam limitando a aceitação global.
Validação GPU da correção static IA: `motion_cloth_correspondence_2610d`
passou, com qualificação por pausa/retorno guest, menu e fechamento normal às
14:28:56. As duas fases qualificaram 792 vértices expandidos da capa; o quadro
seguinte encontrou uma correspondência. Readbacks após fence do passe exclusivo
da capa produziram **19.007 pixels válidos**, todos finitos e com velocidade
não nula (magnitudes 0,3747–16,1999 pixels/quadro). A inspeção da máscara confirma
a forma da capa; os demais draws capturados continuam invalidadores nesse passe.
A cena toda teve 842.143 pixels válidos (91,3784%) nessa captura. Essa porcentagem
não estabelece cobertura global nem uma comparação A/B com outras câmeras.
Dados: `logs/motion_cloth_correspondence_2610d/inspection.json`,
`observed_cloth_motion_{validity,velocity}_s1449.raw/.png`,
`logs/bench_motion_cloth_correspondence_2610d.log`. DLSS continuou Off.

## Entrada HDR e alocação para DLSS

A auditoria de código/capturas identificou o resolve HDR final consumido no
slot0 pelo tonemap VS0408/PS84FD. O mesmo destino guest é resolvido também
mais cedo, antes de fog/composites; apenas o endereço não seleciona a versão
correta. O upscale deve produzir um HDR privado em resolução de saída e
redirecionar somente o input qualificado do tonemap. Luminância, bloom, filtro
e HUD conservam seus contratos. Ainda não há Evaluate NGX nesse fluxo.

Há evidência de source para identificar a dupla default antes do primeiro
clear: `sub_82621258` instala headers embutidos color82986F60/depth82986F20
nos globals82998868/8299886C. `sub_8261F648`/`sub_82616C40` constroem os
headers e backing resources; `sub_8261FAE0` libera e zera os mesmos headers,
e `sub_8261FBD8` limpa os globals. `sub_822EB148` inicializa a configuração
1280x720 e chama a criação. `sub_827867B0` consome a dupla ao criar a view.
O estado82998864 percorre 0/1/2: não é geração monotônica. A implementação
ainda precisa observar construção/teardown, congelar uma geração host nos
comandos e verificar configuração/headers antes da alocação. A dupla default
também é usada na apresentação; não prova gameplay exclusivamente.

Para Quality 1280x720, a query existente forneceu 853x480. O renderer ainda
usa escala escalar em superfícies/resolves, viewport e metadados de amostragem;
853/1280 difere de 480/720. Alocação, retângulos, alias/clears, fixups e sampling
precisam de mapeamentos exatos por eixo antes de ligar DLSS. Também faltam a
qualificação completa de cor/exposição/depth/motion e fallback espacial quando
uma qualificação falha depois de renderizar em resolução reduzida.

## Sombras e reflexos pendentes

O RT Shadows atual executa na família de iluminação já mapeada; as alterações
de movimento acima não ampliam sua cobertura. No log da rodada `finish_2610a`,
caster57A e colorC687 compartilham owner024EEA10/generation1/render82306BB0/
vtable820217E0 e a rota de parte82307298. Isso permite transportar a associação
verificada desse subconjunto, mas não identifica todos os personagens/veículos.
Nenhum caster inspecionado foi associado à capa02D7BE00/generation2. O TLAS
atual ainda agrega geometrias como não-personagem. Character Shadows completo
continua indisponível.

RT Reflections ainda precisa da cena sem viés, atributos e materiais reais por
triângulo, shading iluminado de hit, ordenação captura/build/consumo, residência
fora da tela e rejeição/filtragem temporal. O contrato auditado de receiver e
substituição do ambiente está em `rt-reflection-input-contract-2026-10-25.md`.
O retorno de albedo do teste sintético não satisfaz esse contrato. Os estados
do menu continuam seguindo execução real; esta continuação não os força a On.
