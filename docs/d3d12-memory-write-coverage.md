# Cobertura de escritas físicas no runtime D3D12

Revisão base: ReXGlue v0.10.0, `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
Estado: integração em desenvolvimento; **não há autorização mecânica de pular hashes**.

O SDK stock não exporta o contrato v1. O teste real de memória falhou, como esperado,
por export ausente nos três casos iniciais. O build opcional usa checkout completo
separado, patch versionado e manifesto SHA256; o recorte de GPU não é modificado.

| Classe | Origem no SDK | Mecanismo / situação |
| --- | --- | --- |
| Stores guest e host físico | `src/system/xmemory.cpp`, `Memory::MapViews`, `AccessViolationCallback` | Guardas de CPU de uso único; perda de cobertura publicada antes de liberar o store |
| Aliases A/C/E | `Memory::MapViews`, `PhysicalHeap::GetPhysicalAddress` | Proteger todas as vistas; E usa offset físico de 4 KiB e mapeamento Windows arredondado a 64 KiB |
| Alias adicional 7F | `Memory::MapViews`, GPU writeback de 16 MiB | Também mapeia os primeiros 16 MiB físicos, embora LookupHeap retorne null para essa vista |
| Alloc/fixed/range e reuso | `BaseHeap`, heap físico pai | Registrar identidade uma vez no pai; alias não cria alocação física independente |
| Proteção, commit, decommit, release, reset | Todos os `Protect`/`AllocFixed` de `src/system/xmemory.cpp`, inclusive recovery de proteção obsoleta | Invalidar antes da chamada OS, sob região crítica; reset não recicla contadores |
| Leitura de arquivos host | `XFile::ReadInternal` → VFS → `Win32FileHandle::Read` | Precisa de begin/end explícito; kernel não gera o fault de CPU necessário. Pendente |
| Scatter de arquivos | `XFile::ReadScatter` → `ReadInternal` | Mesmo escopo de I/O, inclusive falhas/parcial. Pendente |
| Recepção de rede | `XSocket::Recv`, `RecvFrom` → Winsock | Precisa de begin/end e liberação de proteção do buffer antes de entregar ao kernel. Pendente |
| Áudio, memexport e resolves com cópia CPU | XMA e `src/graphics/d3d12/command_processor.cpp` | Stores comuns pelo ponteiro físico devem atingir guardas; não chamar data providers no fault físico |
| Novos mapeamentos ou produtores fora do inventário | Extensões/plugins/API do host | Cobertura desconhecida; não anunciar capability HostProducers sem fechar o inventário |

Na primeira versão, um fault invalida a alocação até nova geração. Não rearmar no fim
do frame. Guardas pertencentes ao SDK não são desfeitas pelo monitor: qualquer
alteração externa de proteção retira a propriedade antes da mudança. A consulta
de múltiplas faixas e a instalação de guardas usam a região crítica global; os
metadados puros têm seu próprio mutex, sempre adquirido depois dela.

A instalação não deve certificar um snapshot enquanto houver stores anteriores
pendentes. A implementação completa deverá proteger todas as vistas antes da
cópia e executar uma barreira entre processadores antes da primeira certificação.
A documentação de [VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect)
descreve a alteração de proteção das páginas comprometidas; a de
[FlushProcessWriteBuffers](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushprocesswritebuffers)
garante visibilidade das escritas entre processadores. Isso não substitui os
scopes de I/O nem permite rearmar uma alocação após fault. O custo da barreira
inicial deve entrar na medição de criação de watches.

O contrato inicialmente anuncia somente lifecycle/aliases, guards e observação
das mudanças de proteção (`11`); falta HostProducers (`4`). O renderer deve exigir
a máscara completa (`15`), logo não pode usar esta versão para dispensar hash.
Os testes de guarda observam mudanças de época sem falsificar cobertura completa.

Verificação inicial: doze casos no runtime real passaram (CTest 1/1), cobrindo
stores A/C/E/raw em heaps de 4/64 KiB, alias 7F, alloc/range/release/reuso,
decommit/reset/restore (incluindo reset de alias e restore de páginas livres),
alteração de proteção compartilhada, remoção de watch e manutenção
das proteções pertencentes ao SDK. A suíte pura contém 209 casos, e os testes
Python de ferramentas passaram em 73/73. Isso ainda não certifica os produtores
host, uploads D3D12 ou ganho de FPS.

Reprodução: `powershell -NoProfile -File tools/setup_memory_tracking_sdk.ps1`;
depois `SR_MEMORY_TRACKING_SDK=ON`, `SR_BUILD_DIR=port/out/build/win-amd64-write-tracking`
e `build.cmd`. `-VerifyOnly` valida origem/patch/arquivos. O setup não descarta nem
sobrescreve edições desconhecidas. As fontes compartilhadas em `tools/` são a
cópia canônica; o diretório `.tools` é apenas materialização para build.

Compatibilidade do build fonte no Windows: o patch usa os arquivos reais de
`libmspack/libmspack/mspack`, pois `cabextract/mspack` contém symlinks que este Git
materializa como arquivos de texto. O build opcional também aplica
`-march=x86-64-v2`, como o preset `windows-base` do SDK. O build stock passou após
a introdução da opção, sem mudar seus flags.
