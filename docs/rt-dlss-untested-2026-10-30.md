# RT sem validação em GPU — 30/10/2026

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

## Não implementado

**DLSS** continua sem `Evaluate` no jogo. Faltam: SDK instalado (a licença da NVIDIA
precisa de autorização), vetores de movimento em todo quadro (hoje é uma captura
opt-in de dois quadros), jitter aplicado, domínios de resolução em `GetSurface` e
`ExecResolve`, e o ponto HDR antes do tonemap ainda não qualificado em runtime.

**Reflexos com cor** exigem capturar atributos e materiais por triângulo, geometria
sem viés, TLAS construído antes do consumo e avaliação iluminada no ponto de acerto.

## O que conferir na RTX 2060

1. Ligar RT Shadows e procurar `native RT shadow variant ready` no log para ver
   quais PS entram de fato.
2. Comparar Character Shadows On/Off com o Superman em cena.
3. Ligar RT Reflections e procurar escurecimento indevido em fachadas.
