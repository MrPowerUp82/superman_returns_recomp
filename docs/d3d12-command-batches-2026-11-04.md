# D3D12: lotes do worker — 04/11/2026

O D3D12 passa de 32 para 128 comandos por lote. O worker recebe os mesmos
comandos na mesma ordem; o limite de 4 MiB e os flushes explícitos no swap
permanecem. Vulkan mantém 32 por padrão. `SR_NATIVE_BATCH_COMMANDS=32` restaura
a cadência anterior; 128 e 256 também podem ser selecionados explicitamente.
O parser da política é compartilhado com o log para registrar o limite real.

## Medição

Benchmark do sobrevoo do título, RTX 2060, D3D12 nativo, janela 1280×720,
escala 100%, sombras/MSAA 1, FXAA, VSync e geração de quadros desligados,
FPS sem limite. Cada rodada tem 5 s de aquecimento e 60 s medidos. Modo
headless mantém o render ativo fora da área visível. Não mede scanout nem
garante o mesmo desempenho com a janela em primeiro plano ou em gameplay.

O perfil diagnóstico inicial mostrou GPU real em torno de 3 ms e thread do
jogo em torno de 21 ms; o front-end contribuiu aproximadamente 5 ms. Esses
tempos são de um perfil parcial e incluem instrumentação, não são números
de aceitação. As rodadas abaixo desligam os perfis e usam o mesmo EXE/runtime.
Nenhum build ou teste concorrente ocorreu durante suas janelas.

| Rodada | Lote 32: FPS / 1% low / p99 ms | Lote 128: FPS / 1% low / p99 ms |
| --- | --- | --- |
| 1 | 34,96 / 11,40 / 62,09 | 41,25 / 16,32 / 42,32 |
| 2 (ordem inversa) | 37,89 / 20,44 / 41,57 | 42,77 / 16,47 / 43,13 |
| 3 | 41,05 / 8,80 / 53,44 | 41,56 / 15,19 / 49,58 |
| Mediana | 37,89 / 11,40 / 53,44 | 41,56 / 16,32 / 43,13 |

Ganho de **9,7% na mediana de FPS** e redução de **19,3% no p99** das três
rodadas. O FPS médio melhorou nos três pares, mas o ganho por par variou
de 1,3% a 18,0%. A segunda referência teve melhores lows que a variante.
Não há controle de câmera, temperatura ou clocks: esta é evidência local,
com variação considerável, não uma promessa geral nem eliminação de stutter.
Persistiram pausas de 245–267 ms nas rodadas com 128.

256 comandos foi descartado como padrão: duas rodadas deram 42,71 e 38,66 FPS,
com 1% low de 13,83 e 8,55 FPS; pior equilíbrio de média e cauda que 128.
Ordem completa: 32, 128, 256, 256, 128, 32, 32, 128.

## Reprodução e evidências

```powershell
python tools/bench/d3d12_fps.py reference --batch 32
python tools/bench/d3d12_fps.py candidate --batch 128
python tools/bench/d3d12_fps.py diagnostic --profile
```

Use rótulos novos: o script recusa sobrescrever rodadas. Argumentos, hashes
de EXE/runtime, variáveis SR, timestamps, status de saída e resultados ficam em
`artifacts/d3d12-fps-20261104/<rótulo>/`. JSON e CSV completos são preservados;
nenhum outlier é removido. `summary.json` contém as medianas. O executável e
runtime anteriores ao trabalho estão em `reference/`. Caches existentes não
foram apagados: os testes são aquecidos, sem alegação de cache frio.

O padrão novo usa exatamente o tamanho de lote comparado. A suite nativa
inclui teste de defaults por backend, overrides válidos e fallback de entradas
inválidas. A conferência final do padrão é separada das rodadas de aceitação.

## Conferência da build final

Build concluída e CTest 5/5 aprovado. `final_clean`, 60 s sem override de lote,
confirmou `batch_limit=128` no log e terminou válido: **42,3 FPS**, **19,6 FPS
de 1% low**, sem captura ou instrumentação durante a medição.

`final_visual` capturou as superfícies via readback numa rodada separada de
30 s: 32,65 FPS e máximo 515,06 ms, preservados sem entrar nas medianas.
O custo dessa operação e a duração diferente impedem compará-la diretamente
à série. A imagem `final_visual/dump/output_1280x720.png` mostra cidade,
personagem, capa, texturas e iluminação presentes, sem novo defeito evidente
nesse quadro. Não é gate pixel a pixel nem validação de toda a campanha.
O cabeçalho do output indica 1920×1080, formato R10G10B10A2, apesar do nome
fixo do arquivo e dos argumentos de janela 1280×720; a escala interna ficou
em 1 em todas as rodadas. Nenhuma resolução foi reduzida para obter o ganho.

O ganho permanece limitado pela CPU e não demonstra 60 FPS constantes.
