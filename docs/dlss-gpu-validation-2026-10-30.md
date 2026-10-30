# DLSS nativo: correção e validação na RTX 2060

## Problemas encontrados

O build local estava com `SR_DLSS=OFF`. Depois de recompilar com o SDK oficial
3.7.20 já instalado, NGX inicializou e retornou suporte e dimensões Quality
corretas: entrada 1280×720, saída 1920×1080. Porém não havia avaliação: o log
de diagnóstico mostrou cena `0x0`, mesmo com câmera e profundidade disponíveis.

A captura ocorria no próximo draw depois do filtro B397 ou na apresentação.
Nesse intervalo, o resolve já limpava os alvos vinculados. No benchmark o HUD
também é cortado, então não existe aquele próximo draw. O novo hook roda após
o draw real do filtro terminar, em todos os cinco caminhos de draws guest,
antes dos resolves e do HUD. Draws suprimidos não autorizam avaliação.

A profundidade passa a ser capturada junto da câmera reconhecida, em vez de
ser sobrescrita por draws de outra câmera. Um frame sem avaliação válida
invalida o histórico para a próxima tentativa. O estado aplicado também
preserva Quality já confirmado na geração atual enquanto um novo ticket está
pendente; antes, o processamento contínuo poderia deixar a interface sempre
em Pending. Falhas/Off continuam invalidando a geração.

## Build e uso

`build.cmd` detecta o SDK local pelo manifesto e habilita o adapter. Em um
CMake novo, a detecção exige o conjunto de arquivos necessário; cache existente
e `SR_DLSS=OFF` explícito continuam respeitados. Para um cache antigo Off:

```powershell
$env:SR_DLSS='ON'
.\build.cmd
```

O SDK deve estar em `.tools/dlss-v3.7.20` ou ser instalado pelo
`tools/setup_dlss.ps1`. A DLL oficial assinada e a licença são copiadas para o
diretório de execução. Não há DLL proprietária adicionada ao Git.

Usar renderer native, D3D12, GPU RTX e MSAA desativado (amostras=1). Selecionar
NVIDIA DLSS / Quality no menu, ou iniciar com `--sr_dlss=quality`. SDK disponível
não significa recurso aplicado: o log deve confirmar avaliação, composição e
fence da GPU, e snapshots de frames com `applied=2`.

## Evidência em GPU

Placa: RTX 2060; driver 617.42; runtime oficial NGX/DLSS 3.7.20 com assinatura
NVIDIA validada. `logs/dlss_probe_02` reproduziu a rejeição; `dlss_probe_03`
confirmou a correção do ponto de captura. `logs/dlss_quality_final_01/result.json`
valida várias avaliações, confirmação de fence, composição e estado Quality.
O benchmark headless terminou válido, exit 0. Os números de FPS não comparam
resoluções equivalentes e não devem ser tratados como ganho de desempenho.

O dump do swap 2100 contém cena RGBA8 1280×720, saída NGX RGBA8 1920×1080 e
saída apresentada R10G10B10A2 1920×1080. O nome histórico `output_1280x720.raw`
é mantido para ferramentas antigas; dimensões/formato reais estão no cabeçalho
de quatro uint32 (width, height, DXGI format, row pitch). Não interpretar o
R10G10B10A2 como RGBA8. PNG da apresentação inspecionado: cena da cidade válida.

No ensaio `dlss_pixels_linear_01`, com gamma de apresentação desligado, 100%
dos pixels apresentados ficaram a até 2/255 da saída NGX, com erro médio de
0,0562/255 após decodificar o framebuffer R10G10B10A2. O benchmark omite HUD;
isso comprova contribuição real de DLSS nessa cena, não apenas chamadas à API.
Último snapshot: 816 avaliações, 814 confirmações e 816 composições, `applied=2`.
O ensaio Off não registrou avaliação NGX e terminou válido. Com RT solicitado,
`dlss_with_rt_final_01` também terminou válido: último snapshot com 678
avaliações, 675 confirmações e 678 composições. Não é validação visual do RT.

Regressão: CTest nativo 5/5, incluindo contratos DLSS sem SDK, e runtime/XMA
6/6 aprovados. O executável local foi recompilado com `SR_DLSS=ON`. A preferência
local passou a `dlss=1`, mantendo os outros campos; foi preservado backup
`.before-dlss-fix`. Esses arquivos locais não entram no Git.
O ensaio `dlss_saved_preferences_final_01` não passou `--sr_dlss`: inicializou
pela preferência salva e terminou válido, incluindo RT solicitado. Último
snapshot: 765 avaliações, 762 confirmações, 765 composições, `applied=2`.
Os cinco ensaios finais (Quality, pixels lineares, Off, RT e preferência salva)
terminaram válidos e sem falha de avaliação NGX.

Para repetir a validação real (falha se não avaliar/compor/concluir em GPU):

```powershell
python tools/bench/bench_dlss.py --mode quality
python tools/bench/bench_dlss.py --mode off
python tools/bench/bench_dlss.py --mode quality --capture-swap 2100 --linear-output
python tools/analysis/check_dlss_capture.py logs/<nome-do-ensaio>/capture
```

## Limites

O adapter mantém movimento apenas de câmera. Objetos, personagem e capa não
possuem vetores próprios: podem apresentar rastros. Esta correção não implementa
movimento por objeto/deformação. A associação pelo filtro é a já auditada para
título/cidade; outras cenas podem cair no raster se não fornecerem entradas.
A composição do HUD continua usando diferença de pixels, com as limitações
documentadas em `rt-dlss-untested-2026-10-30.md`. Não se declara validação visual
de todo o jogo nem eliminação de todos os artefatos temporais.

Referência de API: [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS/tree/a11125971f4d5f19ce17ca8cbe6f703eaa16de76).
