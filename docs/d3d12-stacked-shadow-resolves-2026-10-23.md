# Experimento D3D12 de shadow resolves empilhados

Escopo aprovado: evolução do renderer atual, D3D12 primeiro. O áudio continua adiado. Este incremento trata o armazenamento e o binding raster observado; RT, DLSS, menu e 60 FPS continuam pendentes.

## Dados anteriores ao patch

O diagnóstico `stacked_args_20261023_game1` capturou o frame 2506, com 2.891 draws, 69 pares e zero shaders desconhecidos. Os seis resolves do destino `1E820000` usaram nível 0 e slices **0, 0, 1, 1, 2, 2**; cada par usou flags `EC000000` e `EC000100`. O fetch descreve 640×640×4. O receptor `8A6DD1D52752FAD6` usa Texture3D no DXIL, conforme a investigação em `parity-d3d12-foundation-2026-10-23.md`.

Nos 751 draws desse receptor, PS c48 foi `(0.25,0,0,0)`. As coordenadas Z das três consultas correspondem aos centros normalizados `.625`, `.125` e `.375` de quatro slices. Assim, o experimento conserva Texture3D e o shader traduzido existente; um SRV Texture2DArray não corresponderia à declaração compilada.

O VS receptor `2C45BFFE76D77398` teve uma única matriz c0–c3 e uma única transformação c5–c7 em 751 draws. A posição candidata `(-2268.6973,1177.973,-411.48022)` produz clip W aproximadamente zero ao multiplicar pela matriz candidata. Isso apoia a investigação de câmera, mas ainda não implementa um contrato temporal nem confirma toda a convenção de profundidade.

## Implementação experimental

`sr_native_stacked_resolves=false` permanece Off por padrão e exige reinício. Quando On, somente resolves stacked de cor, mip único 0, uma amostra e dimensões host até 2048 criam um recurso Texture3D. O executor encaminha nível/slice já capturados e copia a região para o Z correspondente. Todas as camadas são inicializadas com zero, incluindo a quarta camada que não recebeu resolve no frame observado.

A criação do SRV usa Texture3D somente para o recurso correspondente, validando tamanho e camadas do fetch; o shared block seleciona o heap 3D somente quando existe esse volume. O caminho conserva invalidação de slots, retirada de recursos/views por fences e clears existentes. Mips, depth e MSAA não são estendidos por este experimento e mantêm fallback explícito. Não há suporte geral novo para toda textura stacked estática.

## Verificação

Quatro novos testes puros verificam o fetch real, todas as quatro camadas, limites e rejeições, e a seleção de heap pela view efetiva. RED por header ausente, seguido de **240 testes nativos, zero falhas**. A build incremental experimental passou; logs `stacked_resolve_20261023_{red,native_build,native_green,build}.log`.

A comparação usa o mesmo EXE SHA256 `CC8113A63A6355763864A619EB3674DD86AF26DFBF48116B1C93EBF89E510258`, runtime SHA256 `BDF4B0E8D6F7421C74AFBA67460866952BA8E8E7ECEC8FC560EBF898FDC36DC1`, D3D12 1280×720, escala 1, MSAA 1, limite 60, VSync On, tracking On, shared constants cache Off, sem perfil detalhado. Manifestos preservam argumentos e PIDs. A build release normal mantém SHA256 `C1FC299694D25F4BE7115A7C5BBD9CC7674C2CA3C7FE67A11F3220B9B1BF75A8`.

O SDK não conseguiu habilitar a camada de debug D3D12 nesta instalação. Portanto, não houve validação dessa camada. A execução On inicial `on1` foi interrompida por configuração incorreta do tracking e excluída da comparação; a tentativa válida é `on2`.

| Medida | Off (`off1`) | On (`on2`) |
| --- | ---: | ---: |
| Frame / draws | 3651 / 2.791 | 2034 / 1.920 |
| Pares / shaders desconhecidos | 65 / 0 | 66 / 0 |
| Draws do receptor PS 8A6DD1D52752FAD6 | 689 | 463 |
| Eventos stacked slot 15 | 1.645 | 1.061 |
| Recurso / heap 3D do slot 15 | 2D / nulo 1 | volume / SRV 3 |
| Cópias Z registradas no trace | não disponíveis | 0,0,1,1,2,2 |
| Volumes criados | 0 | 1 (640×640×4, DXGI 37) |
| Rejeições stacked do decoder | 0 | 0 |
| Fallback stacked não suportado | 0 | 0 |
| Linhas de falha/removal de device | 0 | 0 |

Os eventos de binding incluem outros receptores que usam o mesmo slot; não equivalem somente aos draws do PS identificado. O evento informativo de habilitação de DRED contém as palavras Device Removed, mas não é falha de device e foi excluído dessa contagem. Relatório bruto: `logs/stacked_fix_20261023_comparison.json`.

New Game e voo sobre a cidade foram observados com On; janelas iluminadas continuam visíveis. Capturas: `logs/stacked_fix_20261023_{off1,on2}_trace.jpg`, `on2_flight.jpg` e `on2_flight_end.jpg`. As câmeras de idle ficaram diferentes; não houve gate de imagem alinhada, portanto não afirmar melhora visual quantitativa, sombras fiéis ao console ou ganho causal de FPS. O que foi confirmado é a correção do armazenamento/cópia/binding observados, com gameplay e voo funcionais nesta configuração. Processos de teste próprios foram encerrados.

Houve stalls de carregamento no caminho On, com dumps do watchdog e posterior retomada. A causa desses stalls não foi isolada. Este teste não constitui aceitação de desempenho. A revisão independente do diff não encontrou P1/P2 acionáveis; confirmou limites, inicialização, views e lifetimes no caminho observado. A portabilidade da cópia 2D→3D em drivers antigos continua uma limitação: a [especificação Microsoft](https://microsoft.github.io/DirectX-Specs/d3d/VulkanOn12.html#texture-copy-between-inequal-dimension-textures) formaliza esse suporte a partir de DDI 12_8_0091/WDDM 3.1.

O cvar permanece Off. Próximas verificações visuais: capturas alinhadas e inspeção/readback das camadas, além de referência comparável do console para alegar fidelidade. O gargalo de CPU e os contratos temporal/de cena continuam no escopo aprovado, antes da integração de DLSS/RT/menu.
