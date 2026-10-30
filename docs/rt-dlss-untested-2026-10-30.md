# RT sem validação em GPU — 30/10/2026

Atualização: este registro descreve a implementação inicial. A integração DLSS
foi posteriormente corrigida e testada na RTX 2060, com avaliação NGX, fence,
composição e leitura da imagem. Ver [validação DLSS em GPU](dlss-gpu-validation-2026-10-30.md).
Isso não substitui a validação visual de RT nem implementa movimento de objetos.

Branch `feat/rt-dlss-untested`, escrita em um notebook só com Intel UHD, por
decisão do usuário. **Nada aqui executou em GPU com DXR.** O que foi verificado:
compilação do jogo, testes nativos e DXC dos 14 shaders especializados (`ps_6_5`).

## Implementado

- **Character Shadows:** dois BLAS (mundo e personagem) e máscara de instância
  1 ou 3. Personagem = caster com VS 57A (paleta de skin). É uma heurística: skin
  não prova personagem, e veículos ou a capa podem cair na classe errada.
- **RT Shadows:** além de PS 8A6D, mais 13 PS com o mesmo bloco `p0` de fade,
  mudando só os registradores. Continua exigindo VS 2C45, único dos 174 VS com a
  exportação de mundo em TC2 no formato auditado. Se esses PS não forem usados com
  2C45, a cobertura não aumenta; os demais draws mantêm a sombra original.
- **RT Reflections, só oclusão:** nos 6 PS com uma amostra `env_map` (0D62, 6922,
  8A6D, 8C2D, ADE7, CD5A), um acerto multiplica o ambiente por 0,25. **Não há
  cor refletida.** O menu diz isso. Não cumpre o contrato de
  `rt-reflection-input-contract-2026-10-25.md`: usa casters deslocados, `TMin` 0,25,
  `TMax` 2000 e fator 0,25 sem validação. Autointerseção pode escurecer demais.

Character Shadows e RT Reflections dependem de RT Shadows ligado.

## DLSS

O SDK v3.7.20 foi instalado com autorização do usuário em `.tools/dlss-v3.7.20`
(fora do Git) e o jogo compila com `SR_DLSS=ON`. `build.cmd` continua com Off por
padrão: use `SR_DLSS=ON` para ter a opção. Redistribuir ainda exige revisar a licença.

Só em GPU com suporte, a saída passa a 1,5x (1920x1080 para cena de 1280x720, sem
mexer na resolução das superfícies). A cena continua em 1280x720 e, com DLSS ligado:

1. os draws de cena (`guest_format` 12, fora de tela cheia) recebem jitter Halton;
2. no primeiro draw depois do filtro final PS B397, a cena é copiada e um compute
   gera profundidade R32 e movimento a partir da câmera (c0 do VS 2C45);
3. NGX sobe a cópia, e a apresentação usa o resultado onde a imagem final não mudou
   depois do filtro. O que mudou (o HUD) vem da imagem normal esticada.

Limites conhecidos:

- **Movimento só de câmera** (`motionCameraOnlyAccepted`): objetos e capa podem ter
  rastro. O menu avisa.
- PS B397 foi auditado na cidade e na tela de título. Em cena sem esse filtro, ou em
  cinemáticas, o DLSS não roda naquele quadro e o histórico é reiniciado.
- A profundidade é a do último draw de cena. A captura de 24/10 concordou com os
  triângulos em só 302 de 576 amostras, então o movimento pode sair errado.
- `dlss_scene_ds_` guarda um ponteiro de superfície dentro do quadro, assumindo que
  ela não é removida antes do filtro.
- Limite de 2/255 para detectar HUD: elementos muito transparentes ficam de fora, e
  o jitter pode marcar pixels como HUD se a resolução final for diferente da cópia.
- Com suporte a DLSS, a saída fica 1,5x mesmo com a opção Off (upscale bilinear).
- Falha do NGX desliga o DLSS na sessão e mantém a imagem normal.

## Reflexos com cor

Exigem capturar atributos e materiais por triângulo, geometria sem viés, TLAS
construído antes do consumo e avaliação iluminada no ponto de acerto.

## O que conferir na RTX 2060

1. Ligar RT Shadows e procurar `native RT shadow variant ready` no log para ver
   quais PS entram de fato.
2. Comparar Character Shadows On/Off com o Superman em cena.
3. Ligar RT Reflections e procurar escurecimento indevido em fachadas.
4. Compilar com `SR_DLSS=ON`, ligar NVIDIA DLSS e procurar `native DLSS` no log.
5. Comparar voo e parado: rastro, tremor do jitter e bordas do HUD.
