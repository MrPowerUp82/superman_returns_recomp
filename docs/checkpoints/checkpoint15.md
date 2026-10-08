# Checkpoint 15 — sequência da entrega planar até seus unlocks (C12)

Continuação do checkpoint 14. Novo hook observacional no thunk `824707B0`,
papel `FRAME_HANDOFF`, confirmado pela forma exata de quatro instruções,
chamador estático `8235CA48` e alvos observados em C11. Papel acrescentado
ao fim do perfil para preservar índices existentes; agora são 23 papéis.

## Implementação

Somente com `SR_NATIVE_RESOURCE_UNLOCK_AUDIT=1`:

- Antes do original, lê com checagem o objeto, entradas +72/+76 da vtable
  e os 48 bytes do descritor. Registra tipo, destinos +12/+16/+20 e strides
  +36/+40/+44. Sequência global crescente para cada entrada.
- Executa o original sem manter mutex e sem modificar argumentos/resultado.
  Depois registra o r3 retornado. Valores negativos e não negativos são
  contados separadamente; retorno não negativo não prova mudança de bytes.
- Guarda apenas a última entrega elegível por thread: chamador `8235CB5C`,
  tipo 1, alvos observados `8247E9D0/82480D80`, leituras completas e sem
  aninhamento. Chamadas aninhadas invalidam a associação.
- Cada unlock nos três sites `8235CBD4/E0/EC` consome uma vez seu plano
  esperado, exigindo acordo da página do destino com a página do recurso,
  incluindo aliases CPU. Máscara 7 fecha o grupo. Duplicatas, páginas
  divergentes e sites sem entrega pendente não completam um grupo.
- Caminhos de falha `8235CB7C/88/94` limpam a associação pendente. Uma nova
  entrega contabiliza eventual grupo anterior abandonado. Memória limitada
  a uma observação por thread; detalhes somente das primeiras 12 sequências.
  Resumo cumulativo a cada 120 frames da frontend.

O relatório estático agora também verifica o shape do thunk `824707B0`.
O resumidor confere retornos por sinal, grupos e totais dos três chamadores.
Testes puros cobrem descritor big endian/curto, strides, aliases/offsets,
fim da arena, destinos errados, planos repetidos e tipo empacotado recusado.

## Limites e próximo passo

Essa associação prova ordem observada e acordo de páginas nos sites
instrumentados; não prova identidade durante reutilização, execução de um
ramo interno específico, mudança de bytes, ou cobertura de todos os produtores.
Entradas da vtable são amostras na entrada, não um trace de cada salto.
As texturas continuam sendo validadas pelos hashes existentes.
O identificador cresce na entrada: não é uma versão de escrita concluída,
pois chamadas em threads distintas podem terminar fora dessa ordem.

Próxima etapa: comparar sequências de produtor/unlock antes e depois da
validação de conteúdo, com baseline por entrada do cache/API. Tratar primeira
observação, views diferentes, grupos incompletos, escrita concorrente e
destruição/reutilização explicitamente. Não usar um único baseline global por
faixa. Ainda não há otimização de upload/hash nem ganho de FPS comprovado.

## Resultados e validação

| API | Frame final cumulativo | Entradas/retornos | Grupos completos | Planos associados |
| --- | ---: | ---: | ---: | ---: |
| D3D12 | 3720 | 268/268 | 268 | 804 |
| Vulkan | 2400 | 262/262 | 262 | 786 |

Todas as chamadas observadas retornaram valor não negativo. Zero leituras
recusadas, formatos/alvos não suportados, aninhamentos, grupos abandonados,
páginas divergentes e unlocks sem associação. Os totais foram conferidos
contra os três chamadores; não são uma taxa geral de cobertura dos produtores.

Primeira sequência D3D12: objeto produtor `402C07C0`, descritor `704FF5D0`,
destinos `EBDC3000/EBD7F000/EBD3B000`, recursos
`40619C90/40619CE0/40619D30`. Vulkan: produtor `402B5160`, mesmo endereço
de descritor na stack, destinos `EBDD3000/EBD8F000/EBD4B000`.
Strides 1280/768/768 nas duas APIs. Cada sequência detalhada mostra entrada,
retorno e máscaras 1/3/7, com acordo individual das páginas esperadas.
Não assumir planos de crominância contíguos com stride 640.

Auditoria de conteúdo: D3D12 2309192 checks / 790 mudanças; Vulkan 110693 /
770. Zero mudanças sem notificação física observada. Correlação por faixa
do diagnóstico anterior: 51/49 conjuntos, fetch exato zero. Esses agregados
não associam uma mudança de hash a uma entrega específica.

- Release passou; 123 casos nativos, orçamento de alocações, 88 Vulkan,
  3 testes Python do perfil e 13 de textura com SDK passaram.
- Relatório estático validou os shapes dos wrappers e do thunk; parser do
  resumidor PowerShell e diff check nos arquivos rastreados alterados passaram.
- Gameplay parado/em movimento passou nas duas APIs. Sem build/testes
  pesados durante a amostragem de FPS.
- Gates contra C9 passaram: D3D12 32,36 dB / histograma 0,05854;
  Vulkan 37,45 / 0,01023. Cena/HUD inspecionados. A falha antiga do gate
  C7 Vulkan continua sem resolução.
- A primeira tentativa D3D12 terminou com `Window closing` antes da primeira
  entrada do produtor; descartada. Log preservado como
  `logs/bench_native_c12_handoff_d3d12_closed.log`. Repetição com a mesma
  build passou. Não foi registrada uma falha do hook nessa tentativa.

Reprodução: `SR_NATIVE_RESOURCE_UNLOCK_AUDIT=1` e
`SR_NATIVE_TEXTURE_AUDIT=1`, demais experimentos C5/C8/C9 desligados.
`tools/bench/bench_api.ps1 -Api <api> -Name native_c12_handoff_<api>
-Profile -Exe <Release/superman_returns.exe>`.

Resumo: `tools/bench/summarize_resource_unlock_audit.ps1 -NamePrefix
native_c12_handoff` -> `logs/native_c12_handoff_summary.json`.
Relatório: `tools/analysis/texture_unlock_candidates.py --out
logs/native_c12_unlock_candidates.json`.
Logs/imagens: `logs/bench_native_c12_handoff_{d3d12,vulkan}.*`.
Gates: `build/compare_native_c12.ps1` (Windows PowerShell 5.1),
`logs/native_c12_image_gates.json`. Build: `build/native_c12_handoff_build.log`;
SDK: `logs/native_c12_guest_texture_tests.log`.

Executável preservado:
`artifacts/native-renderer-performance/native_c12_handoff_audit.exe`, SHA-256
`60d5f042a0c3afad50e38e115604bfb282838d484b01656706be13c78ea99008`.
Requer dependências de runtime da pasta Release. Auditoria desligada por padrão.

Alterações anteriores preservadas. Sem commit/publicação.
