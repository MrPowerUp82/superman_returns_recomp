Objetivo: fornecer o contrato concreto dos hooks para classificar a dupla de superfícies default ANTES do primeiro clear, com geração/lifecycle reais.

Contexto: recomp C++ Superman Returns, D3D12; a arquitetura e implementação estão aprovadas em C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp/docs/superpowers/specs/2026-10-23-native-renderer-parity-design.md. O orquestrador implementa o renderer/resolução; esta pesquisa é independente e somente leitura.

Onde olhar (raiz absoluta R=C:/Users/Gusta/Documents/outros-projetos/superman_returns_recomp): R/docs/rt-dlss-continuation-2026-10-26.md; R/port/generated/default/superman_returns_recomp.61.cpp sub_82621258, .9.cpp sub_8261F648, .105.cpp sub_82616C40, .88.cpp sub_8261FAE0/sub_8261FBD8; R/port/src/native_renderer/instance_probe.cpp e native_renderer.cpp/.h; R/port/superman_returns_manifest.toml.

Faça: confirme os registradores de entrada/retorno e campos de configuração, sucesso/falha e todas as reconstruções da dupla color82986F60/depth82986F20 publicada em82998868/6C. Proponha os POUCOS hooks existentes para invalidar antes de teardown e avançar geração depois de construção bem-sucedida. Enumere valores de formato/samples/largura/altura verificáveis e como congelar a observação nos WorkCmd (não ler globals futuros no worker). Identifique hookduplicado ou chamada já sobrescrita. Estado82998864 NÃO é geração.

Não faça: editar/criar/apagar arquivos, executar comandos, builds ou jogo; use apenas leitura de arquivos. Não delegue nem use rede. Não invente semântica de gameplay a partir da dupla default.

Saída: resumo até5linhas; contrato C++ proposto (somente interfaces); achados com arquivo e âncoras PPC exatas; dúvidas. Pronto quando os hooks e sucesso forem implementáveis sem adivinhar registradores/offsets. Limite a pesquisa a esse caminho.
