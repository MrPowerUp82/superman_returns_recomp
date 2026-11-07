# M1: identidade restrita de partes rígidas

Contrato escrito antes da implementação. Reusar exclusivamente os wrappers de
render `8230F8E0`/`823103C8` e o submit `8261CD90` já observados. Nenhum novo hook,
alteração de código gerado ou fatal. A implementação não classifica veículos e
não habilita movimento de toda uma família de shaders por endereço ou ordem.

## Evidência e rota aceita

O documento `instance-owner-lifetime-evidence-2026-10-26.md` audita o construtor
base `822E8BD0`, cleanup `822F54D8`, a cadeia derivada `822F6F68` e os vtables
`820211C0 -> 8230F8E0` / `82021930 -> 823103C8`. Os wrappers existentes em
`guest_cpu_profile.cpp` verificam dispatch e transportam o token de geração.

No submit direto `8261CD90`, aceitar somente:

| Render / retorno | Owner | Modelo salvo | Count | Slot do asset / cursor |
| --- | --- | --- | --- | --- |
| `8230F8E0` / `823102B0` | r30 | caller SP+132 | caller SP+136 | r24 / r29 |
| `823103C8` / `82310D50` | r30 | caller SP+136 | r14 | r23 / r29 |

Fontes geradas auditadas: `superman_returns_recomp.67.cpp`, função `8230F8E0`,
e `.29.cpp`, função `823103C8`. Ambas obtêm modelo por
`owner+88 -> +12 -> +12`; `model+108 != 0` habilita o array `model+12`, cujo
count está em `model+8`. O cursor precisa ser exatamente `parts+4*slot`, o
packet em r3 precisa ser `*cursor`, e slot precisa estar dentro do count.
Os renders passam `owner+128` como transform em `8230FD64` / `82310864`.
Esses valores são seletores do array do asset, não ordinais de draws emitidos.
Outros branches (`8261CED0`, `82617798`, caminhos enfileirados) ficam rejeitados.

## Identidade e inputs

Congelar no frontend a parte com origens distintas 4/5, modelo, array, slot e
offset, somente após validar a cadeia completa e o token vivo. O helper puro
produz uma chave com owner, domínio render/vtable, namespace modelo/parts e
seletor de parte; a geração fica separada. Packet, transform e constantes
mutáveis não entram na identidade entre quadros. As mesmas seleções podem
aparecer em vários draws; conflitos de inputs continuam ambíguos.

Aceitar somente os cinco VS rígidos já auditados por
`static_world_shader_dependencies.h`: 4F172, 18B4F, C5E8, C63F e 7D1AA.
Preservar o VS, todas as constantes float/bool atuais, world rows e clip rows
object-to-clip como inputs atuais; nunca interpretar WVP do objeto como câmera
compartilhada. Uma assinatura não nula dos bindings reais atuais é exigida,
mas sua origem e completude são responsabilidade do consumidor: deve incluir
todos os streams/IB/fetch, gerações, versões de conteúdo e layouts realmente
usados. Ela permanece no registro de inputs/ambiguidade, sem substituir a chave
canônica de malha e a prova de ordem dos vértices.

O consumidor deve manter checks de uploads reais, mesh/VB/IB/fetch canônicos,
gerações, versões, topologia/ordem, câmera, cobertura, limites, frame consecutivo,
resize/cut e descarte por ambiguidade. Esses helpers sozinhos não autorizam
`kVerifiedInstance` ou NGX; a integração no renderer é uma mudança separada.

## Verificação e limites

Adicionar testes CPU positivos para ambas as rotas e cinco VS, rejeições da
cadeia/token/slot/packet, preservação de inputs e estabilidade da chave quando
matrizes mudam. Não executar builds/testes/GPU durante a matriz concorrente.
Antes de anunciar veículos cobertos, observar draws reais com essas rotas,
transform mutável e correspondência de malha entre quadros. A evidência atual
permite implementar a seleção restrita; não demonstra cobertura de veículos,
instancing genérico, pooling/copy não observado ou todo draw de um desses VS.
