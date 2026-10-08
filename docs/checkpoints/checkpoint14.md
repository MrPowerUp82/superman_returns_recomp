# Checkpoint 14 — produtor planar: cadeia indireta e cópia dos planos (C11)

Continuação do checkpoint 13. Objetivo desta etapa: resolver a cadeia do
produtor antes de atribuir mudanças de conteúdo a seus unlocks.

## Resultado técnico

Auditoria opt-in `SR_NATIVE_RESOURCE_UNLOCK_AUDIT=1`, no hook já confirmado
do unlock comum. Nos retornos `8235CBD4/E0/EC`, lê com `ReadProcessMemory`
o objeto global `829761D4`, sua vtable e entradas +72/+76. Não altera
registradores, dados guest, hashes nem política de upload. Limite de 16
tuplas objeto/vtable/alvos; contadores de leituras recusadas e excesso.

Cadeia observada nas duas APIs:

`8235CA48 -> 824707B0 -> vtable+72 = 8247E9D0 -> vtable+76 = 82480D80`

Vtable `82050038`; ponteiros de objeto variam entre execuções.
A amostra ocorre após o produtor retornar: não é um trace da chamada,
nem prova de identidade durante toda a execução ou ausência de reutilização.

`8247E9D0` tem sete instruções: preserva o descritor em r6, passa
`descritor[1]` em r4, `descritor+8` em r5 e chama a entrada +76.
`82480D80` tem 717 instruções. Seu ramo `descritor[0]==1` usa largura/altura
nos offsets 232/236 do objeto, exige dimensões pares, obtém um buffer de
outro objeto e copia três planos. Destinos: descritor+12/+16/+20; strides:
+36/+40/+44. Há cópia contígua e por linha; crominância tem largura/altura
pela metade. Outro ramo (`==255`) contém conversão vetorial para saída
empacotada. Isso identifica entrega/cópia de frame; codec e decodificador
continuam sem confirmação. Não substituir essa rotina inteira por memcpy.

Relatório reproduzível: `python tools/analysis/texture_unlock_candidates.py
--out logs/native_c11_unlock_candidates.json`. Verifica o shape exato do
adaptador de sete instruções; `--target HEX` permite investigar outros alvos.
Registra instruções/chamadores e SHA dos comentários PPC da imagem do dono.

## Limite e próximo passo

Não foi implementada ainda a correlação temporal por entrada do cache.
Faixas iguais não identificam objetos: views, destruição/reuso e unlocks
concorrentes precisam ser considerados. Próxima etapa: observar entrada e
saída da entrega de frame, incluindo descritor, alvos e resultado; comparar
sequências antes/depois do hash com baseline por entrada/API. Distinguir
primeira observação, mudança, corrida e reutilização antes de experimentar
versões de produtor no cache. Hashes permanecem ativos; sem ganho de FPS
comprovado.

## Validação e artefatos

- Release passou; suítes existentes: 120 casos nativos e orçamento de
  alocações, 88 Vulkan, 3 testes Python do perfil e 13 de textura com SDK.
  Shape do adaptador validado pelo relatório estático. Parser PowerShell OK.
- Ensaios finais `native_c11_chain_{d3d12,vulkan}` completaram gameplay
  parado/em movimento. Sem build/testes pesados durante a amostragem.
- D3D12: frame cumulativo final 3720; objeto `4029CC90`; 2247 amostras.
  Vulkan: frame 2280; objeto `402C0DD0`; 2211 amostras. Uma tupla por API,
  zero leituras recusadas/descarte por limite. Totais conferidos contra os
  três chamadores. Não são contagens de frames decodificados: cada entrega
  pode produzir três unlocks e a leitura ocorre após o retorno.
- D3D12: 1992072 checks de conteúdo / 1962 mudanças; Vulkan: 87375 / 1952.
  Zero mudanças sem notificação física observada; isso não prova cobertura
  dos unlocks. Correlação por faixas: 51/49 conjuntos; fetch exato: zero.
- O contador de amostras D3D12 já era 2247 em frame 2160 e permaneceu
  assim até 3720. Nesta execução a atividade observada concentrou-se antes
  do fim do gameplay; não foi demonstrado gargalo dessa rota em gameplay.
- Gates contra C9 passaram: D3D12 34,09 dB / histograma 0,02725;
  Vulkan 39,26 / 0,00865. Cena/HUD inspecionados nas duas APIs.
  A falha antiga do gate C7 Vulkan continua sem resolução.
- Primeiro piloto D3D12 `native_c11_producer_d3d12` capturava apenas +72;
  não é a build final. Primeira tentativa Vulkan falhou na checagem final
  de gameplay; imagem mostrava cena 3D sem HUD. Descartada. Imagens
  preservadas com sufixos `vulkan_failed_*` e `vulkan_not_gameplay.png`;
  seu log não foi preservado pelo script antigo. Repetição passou.
- `bench_api.ps1 -Profile` agora copia o log em `finally`, mesmo quando
  gameplay/gate falha. O próximo lançamento limpa `game.log`.

Reprodução: ambas as variáveis `SR_NATIVE_RESOURCE_UNLOCK_AUDIT=1` e
`SR_NATIVE_TEXTURE_AUDIT=1`; replay, source probe e shared constants cache
desligados. `tools/bench/bench_api.ps1 -Api <api> -Name
native_c11_chain_<api> -Profile -Exe <Release/superman_returns.exe>`.

Resumo: `tools/bench/summarize_resource_unlock_audit.ps1 -NamePrefix
native_c11_chain` -> `logs/native_c11_chain_summary.json`.
Logs/imagens: `logs/bench_native_c11_chain_{d3d12,vulkan}.*`.
Gates: `build/compare_native_c11.ps1` (Windows PowerShell 5.1),
`logs/native_c11_image_gates.json`. Build: `build/native_c11_producer_build.log`;
SDK: `logs/native_c11_guest_texture_tests.log`.

Executável preservado:
`artifacts/native-renderer-performance/native_c11_producer_audit.exe`, SHA-256
`8c9c641dddfe58cedadde132773c3df1be2f612b5d7f39e4a8e5b944048e927e`.
Requer dependências de runtime da pasta Release. Diagnóstico desligado por
padrão; não há nova otimização habilitada.

Alterações anteriores preservadas. Sem commit/publicação.
