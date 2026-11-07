# Co-op: leituras agrupadas de símbolos APT

## Design anterior à implementação

O caminho ativo `coop_apt_probe.cpp::Movie/MeterGeometry` resolve nomes exportados
e controles do HUD com `String`. A função atual faz até 128 chamadas de leitura
protegida, uma por byte. Os caches pertencem ao passe, portanto o custo reaparece
nas duas views e nos próximos frames. O launcher usa esse caminho para stamina P2.

Ler blocos de 32 bytes, sem cache persistente de ponteiros guest. Se a leitura de
um bloco falhar, percorrer somente esse bloco byte a byte: um terminador antes de
uma região inacessível deve continuar funcionando. Preservar limite de 128 bytes,
ASCII imprimível, nulidade e falhas fechadas. Não substituir ReadProcessMemory por
memcpy nem armazenar metadados entre gerações. Nenhum novo hook ou fatal.

Validar comportamento em fronteiras/bytes inválidos e medir chamadas/tempo com o
mesmo backend de leitura Windows. O benchmark de título desativa co-op; não serve
como evidência de ganho de FPS em split-screen. FPS de gameplay exige ensaio com
P2, duas views e save isolado, não inferência a partir do microbenchmark.

## Implementação e medição

`port/src/coop_guest_string.h` implementa a leitura agrupada; `String` no probe APT
usa o mesmo leitor protegido existente. O launcher ativa `apt_probe` e
`stamina_preview` no modo co-op, portanto esse é um consumidor efetivo, não uma
otimização restrita a um caminho desligado no launcher. Não altera câmeras,
simulação, resolução, qualidade, input ou o estado APT compartilhado.

Microbenchmark Windows em `tools/coop/bench_guest_symbol.cpp`: 100.000 símbolos por
rodada, cinco nomes reais do HUD, mesmo `ReadProcessMemory` para os dois leitores,
três rodadas com ordem alternada. Valida conteúdo e checksum, não somente duração.

| Rodada | Byte a byte ms | Agrupado ms | Chamadas antes/depois |
|---|---:|---:|---:|
| 1 | 558,949 | 57,5366 | 1.040.000 / 100.000 |
| 2 | 556,077 | 57,2106 | 1.040.000 / 100.000 |
| 3 | 554,665 | 56,8068 | 1.040.000 / 100.000 |
| Média | 556,564 | 57,185 | 1.040.000 / 100.000 |

Redução de 90,38% nas chamadas e 89,73% no tempo **dessa operação** no cenário
medido. Não é redução de 89,73% no custo do HUD ou do frame. Strings inválidas ou
terminadores próximos a memória inacessível podem usar o fallback e não obter
esse ganho. Não há resultado novo de FPS, frametimes ou controles físicos co-op.

Reprodução em terminal de desenvolvimento do Visual Studio com clang++ no PATH:

```powershell
clang++ -std=c++20 -O2 tools/coop/bench_guest_symbol.cpp -o build/bench_guest_symbol.exe
& build/bench_guest_symbol.exe
cmake --build build/tests-native --parallel 4
& build/tests-native/sr_native_tests.exe
ctest --test-dir build/tests-native --output-on-failure
```

Evidência local: `logs/coop_optimization_20261107/microbenchmark.csv`, `build.log`,
`ctest.log`. A tentativa inicial sem ambiente VS não encontrou cmake; com o
ambiente correto os 733 testes nativos e os 6 targets CTest passaram. A primeira
build do jogo foi bloqueada por divergência de manifest do header instalado
`mnk_button_edges.h`. O setup oficial foi reaplicado à cópia isolada em `.tools`,
sem desabilitar verificação; logs `sdk_setup.log` e `rebuild.log` registram a
sincronização e a tentativa subsequente de build.

A tabela usa a repetição após concluir a build, sem compilação concorrente. A série
inicial permanece em `microbenchmark_initial.csv` e não entra nessas médias.
A build Release subsequente passou (exit 0), compilou `coop_apt_probe.cpp` e
linkou o jogo. SHA256 do executável atualizado:
`d54eda8ae0ad62dc3c7601863de17e624e3a5c7717841500995cf7d39430f55a`.
Não houve teste novo de gameplay ou equivalência visual dos dois HUDs; os testes
de fronteira e conteúdo validam a operação alterada, não certificam o co-op inteiro.
