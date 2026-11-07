# M4 — união das descrições observadas após teste frio

Design escrito antes de alterar os assets. Não mudar shaders nem lógica de
renderização, mascarar contadores ou remover os fallbacks de compilação.

## Falha reproduzida

Executável `565cd1d26e13a1ba36f3fd77b2eecadfa3a7e1afa3ca967a6ea044940e2fe641`,
rota `milestones_resume_cold_vulkan_r1`, medição60s: semente303, cache/journal
locais ausentes. O benchmark foi válido como medição, mas reprovou o gate M4:
`runtime_attempts=2`, `description_misses=2`. O contador real de shader misses,
jobs e compilações de shaders permaneceu0. O campo agregado `shader_misses`
da rota inclui description misses e por isso registrou2. A falha está preservada
em logs; os quatro caches originais foram restaurados por bytes/hash.

## Dados e escolha

O journal original Vulkan tinha306 entradas; o produzido pela rota fria tem305.
Ambos passam magic SRVPSO, versão1, ABI1, count/bounds, ausência de duplicatas,
FNV64 e consumo integral. A união é308: os306 anteriores e duas observações
novas da rota fria. Os303 registros da semente anterior são preservados.

O CNPS original D3D12 tem6495 registros e, unido ao seed1024 pelo packer existente,
resulta em1050 descrições distintas por bytes normalizados sem chave. Seus26
registros adicionais também são observações reais, não combinações sintetizadas.
O renderer ainda faz sua deduplicação semântica e não precisa criar1050 PSOs
distintos. Preservar todos os inputs e seus hashes antes de publicar os assets.

## Implementação

1. Arquivar seed1024/303 e fontes dos journals com metadata de hashes.
2. Publicar a união CNPS1050 usando `pack_pipeline_seed.py --merge` existente.
3. Publicar SRVPSO308, mantendo o layout/ABI e recomputando FNV64. Somente
   descrições numéricas: nenhum bytecode opaco de driver/shader entra no seed.
4. Atualizar a expectativa do teste de asset Vulkan e exigir que o decoder de
   produção reconstrua todos308 estados, layouts e formatos sem handles.
5. Build Release e comparar `.text` com o executável565cd1: espera-se somente
   alteração dos recursos. Métricas de M2 permanecem vinculadas ao hash565cd1,
   mesmo se a seção de código for idêntica no executável seguinte.
6. Repetir três rotas frias de60s em cada backend, no mesmo executável novo,
   sem builds/capturas concorrentes. Rejeitar qualquer miss/criação runtime;
   preservar falhas e repetir após corrigir sua causa. Restaurar caches no finally.

## Aceite e fronteira

O auditor exige fingerprint do binário/bibliotecas, contadores delimitados,
configuração e captura real. Dump de shaders e screenshots são feitos em
execuções diagnósticas separadas. Não apagar o cache implícito do driver NVIDIA.
Zero runtime refere-se somente às rotas testadas. Fases não percorridas continuam
fora do aceite; se uma nova rota produzir novas descrições, o seed deve crescer
com essas observações e a validação deve ser repetida.

## Segundo achado, antes da ampliação seguinte

O executável accfa8/seed1050 passou na primeira rota D3D12, mas a segunda
registrou sete criações de PSO e seis descrições serializadas novas. Ambas as
medições permanecem vinculadas ao accfa8 e a segunda reprova M4. Incorporar as
seis observações, chegando a1056 descrições, sem inventar estados. O log tem
aliases distintos para uma mesma descrição, daí a diferença entre6 e7.
Arquivar também seed1050 e esse journal antes da troca. Repetir o grupo inteiro.

O próximo harness coleta as três rodadas dos dois backends mesmo quando um
gate de cache falha; cada rota recebe seu aceite explícito e o conjunto só
retorna sucesso se todas passarem. Crash, medição inválida ou mudança de hash
continuam interrompendo o ensaio. Esse ajuste amplia a observação sem converter
miss em aprovação e evita esconder novas descrições após a primeira falha.
