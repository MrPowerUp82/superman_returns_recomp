# Auditoria dos resolves empilhados e readback — 27/10/2026

## Achado confirmado e escopo

O código atual tem `sr_native_stacked_resolves=true`; os documentos de 23/10
descrevem o padrão anterior Off. O fetch 640×640×4 e o receptor
`8A6DD1D52752FAD6` continuam exigindo Texture3D. Não foi demonstrado outro
defeito de identidade/binding nesse caso; não há evidência para trocar o SRV
por Texture2DArray.

O defeito original estava na observabilidade: `DumpFrameResources` obtinha um
footprint e copiava o subresource inteiro, mas sua chamada a
`WritePaddedReadbackRows(..., rows)` gravava apenas Z0. Para Texture3D, mip0 é
um único subresource contendo todos os slices; não se deve incrementar o
índice de subresource para acessar Z1–Z3.

Os quatro arquivos existentes `resolve_1E820000_f37_640x640.raw` em
`logs/cutscene_probe/`, `logs/hud_verified/`, `logs/warworld_probe/` e
`logs/cutscene_test/dump/` têm exatamente **1.638.416 bytes**. Seus headers
little-endian são `(640,640,37,2560)`: 16 bytes de header mais uma única
camada de 640×2560 bytes. Eles não permitem afirmar que os quatro slices
do volume sejam distintos ou corretos. Nem estabelecem se esses dumps
anteriores vieram de um recurso 2D ou 3D.

## Helper e integração

`port/src/graphics/readback_rows.h` agora oferece:

```cpp
struct ReadbackVolumeLayout {
  size_t offset;
  uint32_t row_bytes, row_pitch, rows, depth;
  size_t slice_pitch;
};
bool WritePaddedReadbackSlice(std::ostream&, std::span<const uint8_t>,
                            const ReadbackVolumeLayout&, uint32_t slice);
```

O helper valida o volume inteiro antes de escrever o payload de qualquer
slice. Rejeita linhas/slices sobrepostos, dimensões vazias, Z fora dos
limites, fonte truncada e aritmética que ultrapassaria os limites; não lê
padding da GPU. Cada saída conserva as linhas com padding zero do formato
2D existente, incluindo a última linha cujo padding não esteja mapeado.

Integração do agente principal em `DumpFrameResources` (ponto anterior:
`native_renderer.cpp` perto de 8179):

1. Após `GetCopyableFootprints`, validar `row_size <= UINT32_MAX`,
   `rows > 0`, `fp.Footprint.RowPitch > 0` e produto
   `uint64_t(fp.Footprint.RowPitch) * rows <= SIZE_MAX`.
2. Somente para mip0 Texture3D, exigir igualdade de largura, altura, formato
   e `fp.Footprint.Depth == desc.DepthOrArraySize`. Não estender essas
   igualdades a superfícies 2D de depth: formato/plano do footprint pode
   diferir do recurso. Usar `rows` retornado pela API
   para o stride, pois formatos em blocos não têm uma linha por pixel.
3. Manter a cópia completa atual: origem subresource0, destino buffer com
   footprint, XYZ zero, source box `nullptr`. Esse contrato já inclui todo
   o volume; o erro era a gravação no disco.
4. Layout `{size_t(fp.Offset), uint32_t(row_size), fp.Footprint.RowPitch,
   rows, fp.Footprint.Depth, size_t(fp.Footprint.RowPitch)*rows}`.
   Exportar cada Z chamando `WritePaddedReadbackSlice`.
5. Preservar o nome de arquivo e o header de quatro dwords para 2D.
   Para volumes, usar um arquivo por Z com sufixo inequívoco e o mesmo
   header. Um manifesto ou log deve associar identidade do recurso, dimensões,
   profundidade e arquivos. Registrar falhas de qualquer slice.

Não foi alterado `native_renderer.cpp` por esta investigação.

## Amostragem das sombras e janelas

O frame existente `logs/stacked_fix_20261023_on2_frame.json` contém 463 draws
do receptor. Todos têm slot4:

```
80800002 12B38086 0FC1F83F 00A80C14 00000183 12C38C18
srv 27, texture 64x64x64, DXGI 28
```

A reflection de `artifacts/shaders/catalog.json` identifica `noise_map`
sampler3D no slot4, `KLight` em PS c31, `gLight_freq_Klight` em c30 e
`gLodFade_nr_far` em c32. No HLSL desse PS, linha 1113 define o predicado
por `gLodFade_nr_far.x > r4.w`; linhas 2187–2440 amostram `noise_map`,
modulam a emissão e a somam à cor. Isso identifica uma via concreta de
emissão de fachada, ainda sem prova por isolamento visual de um draw.
Os valores dessas constantes não foram capturados nesse frame.

`samp_depth` é outro sampler3D, slot15. Suas três consultas nas linhas
1574–1584 usam os centros Z .625/.125/.375 com c48.x=.25 conforme o trace
anterior; filtragem linear nos centros não mistura camadas vizinhas.

| Fetch capturado | Swizzle | Clamp U/V/W | Mag/min/mip | Volume mag/min |
| --- | --- | --- | --- | --- |
| Shadow slot15 | R,R,R,1 | clamp/clamp/clamp | linear/linear/base map | linear/linear |
| Noise slot4 | B,G,R,A | wrap/wrap/wrap | linear/linear/linear | linear/linear |

Os dois fetches têm sinais unsigned e aniso guest zero. O campo mip=2
significa base map; o shadow usa um recurso de mip único. Para os valores
observados, o sampler básico e Texture3D.Sample são compatíveis com os
filtros U/V/W. `GetSamplerIndex` e `SamplerKey` não consideram os filtros
de volume do dword4: é uma limitação geral quando W difere de U/V, mas não
é uma causa demonstrada aqui, pois os filtros observados concordam.

## Verificação e limite

Compilação focada C++20 com Clang23 e execução de `main.cpp` mais
`test_readback_rows.cpp`: **7 testes, zero falhas**. Primeiro RED: tipos e
função novos ausentes. Uma mutação temporária que ignorou o índice Z,
fora do checkout, compilou e causou exatamente três falhas no teste das
quatro camadas. O helper real passou. Os casos incluem prefixo, padding
de linha e slice distintos, última linha sem padding mapeado, truncamento
de Z3, overflow, Z inválido, compatibilidade 2D e erro de stream.

A suíte completa e o build/jogo ficam com o agente principal. A coleta real
abaixo confirma a exportação das quatro camadas. Hashes distintos
isoladamente não comprovam fidelidade das sombras ao console.

Verificador offline disponível:

```powershell
python tools/analysis/check_stacked_dump.py <diretorio> --prefix <prefixo> --depth 4
```

O endereço padrão é `1E820000`, alterável com `--base`. O modo antigo 2D
exige `--legacy-2d` no lugar de `--depth`. O script rejeita conjuntos
parciais, identidades misturadas e arquivos inconsistentes; reporta SHA256
do arquivo e dos pixels sem padding, grupos idênticos e estatísticas R/G
antes do swizzle. DXGI37 é **R16G16_SNORM**, confirmado no header do Windows
SDK; DXGI34 é R16G16_FLOAT e usa decodificação IEEE float16. Dez testes
Python passaram após o RED por módulo ainda ausente. O dump real antigo
`logs/cutscene_probe/` foi aceito no modo 2D.

## Coleta real das quatro camadas

Diretório: `port/out/build/win-amd64-release/logs/native_focus_shadow_on_20261027`.
Verificação executada com prefixo vazio, base padrão e `--depth 4`.
Relatório completo: [native_focus_shadow_slices_20261027.json](../logs/native_focus_shadow_slices_20261027.json).

O conjunto `resolve_1E820000_f37_640x640_id1_z000.raw` até `_z003.raw`
passou na validação de completude, identidade, headers e tamanho. Os quatro
headers são `(640,640,37,2560)`; cada arquivo tem 1.638.416 bytes e cada
canal tem 409.600 amostras. As distribuições de R e G coincidem em cada
camada; a tabela mostra os valores SNORM antes do swizzle.

| Z | Mínimo | Máximo | Média R/G | Negativos por canal | Zeros por canal |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 1 | 1 | 1 | 0 | 0 |
| 1 | -1 | 1 | 0,06862429279911 | 190.745 | 0 |
| 2 | -1 | 1 | 0,7747133619780142 | 46.136 | 0 |
| 3 | 0 | 0 | 0 | 0 | 409.600 |

SHA256 dos arquivos completos, incluindo header:

| Z | SHA256 |
| --- | --- |
| 0 | `a963e78ee7550ab2e88f0bb2cc68bb82d6a96a798a3218e7e976299f758c2253` |
| 1 | `936438e3c2294c2f840f1dd0fa784ff7a547b188d4b7f755d6a71849f7c4056e` |
| 2 | `1ed0e70dff35874f933e8054194dd0f4a9f2d7ce4fb3a309319c3aae46ec60ba` |
| 3 | `d7f80af79b1ef2d17ec0bf3749554e08e0ddd582a08fcc6367c8390a462581b1` |

Os quatro hashes dos pixels, sem padding, também são distintos; não há
grupos idênticos. Z3 é integralmente zero, compatível com a inicialização
da camada sem resolve observado. Z0 está saturado em +1 nesta captura;
isso descreve o conteúdo exportado e não estabelece se ele é correto.

`logs/game.log`, 18:55:41.651, confirma `depth=4`,
`slice_pitch=1638400`, `row_bytes=2560`, `row_pitch=2560`. O resumo foi
30/31 recursos e 33 slices: a guarda inicial rejeitou a superfície 2D
`surf_02D08000_1280x720_depth`. O ajuste acordado com o agente principal
limita as igualdades estritas a Texture3D; a rejeição da superfície 2D não
invalida o conjunto de quatro slices verificado. Não foi alegada
fidelidade de sombras, iluminação ou imagem em relação ao console.
